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

namespace {

void print_usage(std::ostream& out) {
  out << "Usage:\n"
      << "  cxxmcp-gateway-embedded-runtime-example [--host <host>]\n"
      << "      [--port <port>] [--path <path>] [--persistent]\n"
      << "      [--session-pool-size <n>] [--prewarm]\n"
      << "      --http <id=uri> [--http <id=uri> ...]\n"
      << "      --stdio <id=command> [--stdio <id=command> ...]\n";
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

bool parse_positive_size(std::string_view text, std::size_t& value) {
  std::size_t parsed_value = 0;
  const auto* begin = text.data();
  const auto* end = text.data() + text.size();
  const auto parsed = std::from_chars(begin, end, parsed_value);
  if (parsed.ec != std::errc{} || parsed.ptr != end || parsed_value == 0) {
    return false;
  }
  value = parsed_value;
  return true;
}

bool split_assignment(std::string_view text, std::string& key,
                      std::string& value) {
  const auto equals = text.find('=');
  if (equals == std::string_view::npos || equals == 0 ||
      equals + 1 >= text.size()) {
    return false;
  }
  key = std::string(text.substr(0, equals));
  value = std::string(text.substr(equals + 1));
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

}  // namespace

int main(int argc, char** argv) {
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
  mcp::gateway::HttpEndpoint endpoint;
  mcp::gateway::GatewayConfig config;
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
    if (arg == "--persistent") {
      options.upstream_session_mode =
          mcp::gateway::UpstreamSessionMode::persistent;
      continue;
    }
    if (arg == "--session-pool-size" && i + 1 < args.size()) {
      std::size_t pool_size = 1;
      if (!parse_positive_size(args[++i], pool_size)) {
        std::cerr << "invalid --session-pool-size value\n";
        return 2;
      }
      options.persistent_session_pool_size = pool_size;
      continue;
    }
    if (arg == "--prewarm") {
      prewarm = true;
      continue;
    }
    if (arg == "--http" && i + 1 < args.size()) {
      std::string id;
      std::string uri;
      if (!split_assignment(args[++i], id, uri)) {
        std::cerr << "--http expects <id=uri>\n";
        return 2;
      }
      mcp::gateway::UpstreamServer upstream;
      upstream.id = std::move(id);
      upstream.transport = mcp::gateway::UpstreamTransportKind::streamable_http;
      upstream.streamable_http.uri = std::move(uri);
      upstream.streamable_http.timeout = std::chrono::seconds{30};
      config.upstreams.push_back(std::move(upstream));
      continue;
    }
    if (arg == "--stdio" && i + 1 < args.size()) {
      std::string id;
      std::string command;
      if (!split_assignment(args[++i], id, command)) {
        std::cerr << "--stdio expects <id=command>\n";
        return 2;
      }
      mcp::gateway::UpstreamServer upstream;
      upstream.id = std::move(id);
      upstream.transport = mcp::gateway::UpstreamTransportKind::process_stdio;
      upstream.process_stdio.command = std::move(command);
      upstream.process_stdio.timeout = std::chrono::seconds{30};
      config.upstreams.push_back(std::move(upstream));
      continue;
    }

    std::cerr << "unknown or incomplete option: " << arg << "\n";
    print_usage(std::cerr);
    return 2;
  }

  if (config.upstreams.empty()) {
    std::cerr << "at least one upstream is required\n";
    print_usage(std::cerr);
    return 2;
  }

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
