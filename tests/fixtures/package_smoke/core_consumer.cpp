// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/gateway.hpp>

#include <utility>

int main() {
  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer base_upstream;
  base_upstream.id = "local";
  base_upstream.process_stdio.command = "fixture-server";
  config.upstreams.push_back(std::move(base_upstream));

  auto valid = mcp::gateway::validate_gateway_config(config);
  if (!valid) {
    return 1;
  }

  mcp::gateway::GatewayConfig appended;
  mcp::gateway::UpstreamServer appended_upstream;
  appended_upstream.id = "remote";
  appended_upstream.transport =
      mcp::gateway::UpstreamTransportKind::streamable_http;
  appended_upstream.streamable_http.uri = "http://127.0.0.1:3000/mcp";
  appended.upstreams.push_back(std::move(appended_upstream));

  auto merged = mcp::gateway::merge_gateway_config_upstreams(
      std::move(config), std::move(appended));
  if (!merged || merged->upstreams.size() != 2 ||
      merged->upstreams[0].id != "local" ||
      merged->upstreams[1].id != "remote") {
    return 2;
  }

  const auto resolved =
      mcp::gateway::GatewayRouter::resolve_tool_name("local.echo");
  if (!resolved || resolved->upstream_id != "local" ||
      resolved->upstream_tool_name != "echo") {
    return 3;
  }
  return 0;
}
