// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/gateway/runtime.hpp>

#include <utility>

int main() {
  mcp::gateway::GatewayConfig config;
  mcp::gateway::GatewayRuntime runtime(std::move(config));
  return runtime.router().config().name == "cxxmcp-gateway" ? 0 : 1;
}
