// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/gateway/runtime.hpp>
#include <cxxmcp/gateway/config.hpp>

#include <utility>

int main() {
  mcp::gateway::GatewayConfig config;
  mcp::gateway::GatewayRuntime runtime(std::move(config));
  return runtime.upstream_states().empty() ? 0 : 1;
}
