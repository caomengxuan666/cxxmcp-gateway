// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/gateway/runtime.hpp>
#include <cxxmcp/gateway/config.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>

int main() {
  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "disabled";
  upstream.enabled = false;
  config.upstreams.push_back(std::move(upstream));

  auto valid = mcp::gateway::validate_gateway_config(config);
  if (!valid) {
    return 1;
  }

  std::size_t observed_events = 0;
  mcp::gateway::GatewayRuntimeOptions options;
  options.upstream_session_mode =
      mcp::gateway::UpstreamSessionMode::persistent;
  options.persistent_session_pool_size = 2;
  options.persistent_session_acquire_timeout = std::chrono::milliseconds{100};
  options.active_call_drain_timeout = std::chrono::milliseconds{5000};
  options.observer =
      [&](const mcp::gateway::GatewayRuntimeEvent&) { ++observed_events; };

  mcp::gateway::GatewayRuntime runtime(std::move(config), std::move(options));
  mcp::gateway::GatewayRuntime moved_runtime(std::move(runtime));

  mcp::gateway::GatewayConfig replacement_config;
  mcp::gateway::UpstreamServer replacement_upstream;
  replacement_upstream.id = "replacement";
  replacement_upstream.enabled = false;
  replacement_config.upstreams.push_back(std::move(replacement_upstream));
  mcp::gateway::GatewayRuntime assigned_runtime(std::move(replacement_config));
  assigned_runtime = std::move(moved_runtime);

  auto capabilities = assigned_runtime.server_capabilities();
  auto cleared = assigned_runtime.clear_cached_catalogs();
  if (!cleared) {
    return 1;
  }
  auto refreshed = assigned_runtime.refresh_upstream_capabilities();
  if (!refreshed) {
    return 1;
  }

  auto invalid_start = assigned_runtime.start_http(
      {.host = "", .port = 3000, .path = "/mcp"});
  if (invalid_start) {
    return 1;
  }
  auto wait_before_start = assigned_runtime.wait();
  if (wait_before_start) {
    return 1;
  }

  auto notification = assigned_runtime.handle_notification(
      mcp::protocol::make_notification(
          mcp::protocol::CancelledNotificationMethod,
          mcp::protocol::Json{{"requestId", std::int64_t{42}},
                              {"reason", "package-smoke"}}));
  if (!notification) {
    return 1;
  }

  mcp::protocol::JsonRpcRequest tools_list;
  tools_list.method = mcp::protocol::ToolsListMethod;
  tools_list.id = std::int64_t{43};
  auto tools_list_response = assigned_runtime.handle_request(tools_list);
  if (!tools_list_response.has_value() ||
      !tools_list_response->has_result() ||
      !tools_list_response->result->contains("tools") ||
      !tools_list_response->result->at("tools").is_array() ||
      !tools_list_response->result->at("tools").empty()) {
    return 1;
  }

  auto stopped = assigned_runtime.stop();
  if (!stopped) {
    return 1;
  }

  const auto states = assigned_runtime.upstream_states();
  return states.size() == 1 && !capabilities.tools.enabled &&
                 states.front().persistent_session_pool_size == 2 &&
                 states.front().initialized_persistent_sessions == 0 &&
                 states.front().busy_persistent_sessions == 0 &&
                 observed_events > 0
             ? 0
             : 1;
}
