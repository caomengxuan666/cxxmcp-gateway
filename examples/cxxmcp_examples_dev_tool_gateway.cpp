// Copyright (c) 2025 [caomengxuan666]

#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cxxmcp/gateway/runtime.hpp"
#include "cxxmcp/peer.hpp"
#include "cxxmcp/service.hpp"

#ifndef CXXMCP_GATEWAY_EXAMPLE_WORKSPACE_SERVER
#define CXXMCP_GATEWAY_EXAMPLE_WORKSPACE_SERVER ""
#endif

#ifndef CXXMCP_GATEWAY_EXAMPLE_GIT_SERVER
#define CXXMCP_GATEWAY_EXAMPLE_GIT_SERVER ""
#endif

#ifndef CXXMCP_GATEWAY_EXAMPLE_CMAKE_CTEST_SERVER
#define CXXMCP_GATEWAY_EXAMPLE_CMAKE_CTEST_SERVER ""
#endif

#ifndef CXXMCP_GATEWAY_EXAMPLE_LOG_TRIAGE_SERVER
#define CXXMCP_GATEWAY_EXAMPLE_LOG_TRIAGE_SERVER ""
#endif

#ifndef CXXMCP_GATEWAY_EXAMPLE_DEFAULT_WORKSPACE_ROOT
#define CXXMCP_GATEWAY_EXAMPLE_DEFAULT_WORKSPACE_ROOT "."
#endif

namespace {

void print_usage(std::ostream& out) {
  out << "Usage:\n"
      << "  cxxmcp-gateway-cxxmcp-examples-dev-tool-gateway"
      << " [--stdio] [--host <host>] [--port <port>] [--path <path>]\n"
      << "      [--workspace-root <path>] [--persistent] [--prewarm]\n\n"
      << "Starts one gateway endpoint over cxxmcp-examples upstreams:\n"
      << "  workspace -> cxxmcp_workspace_server\n"
      << "  git       -> cxxmcp_git_server\n"
      << "  cmake     -> cxxmcp_cmake_ctest_server\n"
      << "  logs      -> cxxmcp_log_triage_server\n";
}

bool parse_port(std::string_view text, std::uint16_t& port) {
  unsigned value = 0;
  const auto* begin = text.data();
  const auto* end = text.data() + text.size();
  const auto parsed = std::from_chars(begin, end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end || value == 0 ||
      value > 65535) {
    return false;
  }
  port = static_cast<std::uint16_t>(value);
  return true;
}

void print_error(std::string_view context, const mcp::core::Error& error) {
  std::cerr << context << ": " << error.message;
  if (!error.detail.empty()) {
    std::cerr << ": " << error.detail;
  }
  if (!error.category.empty()) {
    std::cerr << " [" << error.category << "]";
  }
  std::cerr << "\n";
}

const char* status_name(mcp::gateway::UpstreamRuntimeStatus status) {
  switch (status) {
    case mcp::gateway::UpstreamRuntimeStatus::configured:
      return "configured";
    case mcp::gateway::UpstreamRuntimeStatus::connecting:
      return "connecting";
    case mcp::gateway::UpstreamRuntimeStatus::initialized:
      return "initialized";
    case mcp::gateway::UpstreamRuntimeStatus::healthy:
      return "healthy";
    case mcp::gateway::UpstreamRuntimeStatus::degraded:
      return "degraded";
    case mcp::gateway::UpstreamRuntimeStatus::stopping:
      return "stopping";
    case mcp::gateway::UpstreamRuntimeStatus::stopped:
      return "stopped";
  }
  return "unknown";
}

mcp::gateway::UpstreamServer make_stdio_upstream(std::string id,
                                                 std::string command) {
  mcp::gateway::UpstreamServer upstream;
  upstream.id = std::move(id);
  upstream.transport = mcp::gateway::UpstreamTransportKind::process_stdio;
  upstream.process_stdio.command = std::move(command);
  upstream.process_stdio.timeout = std::chrono::seconds{30};
  return upstream;
}

bool require_configured_path(std::string_view name, std::string_view path) {
  if (!path.empty()) {
    return true;
  }
  std::cerr << name << " executable path was not configured\n";
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  if (!require_configured_path("workspace",
                               CXXMCP_GATEWAY_EXAMPLE_WORKSPACE_SERVER) ||
      !require_configured_path("git", CXXMCP_GATEWAY_EXAMPLE_GIT_SERVER) ||
      !require_configured_path("cmake",
                               CXXMCP_GATEWAY_EXAMPLE_CMAKE_CTEST_SERVER) ||
      !require_configured_path("logs",
                               CXXMCP_GATEWAY_EXAMPLE_LOG_TRIAGE_SERVER)) {
    return 1;
  }

  std::vector<std::string_view> args;
  args.reserve(static_cast<std::size_t>(argc));
  for (int i = 1; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }

  if (!args.empty() && (args[0] == "--help" || args[0] == "-h")) {
    print_usage(std::cout);
    return 0;
  }

  bool prewarm = false;
  bool stdio = false;
  std::string workspace_root = CXXMCP_GATEWAY_EXAMPLE_DEFAULT_WORKSPACE_ROOT;
  mcp::gateway::HttpEndpoint endpoint;
  endpoint.port = 39931;
  mcp::gateway::GatewayRuntimeOptions options;
  options.observer = [](const mcp::gateway::GatewayRuntimeEvent& event) {
    if (event.kind !=
        mcp::gateway::GatewayRuntimeEventKind::upstream_status_changed) {
      return;
    }
    std::cerr << "upstream " << event.upstream_id << " -> "
              << status_name(event.upstream_status) << "\n";
    if (event.error.has_value()) {
      print_error("upstream error", *event.error);
    }
  };

  for (std::size_t i = 0; i < args.size(); ++i) {
    const auto arg = args[i];
    if (arg == "--host" && i + 1 < args.size()) {
      endpoint.host = std::string(args[++i]);
      continue;
    }
    if (arg == "--path" && i + 1 < args.size()) {
      endpoint.path = std::string(args[++i]);
      continue;
    }
    if (arg == "--port" && i + 1 < args.size()) {
      if (!parse_port(args[++i], endpoint.port)) {
        std::cerr << "invalid --port value\n";
        return 2;
      }
      continue;
    }
    if (arg == "--workspace-root" && i + 1 < args.size()) {
      workspace_root = std::string(args[++i]);
      continue;
    }
    if (arg == "--persistent") {
      options.upstream_session_mode =
          mcp::gateway::UpstreamSessionMode::persistent;
      continue;
    }
    if (arg == "--prewarm") {
      prewarm = true;
      continue;
    }
    if (arg == "--stdio") {
      stdio = true;
      continue;
    }
    std::cerr << "unknown or incomplete option: " << arg << "\n";
    print_usage(std::cerr);
    return 2;
  }

  mcp::gateway::GatewayConfig config;
  config.name = "cxxmcp-examples-dev-tool-gateway";
  config.version = "0.1.0";

  auto workspace = make_stdio_upstream(
      "workspace", CXXMCP_GATEWAY_EXAMPLE_WORKSPACE_SERVER);
  workspace.process_stdio.args.push_back(std::move(workspace_root));
  config.upstreams.push_back(std::move(workspace));
  config.upstreams.push_back(
      make_stdio_upstream("git", CXXMCP_GATEWAY_EXAMPLE_GIT_SERVER));
  config.upstreams.push_back(
      make_stdio_upstream("cmake", CXXMCP_GATEWAY_EXAMPLE_CMAKE_CTEST_SERVER));
  config.upstreams.push_back(
      make_stdio_upstream("logs", CXXMCP_GATEWAY_EXAMPLE_LOG_TRIAGE_SERVER));

  auto valid = mcp::gateway::validate_gateway_config(config);
  if (!valid) {
    print_error("invalid gateway config", valid.error());
    return 2;
  }

  mcp::gateway::GatewayRuntime runtime(std::move(config), std::move(options));
  if (prewarm) {
    auto refreshed = runtime.refresh_upstream_capabilities();
    if (!refreshed) {
      print_error("capability refresh failed", refreshed.error());
      return 1;
    }
  }

  if (stdio) {
    auto peer =
        mcp::ServerPeer::builder()
            .name("cxxmcp-examples-dev-tool-gateway")
            .version("0.1.0")
            .capabilities(runtime.server_capabilities())
            .stdio()
            .raw_request([&runtime](
                             const mcp::protocol::JsonRpcRequest& request) {
              return runtime.handle_request(request);
            })
            .on_raw_notification(
                [&runtime](const mcp::protocol::JsonRpcNotification&
                               notification,
                           const mcp::server::SessionContext&) {
                  return runtime.handle_notification(notification);
                })
            .build();
    if (!peer) {
      print_error("stdio gateway build failed", peer.error());
      return 1;
    }
    auto running = mcp::serve(std::move(*peer));
    if (!running) {
      print_error("stdio gateway start failed", running.error());
      return 1;
    }
    auto waited = running->wait();
    if (!waited) {
      print_error("stdio gateway wait failed", waited.error());
      return 1;
    }
    return 0;
  }

  std::cout << "cxxmcp-examples dev-tool gateway listening on http://"
            << endpoint.host << ":" << endpoint.port << endpoint.path
            << std::endl
            << "exposed upstream prefixes: workspace.*, git.*, cmake.*, logs.*"
            << std::endl;

  auto started = runtime.start_http(std::move(endpoint));
  if (!started) {
    print_error("gateway start failed", started.error());
    return 1;
  }

  auto waited = runtime.wait();
  if (!waited) {
    print_error("gateway wait failed", waited.error());
    return 1;
  }
  return 0;
}
