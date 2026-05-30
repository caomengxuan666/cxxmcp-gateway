// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/gateway/runtime.hpp"

#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "cxxmcp/gateway/catalog.hpp"
#include "cxxmcp/gateway/error.hpp"
#include "cxxmcp/peer.hpp"
#include "cxxmcp/protocol/capabilities.hpp"
#include "cxxmcp/service.hpp"

namespace mcp::gateway {
namespace {

core::Error runtime_error(std::string message, std::string detail = {}) {
  return make_gateway_error(protocol::ErrorCode::InvalidRequest,
                            std::move(message), std::move(detail));
}

protocol::JsonRpcResponse error_response(const protocol::JsonRpcRequest& req,
                                          const core::Error& error) {
  return protocol::make_error_response(
      req.id, protocol::make_error(error.code, error.message,
                                   error.detail.empty()
                                       ? std::nullopt
                                       : std::optional<protocol::Json>{
                                             error.detail}));
}

bool sdk_owned_request_method(std::string_view method) {
  return method == std::string_view(protocol::InitializeMethod) ||
         method == std::string_view(protocol::PingMethod) ||
         method == std::string_view(protocol::ServerDiscoverMethod);
}

protocol::ServerCapabilities gateway_server_capabilities(
    const GatewayConfig& config) {
  auto builder = protocol::server_capabilities();
  if (!validate_gateway_config(config)) {
    return builder.build();
  }

  const auto has_enabled_upstream =
      std::any_of(config.upstreams.begin(), config.upstreams.end(),
                  [](const auto& upstream) { return upstream.enabled; });
  if (has_enabled_upstream) {
    builder.tools(false);
  }
  return builder.build();
}

core::Result<ClientPeer> build_client_peer(const UpstreamServer& upstream) {
  auto builder = ClientPeer::builder();
  switch (upstream.transport) {
    case UpstreamTransportKind::process_stdio: {
      client::Client::StdioEndpoint endpoint;
      endpoint.command = upstream.process_stdio.command;
      endpoint.args = upstream.process_stdio.args;
      endpoint.cwd = upstream.process_stdio.cwd;
      endpoint.env = upstream.process_stdio.env;
      return builder.process_stdio(std::move(endpoint)).build();
    }
    case UpstreamTransportKind::streamable_http: {
      client::Client::StreamableHttpEndpoint endpoint;
      endpoint.uri = upstream.streamable_http.uri;
      endpoint.headers = upstream.streamable_http.headers;
      endpoint.timeout = upstream.streamable_http.timeout;
      return builder.streamable_http(std::move(endpoint)).build();
    }
  }
  return mcp::core::unexpected(make_gateway_error(
      protocol::ErrorCode::InvalidParams, "unknown upstream transport",
      upstream.id));
}

}  // namespace

struct GatewayRuntime::Impl final {
  explicit Impl(GatewayConfig config) : router(std::move(config)) {
    for (const auto& upstream : router.config().upstreams) {
      upstream_states.emplace(upstream.id,
                              UpstreamRuntimeState{
                                  .upstream_id = upstream.id,
                                  .status = UpstreamRuntimeStatus::configured,
                              });
    }
  }

  GatewayRouter router;
  std::mutex service_mutex;
  std::optional<RunningService<RoleServer>> service;
  mutable std::mutex upstream_state_mutex;
  std::condition_variable upstream_idle_cv;
  std::unordered_map<std::string, UpstreamRuntimeState> upstream_states;
  bool stopping = false;
  bool stopped = false;

  core::Result<core::Unit> ensure_runtime_accepting(
      std::string_view operation) const {
    std::lock_guard lock(upstream_state_mutex);
    if (stopped) {
      return mcp::core::unexpected(runtime_error(
          "gateway runtime is stopped", std::string(operation)));
    }
    if (stopping) {
      return mcp::core::unexpected(runtime_error(
          "gateway runtime is stopping", std::string(operation)));
    }
    return core::Unit{};
  }

  void set_upstream_status_locked(std::string_view upstream_id,
                                  UpstreamRuntimeStatus status) {
    auto& state = upstream_states[std::string(upstream_id)];
    state.upstream_id = std::string(upstream_id);
    state.status = status;
  }

  void set_upstream_status(std::string_view upstream_id,
                           UpstreamRuntimeStatus status) {
    std::lock_guard lock(upstream_state_mutex);
    set_upstream_status_locked(upstream_id, status);
  }

  core::Result<core::Unit> begin_upstream_call(std::string_view upstream_id) {
    std::lock_guard lock(upstream_state_mutex);
    if (stopped) {
      return mcp::core::unexpected(runtime_error(
          "gateway runtime is stopped", std::string(upstream_id)));
    }
    if (stopping) {
      return mcp::core::unexpected(runtime_error(
          "gateway runtime is stopping", std::string(upstream_id)));
    }
    auto& state = upstream_states[std::string(upstream_id)];
    state.upstream_id = std::string(upstream_id);
    ++state.active_calls;
    return core::Unit{};
  }

  void finish_upstream_call(std::string_view upstream_id) {
    std::unique_lock lock(upstream_state_mutex);
    auto& state = upstream_states[std::string(upstream_id)];
    state.upstream_id = std::string(upstream_id);
    if (state.active_calls > 0) {
      --state.active_calls;
    }
    const auto no_active_calls = std::all_of(
        upstream_states.begin(), upstream_states.end(), [](const auto& entry) {
          return entry.second.active_calls == 0;
        });
    lock.unlock();
    if (no_active_calls) {
      upstream_idle_cv.notify_all();
    }
  }

  void mark_upstream_healthy(
      std::string_view upstream_id,
      std::optional<protocol::ServerCapabilities> capabilities) {
    std::lock_guard lock(upstream_state_mutex);
    auto& state = upstream_states[std::string(upstream_id)];
    state.upstream_id = std::string(upstream_id);
    state.status = UpstreamRuntimeStatus::healthy;
    state.capabilities = std::move(capabilities);
    state.last_error.reset();
  }

  core::Error mark_upstream_degraded(std::string_view upstream_id,
                                     core::Error error) {
    std::lock_guard lock(upstream_state_mutex);
    auto& state = upstream_states[std::string(upstream_id)];
    state.upstream_id = std::string(upstream_id);
    state.status = UpstreamRuntimeStatus::degraded;
    state.last_error = error;
    return error;
  }

  std::vector<UpstreamRuntimeState> snapshot_upstream_states() const {
    std::lock_guard lock(upstream_state_mutex);
    std::vector<UpstreamRuntimeState> states;
    states.reserve(upstream_states.size());
    for (const auto& [_, state] : upstream_states) {
      states.push_back(state);
    }
    std::sort(states.begin(), states.end(), [](const auto& lhs,
                                               const auto& rhs) {
      return lhs.upstream_id < rhs.upstream_id;
    });
    return states;
  }

  protocol::ServerCapabilities server_capabilities() const {
    return gateway_server_capabilities(router.config());
  }

  template <class Result, class Fn>
  core::Result<Result> with_initialized_upstream(
      const UpstreamServer& upstream, Fn&& fn) {
    struct ActiveCallGuard {
      Impl& impl;
      std::string upstream_id;

      ActiveCallGuard(Impl& owner, std::string_view id)
          : impl(owner), upstream_id(id) {
      }

      ~ActiveCallGuard() { impl.finish_upstream_call(upstream_id); }
    };

    auto started = begin_upstream_call(upstream.id);
    if (!started) {
      return mcp::core::unexpected(started.error());
    }
    ActiveCallGuard active_call(*this, upstream.id);

    set_upstream_status(upstream.id, UpstreamRuntimeStatus::connecting);

    auto peer = build_client_peer(upstream);
    if (!peer) {
      return mcp::core::unexpected(mark_upstream_degraded(
          upstream.id,
          annotate_gateway_upstream_error(peer.error(), upstream.id)));
    }

    auto running = mcp::serve(std::move(*peer));
    if (!running) {
      return mcp::core::unexpected(mark_upstream_degraded(
          upstream.id,
          annotate_gateway_upstream_error(running.error(), upstream.id)));
    }

    const auto initialized = running->peer().initialize(
        router.config().name, router.config().version);
    if (!initialized) {
      (void)running->stop();
      return mcp::core::unexpected(mark_upstream_degraded(
          upstream.id,
          annotate_gateway_upstream_error(initialized.error(), upstream.id)));
    }
    set_upstream_status(upstream.id, UpstreamRuntimeStatus::initialized);

    auto capabilities = running->peer().server_capabilities();

    const auto notified = running->peer().notify_initialized();
    if (!notified) {
      (void)running->stop();
      return mcp::core::unexpected(mark_upstream_degraded(
          upstream.id,
          annotate_gateway_upstream_error(notified.error(), upstream.id)));
    }

    auto result = fn(running->peer());
    (void)running->stop();
    if (!result) {
      return mcp::core::unexpected(mark_upstream_degraded(
          upstream.id,
          annotate_gateway_upstream_error(result.error(), upstream.id)));
    }
    mark_upstream_healthy(upstream.id, std::move(capabilities));
    return result;
  }

  core::Result<std::vector<protocol::ToolDefinition>> list_tools() {
    auto accepting = ensure_runtime_accepting("tools/list");
    if (!accepting) {
      return mcp::core::unexpected(accepting.error());
    }

    auto valid = router.validate_config();
    if (!valid) {
      return mcp::core::unexpected(valid.error());
    }

    std::vector<UpstreamToolCatalog> catalogs;

    for (const auto& upstream : router.config().upstreams) {
      if (!upstream.enabled) {
        continue;
      }

      auto tools = with_initialized_upstream<
          std::vector<protocol::ToolDefinition>>(
          upstream, [](ClientPeer& peer) { return peer.list_all_tools(); });
      if (!tools) {
        return mcp::core::unexpected(tools.error());
      }
      catalogs.push_back(
          UpstreamToolCatalog{.upstream_id = upstream.id,
                              .tools = std::move(*tools)});
    }

    return merge_tool_catalogs(catalogs);
  }

  core::Result<protocol::ToolResult> call_tool(
      std::string_view exposed_name, protocol::Json arguments) {
    auto accepting = ensure_runtime_accepting("tools/call");
    if (!accepting) {
      return mcp::core::unexpected(accepting.error());
    }

    auto valid = router.validate_config();
    if (!valid) {
      return mcp::core::unexpected(valid.error());
    }

    auto route = router.resolve_tool_route(exposed_name);
    if (!route) {
      return mcp::core::unexpected(route.error());
    }

    protocol::ToolCall call;
    call.name = route->upstream_tool_name;
    call.arguments = std::move(arguments);

    return with_initialized_upstream<protocol::ToolResult>(
        *route->upstream,
        [&](ClientPeer& peer) { return peer.call_tool(call); });
  }

  std::optional<protocol::JsonRpcResponse> handle_request(
      const protocol::JsonRpcRequest& request) {
    if (request.method == protocol::ToolsListMethod) {
      auto tools = list_tools();
      if (!tools) {
        return error_response(request, tools.error());
      }
      protocol::ToolsListResult result;
      result.tools = std::move(*tools);
      return protocol::make_response(
          request.id, protocol::tools_list_result_to_json(result));
    }

    if (request.method == protocol::ToolsCallMethod) {
      auto call = protocol::tool_call_from_json(request.params);
      if (!call) {
        return error_response(request, call.error());
      }
      auto result = call_tool(call->name, std::move(call->arguments));
      if (!result) {
        return error_response(request, result.error());
      }
      return protocol::make_response(request.id,
                                     protocol::tool_result_to_json(*result));
    }

    if (sdk_owned_request_method(request.method)) {
      return std::nullopt;
    }

    return error_response(
        request, make_gateway_error(protocol::ErrorCode::MethodNotFound,
                                    "gateway method is not supported",
                                    request.method));
  }
};

GatewayRuntime::GatewayRuntime(GatewayConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

GatewayRuntime::~GatewayRuntime() { (void)stop(); }

GatewayRuntime::GatewayRuntime(GatewayRuntime&&) noexcept = default;

GatewayRuntime& GatewayRuntime::operator=(GatewayRuntime&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  (void)stop();
  impl_ = std::move(other.impl_);
  return *this;
}

const GatewayRouter& GatewayRuntime::router() const noexcept {
  return impl_->router;
}

core::Result<std::vector<protocol::ToolDefinition>>
GatewayRuntime::list_tools() {
  return impl_->list_tools();
}

core::Result<protocol::ToolResult> GatewayRuntime::call_tool(
    std::string_view exposed_name, protocol::Json arguments) {
  return impl_->call_tool(exposed_name, std::move(arguments));
}

std::optional<protocol::JsonRpcResponse> GatewayRuntime::handle_request(
    const protocol::JsonRpcRequest& request) {
  return impl_->handle_request(request);
}

std::vector<UpstreamRuntimeState> GatewayRuntime::upstream_states() const {
  return impl_->snapshot_upstream_states();
}

protocol::ServerCapabilities GatewayRuntime::server_capabilities() const {
  return impl_->server_capabilities();
}

core::Result<core::Unit> GatewayRuntime::start_http(HttpEndpoint endpoint) {
  std::lock_guard lock(impl_->service_mutex);
  auto accepting = impl_->ensure_runtime_accepting("start_http");
  if (!accepting) {
    return mcp::core::unexpected(accepting.error());
  }
  if (impl_->service.has_value() && impl_->service->running()) {
    return mcp::core::unexpected(
        runtime_error("gateway http endpoint is already running"));
  }
  if (endpoint.host.empty()) {
    return mcp::core::unexpected(
        runtime_error("gateway http host must not be empty"));
  }
  if (endpoint.port == 0) {
    return mcp::core::unexpected(
        runtime_error("gateway http port must not be zero"));
  }
  auto valid = impl_->router.validate_config();
  if (!valid) {
    return mcp::core::unexpected(valid.error());
  }

  const auto& config = impl_->router.config();
  auto peer = ServerPeer::builder()
                  .name(config.name)
                  .version(config.version)
                  .capabilities(impl_->server_capabilities())
                  .streamable_http(endpoint.host, endpoint.port, endpoint.path)
                  .raw_request([impl = impl_.get()](
                                   const protocol::JsonRpcRequest& request) {
                    return impl->handle_request(request);
                  })
                  .build();
  if (!peer) {
    return mcp::core::unexpected(peer.error());
  }

  auto running = mcp::serve(std::move(*peer));
  if (!running) {
    return mcp::core::unexpected(running.error());
  }
  running->wait_until_ready();
  impl_->service.emplace(std::move(*running));
  return core::Unit{};
}

core::Result<core::Unit> GatewayRuntime::wait() {
  std::unique_lock lock(impl_->service_mutex);
  if (!impl_->service.has_value()) {
    return mcp::core::unexpected(
        runtime_error("gateway http endpoint is not running"));
  }
  auto* service = &*impl_->service;
  lock.unlock();
  return service->wait();
}

core::Result<core::Unit> GatewayRuntime::stop() noexcept {
  if (!impl_) {
    return core::Unit{};
  }

  std::unique_lock lock(impl_->service_mutex);
  {
    std::lock_guard state_lock(impl_->upstream_state_mutex);
    if (impl_->stopped) {
      return core::Unit{};
    }
    impl_->stopping = true;
    for (const auto& upstream : impl_->router.config().upstreams) {
      impl_->set_upstream_status_locked(upstream.id,
                                        UpstreamRuntimeStatus::stopping);
    }
  }

  if (!impl_ || !impl_->service.has_value()) {
    std::unique_lock state_lock(impl_->upstream_state_mutex);
    impl_->upstream_idle_cv.wait(state_lock, [&] {
      return std::all_of(impl_->upstream_states.begin(),
                         impl_->upstream_states.end(), [](const auto& entry) {
                           return entry.second.active_calls == 0;
                         });
    });
    impl_->stopping = false;
    impl_->stopped = true;
    for (const auto& upstream : impl_->router.config().upstreams) {
      impl_->set_upstream_status_locked(upstream.id,
                                        UpstreamRuntimeStatus::stopped);
    }
    return core::Unit{};
  }
  auto* service = &*impl_->service;
  lock.unlock();
  auto stopped = service->stop();
  lock.lock();
  impl_->service.reset();
  if (!stopped) {
    return mcp::core::unexpected(stopped.error());
  }
  std::unique_lock state_lock(impl_->upstream_state_mutex);
  impl_->upstream_idle_cv.wait(state_lock, [&] {
    return std::all_of(impl_->upstream_states.begin(),
                       impl_->upstream_states.end(), [](const auto& entry) {
                         return entry.second.active_calls == 0;
                       });
  });
  impl_->stopping = false;
  impl_->stopped = true;
  for (const auto& upstream : impl_->router.config().upstreams) {
    impl_->set_upstream_status_locked(upstream.id,
                                      UpstreamRuntimeStatus::stopped);
  }
  return core::Unit{};
}

}  // namespace mcp::gateway
