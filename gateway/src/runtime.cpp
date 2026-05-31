// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/gateway/runtime.hpp"

#include <algorithm>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cxxmcp/gateway/catalog.hpp"
#include "cxxmcp/gateway/error.hpp"
#include "cxxmcp/gateway/router.hpp"
#include "cxxmcp/peer.hpp"
#include "cxxmcp/protocol/capabilities.hpp"
#include "cxxmcp/service.hpp"
#include "cxxmcp/transport/process_stdio_transport.hpp"

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

bool gateway_owned_error(const core::Error& error) {
  return error.category.rfind("gateway", 0) == 0;
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
    builder.resources(false, false);
    builder.prompts(false);
  }
  return builder.build();
}

protocol::ServerCapabilities gateway_server_capabilities(
    const GatewayConfig& config,
    const std::unordered_map<std::string, UpstreamRuntimeState>& states) {
  auto builder = protocol::server_capabilities();
  if (!validate_gateway_config(config)) {
    return builder.build();
  }

  bool has_enabled_upstream = false;
  bool has_complete_capability_cache = true;
  bool advertise_tools = false;
  bool advertise_resources = false;
  bool advertise_prompts = false;
  bool advertise_completions = false;

  for (const auto& upstream : config.upstreams) {
    if (!upstream.enabled) {
      continue;
    }
    has_enabled_upstream = true;
    const auto state = states.find(upstream.id);
    if (state == states.end() || !state->second.capabilities.has_value()) {
      has_complete_capability_cache = false;
      break;
    }
    advertise_tools =
        advertise_tools || state->second.capabilities->tools.enabled;
    advertise_resources =
        advertise_resources || state->second.capabilities->resources.enabled;
    advertise_prompts =
        advertise_prompts || state->second.capabilities->prompts.enabled;
    advertise_completions = advertise_completions ||
                            state->second.capabilities->completions.enabled;
  }

  if (!has_enabled_upstream) {
    return builder.build();
  }

  if (!has_complete_capability_cache) {
    return gateway_server_capabilities(config);
  }

  if (advertise_tools) {
    builder.tools(false);
  }
  if (advertise_resources) {
    builder.resources(false, false);
  }
  if (advertise_prompts) {
    builder.prompts(false);
  }
  if (advertise_completions) {
    builder.completions();
  }
  return builder.build();
}

core::Result<ClientPeer> build_client_peer(const UpstreamServer& upstream) {
  auto builder = ClientPeer::builder();
  builder.capabilities(protocol::client_capabilities().build());
  switch (upstream.transport) {
    case UpstreamTransportKind::process_stdio: {
      transport::ProcessStdioClientTransportOptions options;
      options.command = upstream.process_stdio.command;
      options.args = upstream.process_stdio.args;
      options.cwd = upstream.process_stdio.cwd;
      options.env = upstream.process_stdio.env;
      options.request_timeout = upstream.process_stdio.timeout;
      return builder
          .transport(std::make_unique<transport::ProcessStdioClientTransport>(
              std::move(options)))
          .build();
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
  struct CatalogCache {
    std::optional<std::vector<protocol::ToolDefinition>> tools;
    std::optional<std::vector<protocol::Resource>> resources;
    std::optional<std::vector<protocol::ResourceTemplate>> resource_templates;
    std::optional<std::vector<protocol::Prompt>> prompts;
  };

  struct PersistentUpstreamSession {
    std::mutex mutex;
    std::condition_variable available;
    struct Slot {
      bool in_use = false;
      std::optional<RunningService<RoleClient>> service;
      std::optional<protocol::ServerCapabilities> capabilities;
    };
    std::vector<Slot> slots;

    explicit PersistentUpstreamSession(std::size_t pool_size)
        : slots(pool_size) {}
  };

  explicit Impl(GatewayConfig config, GatewayRuntimeOptions options)
      : router(std::move(config)),
        upstream_session_mode(options.upstream_session_mode),
        persistent_session_pool_size(
            std::max<std::size_t>(options.persistent_session_pool_size, 1)),
        observer(std::move(options.observer)) {
    for (const auto& upstream : router.config().upstreams) {
      upstream_states.emplace(upstream.id,
                              UpstreamRuntimeState{
                                  .upstream_id = upstream.id,
                                  .status = UpstreamRuntimeStatus::configured,
                              });
      persistent_sessions.emplace(upstream.id,
                                  std::make_unique<PersistentUpstreamSession>(
                                      persistent_session_pool_size));
    }
  }

  GatewayRouter router;
  std::mutex service_mutex;
  std::shared_ptr<RunningService<RoleServer>> service;
  mutable std::mutex upstream_state_mutex;
  std::condition_variable upstream_idle_cv;
  std::unordered_map<std::string, UpstreamRuntimeState> upstream_states;
  mutable std::mutex catalog_cache_mutex;
  CatalogCache catalog_cache;
  UpstreamSessionMode upstream_session_mode = UpstreamSessionMode::per_call;
  std::size_t persistent_session_pool_size = 1;
  std::unordered_map<std::string, std::unique_ptr<PersistentUpstreamSession>>
      persistent_sessions;
  GatewayRuntimeObserver observer;
  bool stopping = false;
  bool stopped = false;

  void notify_runtime_event(GatewayRuntimeEvent event) const noexcept {
    if (!observer) {
      return;
    }
    try {
      observer(event);
    } catch (...) {
    }
  }

  void notify_upstream_status(
      std::string_view upstream_id, UpstreamRuntimeStatus status,
      std::optional<core::Error> error = std::nullopt) const noexcept {
    notify_runtime_event(GatewayRuntimeEvent{
        .kind = GatewayRuntimeEventKind::upstream_status_changed,
        .upstream_id = std::string(upstream_id),
        .upstream_status = status,
        .error = std::move(error),
    });
  }

  void notify_runtime_lifecycle(GatewayRuntimeEventKind kind,
                                UpstreamRuntimeStatus status) const noexcept {
    notify_runtime_event(GatewayRuntimeEvent{
        .kind = kind,
        .upstream_status = status,
    });
  }

  void notify_all_upstream_statuses(
      UpstreamRuntimeStatus status) const noexcept {
    for (const auto& upstream : router.config().upstreams) {
      notify_upstream_status(upstream.id, status);
    }
  }

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
    {
      std::lock_guard lock(upstream_state_mutex);
      set_upstream_status_locked(upstream_id, status);
    }
    notify_upstream_status(upstream_id, status);
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
    {
      std::lock_guard lock(upstream_state_mutex);
      auto& state = upstream_states[std::string(upstream_id)];
      state.upstream_id = std::string(upstream_id);
      state.status = UpstreamRuntimeStatus::healthy;
      state.capabilities = std::move(capabilities);
      state.last_error.reset();
    }
    notify_upstream_status(upstream_id, UpstreamRuntimeStatus::healthy);
  }

  core::Error mark_upstream_degraded(
      std::string_view upstream_id, core::Error error,
      std::optional<protocol::ServerCapabilities> capabilities =
          std::nullopt) {
    {
      std::lock_guard lock(upstream_state_mutex);
      auto& state = upstream_states[std::string(upstream_id)];
      state.upstream_id = std::string(upstream_id);
      state.status = UpstreamRuntimeStatus::degraded;
      if (capabilities.has_value()) {
        state.capabilities = std::move(capabilities);
      }
      state.last_error = error;
    }
    auto observed_error = error;
    notify_upstream_status(upstream_id, UpstreamRuntimeStatus::degraded,
                           std::move(observed_error));
    return error;
  }

  std::optional<protocol::ServerCapabilities> cached_capabilities(
      std::string_view upstream_id) const {
    std::lock_guard lock(upstream_state_mutex);
    const auto it = upstream_states.find(std::string(upstream_id));
    if (it == upstream_states.end()) {
      return std::nullopt;
    }
    return it->second.capabilities;
  }

  bool has_complete_capability_cache() const {
    std::lock_guard lock(upstream_state_mutex);
    for (const auto& upstream : router.config().upstreams) {
      if (!upstream.enabled) {
        continue;
      }
      const auto it = upstream_states.find(upstream.id);
      if (it == upstream_states.end() || !it->second.capabilities.has_value()) {
        return false;
      }
    }
    return true;
  }

  bool should_list_tools_for(const UpstreamServer& upstream) const {
    if (!has_complete_capability_cache()) {
      return true;
    }
    const auto capabilities = cached_capabilities(upstream.id);
    return capabilities.has_value() && capabilities->tools.enabled;
  }

  bool should_list_resources_for(const UpstreamServer& upstream) const {
    if (!has_complete_capability_cache()) {
      return true;
    }
    const auto capabilities = cached_capabilities(upstream.id);
    return capabilities.has_value() && capabilities->resources.enabled;
  }

  bool should_list_prompts_for(const UpstreamServer& upstream) const {
    if (!has_complete_capability_cache()) {
      return true;
    }
    const auto capabilities = cached_capabilities(upstream.id);
    return capabilities.has_value() && capabilities->prompts.enabled;
  }

  core::Result<core::Unit> require_tool_capability(
      const UpstreamServer& upstream) const {
    const auto capabilities = cached_capabilities(upstream.id);
    if (capabilities.has_value() && !capabilities->tools.enabled) {
      return mcp::core::unexpected(make_gateway_error(
          protocol::ErrorCode::MethodNotFound,
          "gateway upstream does not support tools", upstream.id));
    }
    return core::Unit{};
  }

  core::Result<core::Unit> require_resource_capability(
      const UpstreamServer& upstream) const {
    const auto capabilities = cached_capabilities(upstream.id);
    if (capabilities.has_value() && !capabilities->resources.enabled) {
      return mcp::core::unexpected(make_gateway_error(
          protocol::ErrorCode::MethodNotFound,
          "gateway upstream does not support resources", upstream.id));
    }
    return core::Unit{};
  }

  core::Result<core::Unit> require_prompt_capability(
      const UpstreamServer& upstream) const {
    const auto capabilities = cached_capabilities(upstream.id);
    if (capabilities.has_value() && !capabilities->prompts.enabled) {
      return mcp::core::unexpected(make_gateway_error(
          protocol::ErrorCode::MethodNotFound,
          "gateway upstream does not support prompts", upstream.id));
    }
    return core::Unit{};
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
    std::lock_guard lock(upstream_state_mutex);
    return gateway_server_capabilities(router.config(), upstream_states);
  }

  std::optional<std::vector<protocol::ToolDefinition>> cached_tools() const {
    std::lock_guard lock(catalog_cache_mutex);
    return catalog_cache.tools;
  }

  std::optional<std::vector<protocol::Resource>> cached_resources() const {
    std::lock_guard lock(catalog_cache_mutex);
    return catalog_cache.resources;
  }

  std::optional<std::vector<protocol::ResourceTemplate>>
  cached_resource_templates() const {
    std::lock_guard lock(catalog_cache_mutex);
    return catalog_cache.resource_templates;
  }

  std::optional<std::vector<protocol::Prompt>> cached_prompts() const {
    std::lock_guard lock(catalog_cache_mutex);
    return catalog_cache.prompts;
  }

  void store_tools(std::vector<protocol::ToolDefinition> tools) {
    std::lock_guard lock(catalog_cache_mutex);
    catalog_cache.tools = std::move(tools);
  }

  void store_resources(std::vector<protocol::Resource> resources) {
    std::lock_guard lock(catalog_cache_mutex);
    catalog_cache.resources = std::move(resources);
  }

  void store_resource_templates(
      std::vector<protocol::ResourceTemplate> resource_templates) {
    std::lock_guard lock(catalog_cache_mutex);
    catalog_cache.resource_templates = std::move(resource_templates);
  }

  void store_prompts(std::vector<protocol::Prompt> prompts) {
    std::lock_guard lock(catalog_cache_mutex);
    catalog_cache.prompts = std::move(prompts);
  }

  void clear_cached_catalogs_unchecked() {
    std::lock_guard lock(catalog_cache_mutex);
    catalog_cache = CatalogCache{};
  }

  core::Result<core::Unit> clear_cached_catalogs() {
    auto accepting = ensure_runtime_accepting("clear_cached_catalogs");
    if (!accepting) {
      return mcp::core::unexpected(accepting.error());
    }
    clear_cached_catalogs_unchecked();
    return core::Unit{};
  }

  PersistentUpstreamSession& persistent_session_for(
      std::string_view upstream_id) {
    return *persistent_sessions.at(std::string(upstream_id));
  }

  bool runtime_is_stopping_or_stopped() const {
    std::lock_guard lock(upstream_state_mutex);
    return stopping || stopped;
  }

  void notify_persistent_session_waiters() noexcept {
    for (auto& [_, session] : persistent_sessions) {
      session->available.notify_all();
    }
  }

  core::Result<PersistentUpstreamSession::Slot*>
  acquire_persistent_session_slot(PersistentUpstreamSession& session,
                                  std::string_view upstream_id) {
    std::unique_lock lock(session.mutex);
    session.available.wait(lock, [this, &session] {
      return runtime_is_stopping_or_stopped() ||
             std::any_of(session.slots.begin(), session.slots.end(),
                         [](const auto& slot) { return !slot.in_use; });
    });
    if (runtime_is_stopping_or_stopped()) {
      return mcp::core::unexpected(runtime_error(
          "gateway runtime is stopping", std::string(upstream_id)));
    }
    auto slot = std::find_if(session.slots.begin(), session.slots.end(),
                             [](const auto& candidate) {
                               return !candidate.in_use;
                             });
    slot->in_use = true;
    return &*slot;
  }

  core::Result<std::vector<PersistentUpstreamSession::Slot*>>
  acquire_all_persistent_session_slots(PersistentUpstreamSession& session,
                                       std::string_view upstream_id) {
    std::unique_lock lock(session.mutex);
    session.available.wait(lock, [this, &session] {
      return runtime_is_stopping_or_stopped() ||
             std::all_of(session.slots.begin(), session.slots.end(),
                         [](const auto& slot) { return !slot.in_use; });
    });
    if (runtime_is_stopping_or_stopped()) {
      return mcp::core::unexpected(runtime_error(
          "gateway runtime is stopping", std::string(upstream_id)));
    }
    std::vector<PersistentUpstreamSession::Slot*> slots;
    slots.reserve(session.slots.size());
    for (auto& slot : session.slots) {
      slot.in_use = true;
      slots.push_back(&slot);
    }
    return slots;
  }

  void release_persistent_session_slot(
      PersistentUpstreamSession& session,
      PersistentUpstreamSession::Slot& slot) noexcept {
    {
      std::lock_guard lock(session.mutex);
      slot.in_use = false;
    }
    session.available.notify_one();
  }

  void release_persistent_session_slots(
      PersistentUpstreamSession& session,
      const std::vector<PersistentUpstreamSession::Slot*>& slots) noexcept {
    {
      std::lock_guard lock(session.mutex);
      for (auto* slot : slots) {
        slot->in_use = false;
      }
    }
    session.available.notify_all();
  }

  core::Result<core::Unit> ensure_persistent_session_initialized(
      const UpstreamServer& upstream, PersistentUpstreamSession::Slot& slot) {
    if (slot.service.has_value() && slot.service->running()) {
      return core::Unit{};
    }

    slot.service.reset();
    slot.capabilities.reset();
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

    slot.capabilities = capabilities;
    slot.service.emplace(std::move(*running));
    mark_upstream_healthy(upstream.id, capabilities);
    return core::Unit{};
  }

  void discard_persistent_session_slot(PersistentUpstreamSession::Slot& slot) {
    if (slot.service.has_value()) {
      (void)slot.service->stop();
      slot.service.reset();
    }
    slot.capabilities.reset();
  }

  void stop_persistent_sessions() noexcept {
    for (auto& [_, session] : persistent_sessions) {
      std::lock_guard lock(session->mutex);
      for (auto& slot : session->slots) {
        discard_persistent_session_slot(slot);
      }
    }
  }

  template <class Result, class Fn>
  core::Result<Result> with_initialized_upstream_per_call(
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

    auto result = [&]() -> core::Result<Result> {
      if constexpr (std::is_invocable_v<
                        Fn, ClientPeer&,
                        const std::optional<protocol::ServerCapabilities>&>) {
        return fn(running->peer(), capabilities);
      } else {
        return fn(running->peer());
      }
    }();
    (void)running->stop();
    if (!result) {
      if (gateway_owned_error(result.error())) {
        mark_upstream_healthy(upstream.id, std::move(capabilities));
        return mcp::core::unexpected(result.error());
      }
      return mcp::core::unexpected(mark_upstream_degraded(
          upstream.id, annotate_gateway_upstream_error(result.error(),
                                                       upstream.id),
          std::move(capabilities)));
    }
    mark_upstream_healthy(upstream.id, std::move(capabilities));
    return result;
  }

  template <class Result, class Fn>
  core::Result<Result> with_persistent_initialized_upstream(
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

    auto& session = persistent_session_for(upstream.id);
    auto acquired_slot = acquire_persistent_session_slot(session, upstream.id);
    if (!acquired_slot) {
      return mcp::core::unexpected(acquired_slot.error());
    }
    auto& slot = **acquired_slot;
    struct SlotGuard {
      Impl& impl;
      PersistentUpstreamSession& session;
      PersistentUpstreamSession::Slot& slot;
      ~SlotGuard() {
        impl.release_persistent_session_slot(session, slot);
      }
    } slot_guard{*this, session, slot};

    auto initialized = ensure_persistent_session_initialized(upstream, slot);
    if (!initialized) {
      discard_persistent_session_slot(slot);
      return mcp::core::unexpected(initialized.error());
    }

    auto& running = *slot.service;
    auto capabilities = slot.capabilities;
    auto result = [&]() -> core::Result<Result> {
      if constexpr (std::is_invocable_v<
                        Fn, ClientPeer&,
                        const std::optional<protocol::ServerCapabilities>&>) {
        return fn(running.peer(), capabilities);
      } else {
        return fn(running.peer());
      }
    }();

    if (!result) {
      if (gateway_owned_error(result.error())) {
        mark_upstream_healthy(upstream.id, capabilities);
        return mcp::core::unexpected(result.error());
      }
      auto error = mark_upstream_degraded(
          upstream.id,
          annotate_gateway_upstream_error(result.error(), upstream.id),
          capabilities);
      discard_persistent_session_slot(slot);
      return mcp::core::unexpected(std::move(error));
    }
    mark_upstream_healthy(upstream.id, capabilities);
    return result;
  }

  core::Result<core::Unit> prewarm_persistent_upstream_pool(
      const UpstreamServer& upstream) {
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

    auto& session = persistent_session_for(upstream.id);
    auto acquired_slots =
        acquire_all_persistent_session_slots(session, upstream.id);
    if (!acquired_slots) {
      return mcp::core::unexpected(acquired_slots.error());
    }
    auto slots = std::move(*acquired_slots);
    struct SlotsGuard {
      Impl& impl;
      PersistentUpstreamSession& session;
      const std::vector<PersistentUpstreamSession::Slot*>& slots;
      ~SlotsGuard() {
        impl.release_persistent_session_slots(session, slots);
      }
    } slots_guard{*this, session, slots};

    for (auto* slot : slots) {
      auto initialized = ensure_persistent_session_initialized(upstream, *slot);
      if (!initialized) {
        discard_persistent_session_slot(*slot);
        return mcp::core::unexpected(initialized.error());
      }
    }
    return core::Unit{};
  }

  template <class Result, class Fn>
  core::Result<Result> with_initialized_upstream(
      const UpstreamServer& upstream, Fn&& fn) {
    if (upstream_session_mode == UpstreamSessionMode::persistent) {
      return with_persistent_initialized_upstream<Result>(
          upstream, std::forward<Fn>(fn));
    }
    return with_initialized_upstream_per_call<Result>(upstream,
                                                      std::forward<Fn>(fn));
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
    if (auto cached = cached_tools()) {
      return *cached;
    }

    std::vector<UpstreamToolCatalog> catalogs;
    struct PendingCatalog {
      std::string upstream_id;
      std::future<core::Result<std::vector<protocol::ToolDefinition>>> future;
    };
    std::vector<PendingCatalog> pending;

    for (const auto& upstream : router.config().upstreams) {
      if (!upstream.enabled) {
        continue;
      }
      if (!should_list_tools_for(upstream)) {
        continue;
      }

      pending.push_back(PendingCatalog{
          .upstream_id = upstream.id,
          .future = std::async(
              std::launch::async, [this, upstream]() {
                return with_initialized_upstream<
                    std::vector<protocol::ToolDefinition>>(
                    upstream,
                    [](ClientPeer& peer) { return peer.list_all_tools(); });
              }),
      });
    }

    std::optional<core::Error> first_error;
    for (auto& task : pending) {
      auto tools = task.future.get();
      if (!tools) {
        if (!first_error.has_value()) {
          first_error = tools.error();
        }
        continue;
      }
      catalogs.push_back(
          UpstreamToolCatalog{.upstream_id = task.upstream_id,
                              .tools = std::move(*tools)});
    }
    if (first_error.has_value()) {
      return mcp::core::unexpected(*first_error);
    }

    auto merged = merge_tool_catalogs(catalogs);
    if (merged) {
      store_tools(*merged);
    }
    return merged;
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
    auto supported = require_tool_capability(*route->upstream);
    if (!supported) {
      return mcp::core::unexpected(supported.error());
    }

    protocol::ToolCall call;
    call.name = route->upstream_tool_name;
    call.arguments = std::move(arguments);

    return with_initialized_upstream<protocol::ToolResult>(
        *route->upstream,
        [&](ClientPeer& peer) { return peer.call_tool(call); });
  }

  core::Result<std::vector<protocol::Resource>> list_resources() {
    auto accepting = ensure_runtime_accepting("resources/list");
    if (!accepting) {
      return mcp::core::unexpected(accepting.error());
    }

    auto valid = router.validate_config();
    if (!valid) {
      return mcp::core::unexpected(valid.error());
    }
    if (auto cached = cached_resources()) {
      return *cached;
    }

    std::vector<UpstreamResourceCatalog> catalogs;
    struct PendingCatalog {
      std::string upstream_id;
      std::future<core::Result<std::vector<protocol::Resource>>> future;
    };
    std::vector<PendingCatalog> pending;

    for (const auto& upstream : router.config().upstreams) {
      if (!upstream.enabled) {
        continue;
      }
      if (!should_list_resources_for(upstream)) {
        continue;
      }

      pending.push_back(PendingCatalog{
          .upstream_id = upstream.id,
          .future = std::async(
              std::launch::async, [this, upstream]() {
                return with_initialized_upstream<
                    std::vector<protocol::Resource>>(
                    upstream,
                    [](ClientPeer& peer) { return peer.list_all_resources(); });
              }),
      });
    }

    std::optional<core::Error> first_error;
    for (auto& task : pending) {
      auto resources = task.future.get();
      if (!resources) {
        if (!first_error.has_value()) {
          first_error = resources.error();
        }
        continue;
      }
      catalogs.push_back(UpstreamResourceCatalog{
          .upstream_id = task.upstream_id,
          .resources = std::move(*resources),
      });
    }
    if (first_error.has_value()) {
      return mcp::core::unexpected(*first_error);
    }

    auto merged = merge_resource_catalogs(catalogs);
    if (merged) {
      store_resources(*merged);
    }
    return merged;
  }

  core::Result<protocol::ResourcesReadResult> read_resource(
      std::string_view exposed_uri) {
    auto accepting = ensure_runtime_accepting("resources/read");
    if (!accepting) {
      return mcp::core::unexpected(accepting.error());
    }

    auto valid = router.validate_config();
    if (!valid) {
      return mcp::core::unexpected(valid.error());
    }

    auto route = router.resolve_resource_route(exposed_uri);
    if (!route) {
      return mcp::core::unexpected(route.error());
    }
    auto supported = require_resource_capability(*route->upstream);
    if (!supported) {
      return mcp::core::unexpected(supported.error());
    }

    return with_initialized_upstream<protocol::ResourcesReadResult>(
        *route->upstream, [&](ClientPeer& peer) {
          auto result = peer.read_resource(route->upstream_uri);
          if (!result) {
            return result;
          }
          for (auto& contents : result->contents) {
            const auto upstream_uri =
                contents.uri.empty() ? route->upstream_uri : contents.uri;
            contents.uri = GatewayRouter::expose_resource_uri(
                route->upstream->id, upstream_uri);
          }
          return result;
        });
  }

  core::Result<std::vector<protocol::ResourceTemplate>>
  list_resource_templates() {
    auto accepting = ensure_runtime_accepting("resources/templates/list");
    if (!accepting) {
      return mcp::core::unexpected(accepting.error());
    }

    auto valid = router.validate_config();
    if (!valid) {
      return mcp::core::unexpected(valid.error());
    }
    if (auto cached = cached_resource_templates()) {
      return *cached;
    }

    std::vector<UpstreamResourceTemplateCatalog> catalogs;
    struct PendingCatalog {
      std::string upstream_id;
      std::future<core::Result<std::vector<protocol::ResourceTemplate>>> future;
    };
    std::vector<PendingCatalog> pending;

    for (const auto& upstream : router.config().upstreams) {
      if (!upstream.enabled) {
        continue;
      }
      if (!should_list_resources_for(upstream)) {
        continue;
      }

      pending.push_back(PendingCatalog{
          .upstream_id = upstream.id,
          .future = std::async(
              std::launch::async, [this, upstream]() {
                return with_initialized_upstream<
                    std::vector<protocol::ResourceTemplate>>(
                    upstream, [](ClientPeer& peer) {
                      return peer.list_all_resource_templates();
                    });
              }),
      });
    }

    std::optional<core::Error> first_error;
    for (auto& task : pending) {
      auto resource_templates = task.future.get();
      if (!resource_templates) {
        if (!first_error.has_value()) {
          first_error = resource_templates.error();
        }
        continue;
      }
      catalogs.push_back(UpstreamResourceTemplateCatalog{
          .upstream_id = task.upstream_id,
          .resource_templates = std::move(*resource_templates),
      });
    }
    if (first_error.has_value()) {
      return mcp::core::unexpected(*first_error);
    }

    auto merged = merge_resource_template_catalogs(catalogs);
    if (merged) {
      store_resource_templates(*merged);
    }
    return merged;
  }

  core::Result<std::vector<protocol::Prompt>> list_prompts() {
    auto accepting = ensure_runtime_accepting("prompts/list");
    if (!accepting) {
      return mcp::core::unexpected(accepting.error());
    }

    auto valid = router.validate_config();
    if (!valid) {
      return mcp::core::unexpected(valid.error());
    }
    if (auto cached = cached_prompts()) {
      return *cached;
    }

    std::vector<UpstreamPromptCatalog> catalogs;
    struct PendingCatalog {
      std::string upstream_id;
      std::future<core::Result<std::vector<protocol::Prompt>>> future;
    };
    std::vector<PendingCatalog> pending;

    for (const auto& upstream : router.config().upstreams) {
      if (!upstream.enabled) {
        continue;
      }
      if (!should_list_prompts_for(upstream)) {
        continue;
      }

      pending.push_back(PendingCatalog{
          .upstream_id = upstream.id,
          .future = std::async(
              std::launch::async, [this, upstream]() {
                return with_initialized_upstream<
                    std::vector<protocol::Prompt>>(
                    upstream,
                    [](ClientPeer& peer) { return peer.list_all_prompts(); });
              }),
      });
    }

    std::optional<core::Error> first_error;
    for (auto& task : pending) {
      auto prompts = task.future.get();
      if (!prompts) {
        if (!first_error.has_value()) {
          first_error = prompts.error();
        }
        continue;
      }
      catalogs.push_back(UpstreamPromptCatalog{
          .upstream_id = task.upstream_id,
          .prompts = std::move(*prompts),
      });
    }
    if (first_error.has_value()) {
      return mcp::core::unexpected(*first_error);
    }

    auto merged = merge_prompt_catalogs(catalogs);
    if (merged) {
      store_prompts(*merged);
    }
    return merged;
  }

  core::Result<core::Unit> refresh_upstream_capabilities() {
    auto accepting = ensure_runtime_accepting("refresh_upstream_capabilities");
    if (!accepting) {
      return mcp::core::unexpected(accepting.error());
    }

    auto valid = router.validate_config();
    if (!valid) {
      return mcp::core::unexpected(valid.error());
    }
    clear_cached_catalogs_unchecked();

    for (const auto& upstream : router.config().upstreams) {
      if (!upstream.enabled) {
        continue;
      }

      auto refreshed =
          upstream_session_mode == UpstreamSessionMode::persistent
              ? prewarm_persistent_upstream_pool(upstream)
              : with_initialized_upstream<core::Unit>(
                    upstream, [](ClientPeer&) {
                      return core::Result<core::Unit>{core::Unit{}};
                    });
      if (!refreshed) {
        return mcp::core::unexpected(refreshed.error());
      }
    }

    return core::Unit{};
  }

  core::Result<protocol::PromptsGetResult> get_prompt(
      std::string_view exposed_name, protocol::Json arguments) {
    auto accepting = ensure_runtime_accepting("prompts/get");
    if (!accepting) {
      return mcp::core::unexpected(accepting.error());
    }

    auto valid = router.validate_config();
    if (!valid) {
      return mcp::core::unexpected(valid.error());
    }

    auto route = router.resolve_prompt_route(exposed_name);
    if (!route) {
      return mcp::core::unexpected(route.error());
    }
    auto supported = require_prompt_capability(*route->upstream);
    if (!supported) {
      return mcp::core::unexpected(supported.error());
    }

    protocol::PromptsGetParams request;
    request.name = route->upstream_prompt_name;
    request.arguments = std::move(arguments);

    return with_initialized_upstream<protocol::PromptsGetResult>(
        *route->upstream,
        [&](ClientPeer& peer) { return peer.get_prompt(request); });
  }

  core::Result<protocol::CompleteResult> complete(
      protocol::CompleteParams params) {
    auto accepting = ensure_runtime_accepting("completion/complete");
    if (!accepting) {
      return mcp::core::unexpected(accepting.error());
    }

    auto valid = router.validate_config();
    if (!valid) {
      return mcp::core::unexpected(valid.error());
    }

    const UpstreamServer* upstream = nullptr;
    if (params.ref.type == "ref/prompt") {
      auto route = router.resolve_prompt_route(params.ref.name);
      if (!route) {
        return mcp::core::unexpected(route.error());
      }
      upstream = route->upstream;
      params.ref.name = std::move(route->upstream_prompt_name);
    } else if (params.ref.type == "ref/resource") {
      auto route = router.resolve_resource_route(params.ref.uri.value_or(""));
      if (!route) {
        return mcp::core::unexpected(route.error());
      }
      upstream = route->upstream;
      params.ref.name = route->upstream_uri;
      params.ref.uri = std::move(route->upstream_uri);
    } else {
      return mcp::core::unexpected(make_gateway_error(
          protocol::ErrorCode::InvalidParams,
          "gateway completion ref type is not supported", params.ref.type));
    }

    const auto cached = cached_capabilities(upstream->id);
    if (cached.has_value() && !cached->completions.enabled) {
      return mcp::core::unexpected(make_gateway_error(
          protocol::ErrorCode::MethodNotFound,
          "gateway upstream does not support completion", upstream->id));
    }

    return with_initialized_upstream<protocol::CompleteResult>(
        *upstream,
        [&](ClientPeer& peer,
            const std::optional<protocol::ServerCapabilities>& capabilities)
            -> core::Result<protocol::CompleteResult> {
          if (!capabilities.has_value() || !capabilities->completions.enabled) {
            return mcp::core::unexpected(make_gateway_error(
                protocol::ErrorCode::MethodNotFound,
                "gateway upstream does not support completion", upstream->id));
          }
          return peer.complete(params);
        });
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

    if (request.method == protocol::ResourcesListMethod) {
      auto resources = list_resources();
      if (!resources) {
        return error_response(request, resources.error());
      }
      protocol::ResourcesListResult result;
      result.resources = std::move(*resources);
      return protocol::make_response(
          request.id, protocol::resources_list_result_to_json(result));
    }

    if (request.method == protocol::ResourcesReadMethod) {
      auto read = protocol::resources_read_params_from_json(request.params);
      if (!read) {
        return error_response(request, read.error());
      }
      auto result = read_resource(read->uri);
      if (!result) {
        return error_response(request, result.error());
      }
      return protocol::make_response(
          request.id, protocol::resources_read_result_to_json(*result));
    }

    if (request.method == protocol::ResourcesTemplatesListMethod) {
      auto resource_templates = list_resource_templates();
      if (!resource_templates) {
        return error_response(request, resource_templates.error());
      }
      protocol::ResourceTemplatesListResult result;
      result.resource_templates = std::move(*resource_templates);
      return protocol::make_response(
          request.id,
          protocol::resource_templates_list_result_to_json(result));
    }

    if (request.method == protocol::PromptsListMethod) {
      auto prompts = list_prompts();
      if (!prompts) {
        return error_response(request, prompts.error());
      }
      protocol::PromptsListResult result;
      result.prompts = std::move(*prompts);
      return protocol::make_response(
          request.id, protocol::prompts_list_result_to_json(result));
    }

    if (request.method == protocol::PromptsGetMethod) {
      auto get = protocol::prompts_get_params_from_json(request.params);
      if (!get) {
        return error_response(request, get.error());
      }
      auto result = get_prompt(get->name, std::move(get->arguments));
      if (!result) {
        return error_response(request, result.error());
      }
      return protocol::make_response(
          request.id, protocol::prompts_get_result_to_json(*result));
    }

    if (request.method == protocol::CompletionCompleteMethod) {
      auto completion = protocol::complete_params_from_json(request.params);
      if (!completion) {
        return error_response(request, completion.error());
      }
      auto result = complete(std::move(*completion));
      if (!result) {
        return error_response(request, result.error());
      }
      return protocol::make_response(
          request.id, protocol::complete_result_to_json(*result));
    }

    if (sdk_owned_request_method(request.method)) {
      return std::nullopt;
    }

    return error_response(
        request, make_gateway_error(protocol::ErrorCode::MethodNotFound,
                                    "gateway method is not supported",
                                    request.method));
  }

  core::Result<core::Unit> handle_notification(
      const protocol::JsonRpcNotification& /*notification*/) {
    return core::Unit{};
  }
};

GatewayRuntime::GatewayRuntime(GatewayConfig config)
    : GatewayRuntime(std::move(config), GatewayRuntimeOptions{}) {}

GatewayRuntime::GatewayRuntime(GatewayConfig config,
                               GatewayRuntimeOptions options)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(options))) {}

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

core::Result<std::vector<protocol::ToolDefinition>>
GatewayRuntime::list_tools() {
  return impl_->list_tools();
}

core::Result<protocol::ToolResult> GatewayRuntime::call_tool(
    std::string_view exposed_name, protocol::Json arguments) {
  return impl_->call_tool(exposed_name, std::move(arguments));
}

core::Result<std::vector<protocol::Resource>>
GatewayRuntime::list_resources() {
  return impl_->list_resources();
}

core::Result<protocol::ResourcesReadResult> GatewayRuntime::read_resource(
    std::string_view exposed_uri) {
  return impl_->read_resource(exposed_uri);
}

core::Result<std::vector<protocol::ResourceTemplate>>
GatewayRuntime::list_resource_templates() {
  return impl_->list_resource_templates();
}

core::Result<std::vector<protocol::Prompt>> GatewayRuntime::list_prompts() {
  return impl_->list_prompts();
}

core::Result<protocol::PromptsGetResult> GatewayRuntime::get_prompt(
    std::string_view exposed_name, protocol::Json arguments) {
  return impl_->get_prompt(exposed_name, std::move(arguments));
}

core::Result<protocol::CompleteResult> GatewayRuntime::complete(
    protocol::CompleteParams params) {
  return impl_->complete(std::move(params));
}

core::Result<core::Unit> GatewayRuntime::handle_notification(
    const protocol::JsonRpcNotification& notification) {
  return impl_->handle_notification(notification);
}

std::optional<protocol::JsonRpcResponse> GatewayRuntime::handle_request(
    const protocol::JsonRpcRequest& request) {
  return impl_->handle_request(request);
}

std::vector<UpstreamRuntimeState> GatewayRuntime::upstream_states() const {
  return impl_->snapshot_upstream_states();
}

core::Result<core::Unit> GatewayRuntime::clear_cached_catalogs() {
  return impl_->clear_cached_catalogs();
}

core::Result<core::Unit> GatewayRuntime::refresh_upstream_capabilities() {
  return impl_->refresh_upstream_capabilities();
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
  if (impl_->service && impl_->service->running()) {
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
                  .on_raw_notification(
                      [impl = impl_.get()](
                          const protocol::JsonRpcNotification& notification,
                          const server::SessionContext& /*context*/) {
                        return impl->handle_notification(notification);
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
  impl_->service =
      std::make_shared<RunningService<RoleServer>>(std::move(*running));
  return core::Unit{};
}

core::Result<core::Unit> GatewayRuntime::wait() {
  std::shared_ptr<RunningService<RoleServer>> service;
  std::unique_lock lock(impl_->service_mutex);
  if (!impl_->service) {
    return mcp::core::unexpected(
        runtime_error("gateway http endpoint is not running"));
  }
  service = impl_->service;
  lock.unlock();
  return service->wait();
}

core::Result<core::Unit> GatewayRuntime::stop() noexcept {
  if (!impl_) {
    return core::Unit{};
  }

  std::shared_ptr<RunningService<RoleServer>> service;
  {
    std::lock_guard service_lock(impl_->service_mutex);
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
    service = impl_->service;
  }
  impl_->notify_runtime_lifecycle(GatewayRuntimeEventKind::runtime_stopping,
                                  UpstreamRuntimeStatus::stopping);
  impl_->notify_all_upstream_statuses(UpstreamRuntimeStatus::stopping);
  impl_->notify_persistent_session_waiters();

  core::Result<core::Unit> service_stopped = core::Unit{};
  if (service) {
    service_stopped = service->stop();
    std::lock_guard service_lock(impl_->service_mutex);
    if (impl_->service == service) {
      impl_->service.reset();
    }
  }

  std::unique_lock state_lock(impl_->upstream_state_mutex);
  impl_->upstream_idle_cv.wait(state_lock, [&] {
    return std::all_of(impl_->upstream_states.begin(),
                       impl_->upstream_states.end(), [](const auto& entry) {
                         return entry.second.active_calls == 0;
                       });
  });
  state_lock.unlock();
  impl_->stop_persistent_sessions();
  state_lock.lock();
  impl_->stopping = false;
  impl_->stopped = true;
  for (const auto& upstream : impl_->router.config().upstreams) {
    impl_->set_upstream_status_locked(upstream.id,
                                      UpstreamRuntimeStatus::stopped);
  }
  state_lock.unlock();
  impl_->notify_all_upstream_statuses(UpstreamRuntimeStatus::stopped);
  impl_->notify_runtime_lifecycle(GatewayRuntimeEventKind::runtime_stopped,
                                  UpstreamRuntimeStatus::stopped);
  if (!service_stopped) {
    return mcp::core::unexpected(service_stopped.error());
  }
  return core::Unit{};
}

}  // namespace mcp::gateway
