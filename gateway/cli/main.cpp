// Copyright (c) 2025 [caomengxuan666]

#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
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
      << "  cxxmcp-gateway serve";
#if defined(CXXMCP_GATEWAY_HAS_CONFIG_IO)
  out << " [--config <file>]";
#endif
  out << " [--host <host>]\n"
      << "      [--port <port>] [--path <path>]\n"
      << "      [--session-mode <per-call|persistent>]\n"
      << "      [--session-pool-size <n>]\n"
      << "      [--session-acquire-timeout-ms <ms>] [--prewarm]\n"
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

bool parse_nonnegative_milliseconds(std::string_view text,
                                    std::chrono::milliseconds& value) {
  std::int64_t parsed_value = 0;
  const auto* begin = text.data();
  const auto* end = text.data() + text.size();
  const auto parsed = std::from_chars(begin, end, parsed_value);
  if (parsed.ec != std::errc{} || parsed.ptr != end || parsed_value < 0) {
    return false;
  }
  value = std::chrono::milliseconds{parsed_value};
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

bool parse_session_mode(std::string_view text,
                        mcp::gateway::UpstreamSessionMode& mode) {
  if (text == "per-call" || text == "per_call") {
    mode = mcp::gateway::UpstreamSessionMode::per_call;
    return true;
  }
  if (text == "persistent") {
    mode = mcp::gateway::UpstreamSessionMode::persistent;
    return true;
  }
  return false;
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
  mcp::gateway::GatewayRuntimeConfig runtime_config;
  std::optional<mcp::gateway::UpstreamSessionMode> session_mode_override;
  std::optional<std::size_t> session_pool_size_override;
  std::optional<std::chrono::milliseconds> session_acquire_timeout_override;
  bool prewarm_flag = false;
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
    if (arg == "--session-mode" && i + 1 < args.size()) {
      mcp::gateway::UpstreamSessionMode mode =
          mcp::gateway::UpstreamSessionMode::per_call;
      if (!parse_session_mode(args[++i], mode)) {
        std::cerr << "invalid --session-mode value\n";
        return 2;
      }
      session_mode_override = mode;
      continue;
    }
    if (arg == "--session-pool-size" && i + 1 < args.size()) {
      std::size_t pool_size = 1;
      if (!parse_positive_size(args[++i], pool_size)) {
        std::cerr << "invalid --session-pool-size value\n";
        return 2;
      }
      session_pool_size_override = pool_size;
      continue;
    }
    if (arg == "--session-acquire-timeout-ms" && i + 1 < args.size()) {
      std::chrono::milliseconds timeout{0};
      if (!parse_nonnegative_milliseconds(args[++i], timeout)) {
        std::cerr << "invalid --session-acquire-timeout-ms value\n";
        return 2;
      }
      session_acquire_timeout_override = timeout;
      continue;
    }
    if (arg == "--prewarm") {
      prewarm_flag = true;
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
    auto loaded =
        mcp::gateway::load_gateway_config_document_file(*config_file);
    if (!loaded) {
      std::cerr << "failed to load config: " << loaded.error().message;
      if (!loaded.error().detail.empty()) {
        std::cerr << ": " << loaded.error().detail;
      }
      std::cerr << "\n";
      return 2;
    }
    runtime_config = loaded->runtime;
    auto merged = mcp::gateway::merge_gateway_config_upstreams(
        std::move(loaded->config), std::move(config));
    if (!merged) {
      std::cerr << "failed to merge config: " << merged.error().message;
      if (!merged.error().detail.empty()) {
        std::cerr << ": " << merged.error().detail;
      }
      std::cerr << "\n";
      return 2;
    }
    config = std::move(*merged);
  }
#endif

  if (session_mode_override.has_value()) {
    runtime_config.upstream_session_mode = *session_mode_override;
  }
  if (session_pool_size_override.has_value()) {
    runtime_config.persistent_session_pool_size =
        *session_pool_size_override;
  }
  if (session_acquire_timeout_override.has_value()) {
    runtime_config.persistent_session_acquire_timeout =
        *session_acquire_timeout_override;
  }
  if (prewarm_flag) {
    runtime_config.prewarm_capabilities = true;
  }

  if (config.upstreams.empty()) {
    std::cerr << "at least one upstream is required\n";
    return 2;
  }

  mcp::gateway::GatewayRuntimeOptions runtime_options;
  runtime_options.upstream_session_mode = runtime_config.upstream_session_mode;
  runtime_options.persistent_session_pool_size =
      runtime_config.persistent_session_pool_size;
  runtime_options.persistent_session_acquire_timeout =
      runtime_config.persistent_session_acquire_timeout;
  mcp::gateway::GatewayRuntime runtime(std::move(config),
                                       std::move(runtime_options));
  if (runtime_config.prewarm_capabilities) {
    auto refreshed = runtime.refresh_upstream_capabilities();
    if (!refreshed) {
      std::cerr << "failed to prewarm upstream capabilities: "
                << refreshed.error().message;
      if (!refreshed.error().detail.empty()) {
        std::cerr << ": " << refreshed.error().detail;
      }
      std::cerr << "\n";
      return 1;
    }
  }
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
