// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/gateway.hpp>

int main() {
  mcp::gateway::GatewayConfig config;
  auto valid = mcp::gateway::validate_gateway_config(config);
  if (!valid) {
    return 1;
  }

  const auto resolved =
      mcp::gateway::GatewayRouter::resolve_tool_name("local.echo");
  if (!resolved || resolved->upstream_id != "local" ||
      resolved->upstream_tool_name != "echo") {
    return 2;
  }
  return 0;
}
