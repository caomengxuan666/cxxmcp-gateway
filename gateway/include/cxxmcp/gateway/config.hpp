// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cxxmcp/core/result.hpp"

namespace mcp::gateway {

enum class UpstreamTransportKind {
  process_stdio,
  streamable_http,
};

struct ProcessStdioUpstream {
  std::string command;
  std::vector<std::string> args;
  std::string cwd;
  std::unordered_map<std::string, std::string> env;
};

struct HttpUpstream {
  std::string uri;
  std::unordered_map<std::string, std::string> headers;
  std::chrono::milliseconds timeout{30000};
};

struct UpstreamServer {
  std::string id;
  std::string display_name;
  bool enabled = true;
  UpstreamTransportKind transport = UpstreamTransportKind::process_stdio;
  ProcessStdioUpstream process_stdio;
  HttpUpstream streamable_http;
};

struct GatewayConfig {
  std::string name = "cxxmcp-gateway";
  std::string version = "0.1.0";
  std::vector<UpstreamServer> upstreams;
};

struct HttpEndpoint {
  std::string host = "127.0.0.1";
  std::uint16_t port = 3000;
  std::string path = "/mcp";
};

core::Result<core::Unit> validate_upstream_id(std::string_view upstream_id);
core::Result<core::Unit> validate_gateway_config(
    const GatewayConfig& config);

}  // namespace mcp::gateway
