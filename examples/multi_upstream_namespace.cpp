// Copyright (c) 2025 [caomengxuan666]

#include <iostream>
#include <string_view>

#include "cxxmcp/gateway.hpp"

namespace {

void print_error(std::string_view context, const mcp::core::Error& error) {
  std::cerr << context << ": " << error.message;
  if (!error.detail.empty()) {
    std::cerr << ": " << error.detail;
  }
  std::cerr << "\n";
}

mcp::gateway::UpstreamServer make_stdio_upstream(std::string_view id,
                                                 std::string_view command) {
  mcp::gateway::UpstreamServer upstream;
  upstream.id = std::string(id);
  upstream.transport = mcp::gateway::UpstreamTransportKind::process_stdio;
  upstream.process_stdio.command = std::string(command);
  return upstream;
}

}  // namespace

int main() {
  mcp::gateway::GatewayConfig config;
  config.upstreams.push_back(make_stdio_upstream("fs", "filesystem-server"));
  config.upstreams.push_back(make_stdio_upstream("git", "git-mcp-server"));

  auto valid = mcp::gateway::validate_gateway_config(config);
  if (!valid) {
    print_error("invalid gateway config", valid.error());
    return 1;
  }

  const auto fs_tool =
      mcp::gateway::GatewayRouter::expose_tool_name("fs", "read_file");
  const auto git_tool =
      mcp::gateway::GatewayRouter::expose_tool_name("git", "status");
  const auto fs_resource = mcp::gateway::GatewayRouter::expose_resource_uri(
      "fs", "file:///workspace/README.md");

  mcp::gateway::GatewayRouter router(std::move(config));

  auto fs_route = router.resolve_tool_route(fs_tool);
  if (!fs_route) {
    print_error("failed to resolve fs tool", fs_route.error());
    return 1;
  }
  auto git_route = router.resolve_tool_route(git_tool);
  if (!git_route) {
    print_error("failed to resolve git tool", git_route.error());
    return 1;
  }
  auto resource_route = router.resolve_resource_route(fs_resource);
  if (!resource_route) {
    print_error("failed to resolve fs resource", resource_route.error());
    return 1;
  }

  std::cout << "exposed tool: " << fs_tool << " -> "
            << fs_route->upstream->id << "/" << fs_route->upstream_tool_name
            << "\n";
  std::cout << "exposed tool: " << git_tool << " -> "
            << git_route->upstream->id << "/" << git_route->upstream_tool_name
            << "\n";
  std::cout << "exposed resource: " << fs_resource << " -> "
            << resource_route->upstream->id << "/"
            << resource_route->upstream_uri << "\n";
  return 0;
}
