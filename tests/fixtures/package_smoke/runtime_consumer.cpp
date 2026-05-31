// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/gateway/runtime.hpp>
#include <cxxmcp/gateway/config.hpp>

#include <chrono>
#include <cstddef>
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
  auto capabilities = runtime.server_capabilities();
  auto cleared = runtime.clear_cached_catalogs();
  if (!cleared) {
    return 1;
  }
  auto refreshed = runtime.refresh_upstream_capabilities();
  if (!refreshed) {
    return 1;
  }
  auto stopped = runtime.stop();
  if (!stopped) {
    return 1;
  }

  const auto states = runtime.upstream_states();
  return states.size() == 1 && !capabilities.tools.enabled &&
                 states.front().persistent_session_pool_size == 2 &&
                 states.front().initialized_persistent_sessions == 0 &&
                 states.front().busy_persistent_sessions == 0 &&
                 observed_events > 0
             ? 0
             : 1;
}
