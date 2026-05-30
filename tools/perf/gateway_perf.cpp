// Copyright (c) 2025 [caomengxuan666]

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cxxmcp/gateway/runtime.hpp"
#include "cxxmcp/peer.hpp"
#include "cxxmcp/protocol/tool.hpp"
#include "cxxmcp/service.hpp"

namespace {

using Json = mcp::protocol::Json;
using ToolResult = mcp::protocol::ToolResult;

struct Options {
  int iterations = 20;
  std::uint16_t http_port = 39970;
};

struct Measurement {
  std::string transport;
  std::string operation;
  int iterations = 0;
  long long median_us = 0;
  long long p95_us = 0;
};

void print_usage(std::ostream& out) {
  out << "Usage: cxxmcp-gateway-perf [--iterations <count>] "
         "[--http-port <port>]\n";
}

int parse_positive_int(std::string_view text, std::string_view name) {
  int value = 0;
  for (const char ch : text) {
    if (ch < '0' || ch > '9') {
      throw std::runtime_error(std::string(name) + " must be a positive integer");
    }
    value = (value * 10) + (ch - '0');
  }
  if (value <= 0) {
    throw std::runtime_error(std::string(name) + " must be a positive integer");
  }
  return value;
}

Options parse_options(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    if (arg == "--help" || arg == "-h") {
      print_usage(std::cout);
      std::exit(0);
    }
    if (arg == "--iterations" && i + 1 < argc) {
      options.iterations = parse_positive_int(argv[++i], "--iterations");
      continue;
    }
    if (arg == "--http-port" && i + 1 < argc) {
      const auto port = parse_positive_int(argv[++i], "--http-port");
      if (port > 65535) {
        throw std::runtime_error("--http-port must be at most 65535");
      }
      options.http_port = static_cast<std::uint16_t>(port);
      continue;
    }
    throw std::runtime_error("unknown or incomplete option: " +
                             std::string(arg));
  }
  return options;
}

template <class Fn>
Measurement measure(std::string transport, std::string operation, int iterations,
                    Fn&& fn) {
  std::vector<long long> samples;
  samples.reserve(static_cast<std::size_t>(iterations));
  for (int i = 0; i < iterations; ++i) {
    const auto started = std::chrono::steady_clock::now();
    fn();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    samples.push_back(
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
  }
  std::sort(samples.begin(), samples.end());
  const auto median_index = samples.size() / 2;
  const auto p95_index = std::min(
      samples.size() - 1, ((samples.size() * 95) + 99) / 100 - 1);
  return Measurement{.transport = std::move(transport),
                     .operation = std::move(operation),
                     .iterations = iterations,
                     .median_us = samples[median_index],
                     .p95_us = samples[p95_index]};
}

mcp::gateway::GatewayConfig make_stdio_config() {
  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "stdio";
  upstream.transport = mcp::gateway::UpstreamTransportKind::process_stdio;
  upstream.process_stdio.command = CXXMCP_GATEWAY_PERF_STDIO_FIXTURE;
  config.upstreams.push_back(std::move(upstream));
  return config;
}

mcp::gateway::GatewayConfig make_http_config(std::uint16_t port) {
  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "http";
  upstream.transport = mcp::gateway::UpstreamTransportKind::streamable_http;
  upstream.streamable_http.uri =
      "http://127.0.0.1:" + std::to_string(port) + "/mcp";
  config.upstreams.push_back(std::move(upstream));
  return config;
}

void require_result(bool ok, std::string_view message) {
  if (!ok) {
    throw std::runtime_error(std::string(message));
  }
}

void print_csv_header() {
  std::cout << "transport,operation,iterations,median_us,p95_us\n";
}

void print_csv_row(const Measurement& measurement) {
  std::cout << measurement.transport << ',' << measurement.operation << ','
            << measurement.iterations << ',' << measurement.median_us << ','
            << measurement.p95_us << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto options = parse_options(argc, argv);

    auto http_server =
        mcp::ServerPeer::builder()
            .name("cxxmcp-gateway-perf-http-fixture")
            .version("1.0.0")
            .streamable_http("127.0.0.1", options.http_port, "/mcp")
            .tool<Json, ToolResult>("echo", [](const Json& input) {
              return ToolResult::text(input.value("value", std::string{}));
            })
            .build();
    require_result(http_server.has_value(), "HTTP fixture should build");

    auto running_http = mcp::serve(std::move(*http_server));
    require_result(running_http.has_value(), "HTTP fixture should start");
    running_http->wait_until_ready();

    print_csv_header();

    {
      mcp::gateway::GatewayRuntime runtime(make_stdio_config());
      print_csv_row(measure("stdio", "tools/list", options.iterations, [&] {
        auto listed = runtime.list_tools();
        require_result(listed.has_value(), "stdio tools/list failed");
      }));
      print_csv_row(measure("stdio", "tools/call", options.iterations, [&] {
        auto called =
            runtime.call_tool("stdio.echo", Json{{"value", "perf"}});
        require_result(called.has_value(), "stdio tools/call failed");
      }));
    }

    {
      mcp::gateway::GatewayRuntime runtime(
          make_http_config(options.http_port));
      print_csv_row(measure("http", "tools/list", options.iterations, [&] {
        auto listed = runtime.list_tools();
        require_result(listed.has_value(), "http tools/list failed");
      }));
      print_csv_row(measure("http", "tools/call", options.iterations, [&] {
        auto called = runtime.call_tool("http.echo", Json{{"value", "perf"}});
        require_result(called.has_value(), "http tools/call failed");
      }));
    }

    const auto stopped = running_http->stop();
    require_result(stopped.has_value(), "HTTP fixture should stop");
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "cxxmcp-gateway-perf failed: " << ex.what() << "\n";
    return 1;
  }
}
