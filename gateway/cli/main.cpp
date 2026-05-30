// Copyright (c) 2025 [caomengxuan666]

#include <charconv>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cxxmcp/gateway/runtime.hpp"
#if defined(CXXMCP_GATEWAY_HAS_CONFIG_IO)
#include "cxxmcp/gateway/config_io.hpp"
#endif

namespace {

void print_usage(std::ostream& out) {
  out << "Usage:\n"
      << "  cxxmcp-gateway --help\n"
      << "  cxxmcp-gateway --version\n"
      << "  cxxmcp-gateway serve [--config <file>] --port <port>\n"
      << "      [--host <host>] [--path <path>]\n"
      << "      --upstream-http <id=url> [--upstream-http <id=url> ...]\n"
      << "      --upstream-stdio <id=command> [--upstream-stdio <id=command> ...]\n";
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

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string_view> args;
  args.reserve(static_cast<std::size_t>(argc));
  for (int i = 1; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }

  if (args.empty() || args[0] == "--help" || args[0] == "-h") {
    print_usage(std::cout);
    return 0;
  }
  if (args[0] == "--version" || args[0] == "-V") {
    std::cout << CXXMCP_GATEWAY_VERSION << "\n";
    return 0;
  }
  if (args[0] != "serve") {
    std::cerr << "unknown command: " << args[0] << "\n";
    print_usage(std::cerr);
    return 2;
  }

  mcp::gateway::HttpEndpoint endpoint;
  mcp::gateway::GatewayConfig config;
#if defined(CXXMCP_GATEWAY_HAS_CONFIG_IO)
  std::optional<std::string> config_file;
#endif

  for (std::size_t i = 1; i < args.size(); ++i) {
    const auto arg = args[i];
#if defined(CXXMCP_GATEWAY_HAS_CONFIG_IO)
    if (arg == "--config" && i + 1 < args.size()) {
      config_file = std::string(args[++i]);
      continue;
    }
#else
    if (arg == "--config") {
      std::cerr << "--config requires cxxmcp_gateway_config_io\n";
      return 2;
    }
#endif
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
    if (arg == "--upstream-http" && i + 1 < args.size()) {
      std::string id;
      std::string url;
      if (!split_assignment(args[++i], id, url)) {
        std::cerr << "--upstream-http expects <id=url>\n";
        return 2;
      }
      mcp::gateway::UpstreamServer upstream;
      upstream.id = std::move(id);
      upstream.transport = mcp::gateway::UpstreamTransportKind::streamable_http;
      upstream.streamable_http.uri = std::move(url);
      config.upstreams.push_back(std::move(upstream));
      continue;
    }
    if (arg == "--upstream-stdio" && i + 1 < args.size()) {
      std::string id;
      std::string command;
      if (!split_assignment(args[++i], id, command)) {
        std::cerr << "--upstream-stdio expects <id=command>\n";
        return 2;
      }
      mcp::gateway::UpstreamServer upstream;
      upstream.id = std::move(id);
      upstream.transport = mcp::gateway::UpstreamTransportKind::process_stdio;
      upstream.process_stdio.command = std::move(command);
      config.upstreams.push_back(std::move(upstream));
      continue;
    }

    std::cerr << "unknown or incomplete option: " << arg << "\n";
    return 2;
  }

#if defined(CXXMCP_GATEWAY_HAS_CONFIG_IO)
  if (config_file.has_value()) {
    auto loaded = mcp::gateway::load_gateway_config_file(*config_file);
    if (!loaded) {
      std::cerr << "failed to load config: " << loaded.error().message;
      if (!loaded.error().detail.empty()) {
        std::cerr << ": " << loaded.error().detail;
      }
      std::cerr << "\n";
      return 2;
    }
    loaded->upstreams.insert(loaded->upstreams.end(),
                             std::make_move_iterator(config.upstreams.begin()),
                             std::make_move_iterator(config.upstreams.end()));
    config = std::move(*loaded);
  }
#endif

  if (config.upstreams.empty()) {
    std::cerr << "at least one upstream is required\n";
    return 2;
  }

  mcp::gateway::GatewayRuntime runtime(std::move(config));
  auto started = runtime.start_http(std::move(endpoint));
  if (!started) {
    std::cerr << "failed to start gateway: " << started.error().message;
    if (!started.error().detail.empty()) {
      std::cerr << ": " << started.error().detail;
    }
    std::cerr << "\n";
    return 1;
  }

  const auto waited = runtime.wait();
  if (!waited) {
    std::cerr << "gateway stopped with error: " << waited.error().message;
    if (!waited.error().detail.empty()) {
      std::cerr << ": " << waited.error().detail;
    }
    std::cerr << "\n";
    return 1;
  }
  return 0;
}
