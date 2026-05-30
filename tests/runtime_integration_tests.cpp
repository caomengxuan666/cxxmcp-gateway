// Copyright (c) 2025 [caomengxuan666]

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "cxxmcp/gateway/runtime.hpp"
#include "cxxmcp/peer.hpp"
#include "cxxmcp/protocol/tool.hpp"
#include "cxxmcp/service.hpp"

namespace {

using Json = mcp::protocol::Json;
using ToolDefinition = mcp::protocol::ToolDefinition;
using ToolResult = mcp::protocol::ToolResult;
using UpstreamRuntimeState = mcp::gateway::UpstreamRuntimeState;
using UpstreamRuntimeStatus = mcp::gateway::UpstreamRuntimeStatus;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

bool has_tool(const std::vector<ToolDefinition>& tools,
              std::string_view name) {
  return std::any_of(tools.begin(), tools.end(), [&](const auto& tool) {
    return tool.name == name;
  });
}

UpstreamRuntimeState require_upstream_state(
    const std::vector<UpstreamRuntimeState>& states, std::string_view id) {
  const auto it = std::find_if(states.begin(), states.end(), [&](const auto& s) {
    return s.upstream_id == id;
  });
  if (it == states.end()) {
    throw std::runtime_error("missing upstream runtime state for " +
                             std::string(id));
  }
  return *it;
}

void require_status(const UpstreamRuntimeState& state,
                    UpstreamRuntimeStatus expected,
                    std::string_view message) {
  if (state.status != expected) {
    throw std::runtime_error(std::string(message) + ": got status " +
                             std::to_string(static_cast<int>(state.status)));
  }
}

void require_text_result(const ToolResult& result, std::string_view text) {
  require(!result.is_error_result(), "tool result should not be an error");
  require(!result.content.empty(), "tool result should include content");
  require(result.content.front().type == "text", "tool result should be text");
  require(result.content.front().text == text, "tool result text mismatch");
}

void require_gateway_upstream_error(const mcp::core::Error& error,
                                    std::string_view upstream_id) {
  require(error.detail.find("upstream '" + std::string(upstream_id) + "'") !=
              std::string::npos,
          "upstream error should include gateway upstream context");
  require(error.category.rfind("gateway.upstream", 0) == 0,
          "upstream error should use gateway upstream category");
}

void require_gateway_upstream_timeout(const mcp::core::Error& error,
                                      std::string_view upstream_id) {
  require_gateway_upstream_error(error, upstream_id);
  if (error.category.find("timeout") == std::string::npos) {
    throw std::runtime_error("upstream timeout should preserve timeout "
                             "category, got: " +
                             error.category + " / " + error.message + " / " +
                             error.detail);
  }
}

void require_gateway_upstream_protocol(const mcp::core::Error& error,
                                       std::string_view upstream_id) {
  require_gateway_upstream_error(error, upstream_id);
  require(error.category == "gateway.upstream.protocol",
          "upstream protocol errors should use gateway protocol category");
}

mcp::gateway::GatewayConfig make_stdio_config() {
  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "stdio";
  upstream.transport = mcp::gateway::UpstreamTransportKind::process_stdio;
  upstream.process_stdio.command = CXXMCP_GATEWAY_STDIO_FIXTURE;
  config.upstreams.push_back(std::move(upstream));
  return config;
}

void test_disabled_upstream() {
  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "disabled";
  upstream.enabled = false;
  config.upstreams.push_back(std::move(upstream));

  mcp::gateway::GatewayRuntime runtime(std::move(config));
  auto capabilities = runtime.server_capabilities();
  require(!capabilities.tools.enabled,
          "disabled-only runtime should not advertise tools");
  require(!capabilities.resources.enabled,
          "runtime should not advertise resources");
  require(!capabilities.prompts.enabled,
          "runtime should not advertise prompts");
  require(!capabilities.tasks.has_value(),
          "runtime should not advertise tasks");

  auto tools = runtime.list_tools();
  require(tools.has_value(), "disabled upstream list should still succeed");
  require(tools->empty(), "disabled upstream should not expose tools");

  auto called = runtime.call_tool("disabled.echo", Json::object());
  require(!called.has_value(), "disabled upstream call should fail");
  require(called.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::ToolNotFound),
          "disabled upstream should map to tool-not-found");
}

void test_invalid_config_advertises_no_tools() {
  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "invalid";
  upstream.transport = mcp::gateway::UpstreamTransportKind::process_stdio;
  config.upstreams.push_back(std::move(upstream));

  mcp::gateway::GatewayRuntime runtime(std::move(config));
  auto capabilities = runtime.server_capabilities();
  require(!capabilities.tools.enabled,
          "invalid runtime config should not advertise tools");
  require(!capabilities.resources.enabled,
          "invalid runtime config should not advertise resources");
  require(!capabilities.prompts.enabled,
          "invalid runtime config should not advertise prompts");
  require(!capabilities.tasks.has_value(),
          "invalid runtime config should not advertise tasks");
}

void test_stdio_process_start_failure() {
  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "missing";
  upstream.transport = mcp::gateway::UpstreamTransportKind::process_stdio;
  upstream.process_stdio.command =
      "cxxmcp-gateway-definitely-missing-upstream-executable";
  config.upstreams.push_back(std::move(upstream));

  mcp::gateway::GatewayRuntime runtime(std::move(config));
  auto tools = runtime.list_tools();
  require(!tools.has_value(), "missing stdio process should fail tools/list");
  require_gateway_upstream_error(tools.error(), "missing");

  const auto& state = require_upstream_state(runtime.upstream_states(), "missing");
  require_status(state, UpstreamRuntimeStatus::degraded,
                 "missing stdio process should mark upstream degraded");
  require(state.last_error.has_value(),
          "degraded missing stdio upstream should record last error");
}

void test_tools_list_fail_fast_after_partial_success() {
  auto config = make_stdio_config();

  mcp::gateway::UpstreamServer missing;
  missing.id = "missing";
  missing.transport = mcp::gateway::UpstreamTransportKind::process_stdio;
  missing.process_stdio.command =
      "cxxmcp-gateway-definitely-missing-upstream-executable";
  config.upstreams.push_back(std::move(missing));

  mcp::gateway::GatewayRuntime runtime(std::move(config));
  auto tools = runtime.list_tools();
  require(!tools.has_value(),
          "tools/list should fail fast when any enabled upstream fails");
  require_gateway_upstream_error(tools.error(), "missing");

  const auto states = runtime.upstream_states();
  const auto stdio = require_upstream_state(states, "stdio");
  require_status(stdio, UpstreamRuntimeStatus::healthy,
                 "successful upstream should keep healthy state after "
                 "fail-fast tools/list");
  require(stdio.capabilities.has_value(),
          "successful upstream should retain initialized capabilities after "
          "fail-fast tools/list");

  const auto failed = require_upstream_state(states, "missing");
  require_status(failed, UpstreamRuntimeStatus::degraded,
                 "failing upstream should be degraded after fail-fast "
                 "tools/list");
  require(failed.last_error.has_value(),
          "failing upstream should retain last error after fail-fast "
          "tools/list");
}

void test_stdio_process_exit_before_initialize() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "exit";
  config.upstreams.front().process_stdio.args = {"--exit-immediately"};

  mcp::gateway::GatewayRuntime runtime(std::move(config));
  auto tools = runtime.list_tools();
  require(!tools.has_value(),
          "stdio process exit before initialize should fail tools/list");
  require_gateway_upstream_error(tools.error(), "exit");
}

void test_stdio_malformed_response_before_initialize() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "malformed";
  config.upstreams.front().process_stdio.args = {"--malformed-response"};

  mcp::gateway::GatewayRuntime runtime(std::move(config));
  auto tools = runtime.list_tools();
  require(!tools.has_value(),
          "stdio malformed response should fail tools/list");
  require_gateway_upstream_protocol(tools.error(), "malformed");

  const auto state =
      require_upstream_state(runtime.upstream_states(), "malformed");
  require_status(state, UpstreamRuntimeStatus::degraded,
                 "malformed stdio response should mark upstream degraded");
  require(state.last_error.has_value(),
          "malformed stdio response should record last error");
  require_gateway_upstream_protocol(*state.last_error, "malformed");
}

void test_stdio_upstream() {
  mcp::gateway::GatewayRuntime runtime(make_stdio_config());
  auto capabilities = runtime.server_capabilities();
  require(capabilities.tools.enabled,
          "runtime with enabled upstream should advertise tools");
  require(!capabilities.tools.list_changed,
          "runtime should not advertise tools/listChanged");
  require(!capabilities.resources.enabled,
          "runtime should not advertise resources");
  require(!capabilities.prompts.enabled,
          "runtime should not advertise prompts");
  require(!capabilities.tasks.has_value(),
          "runtime should not advertise tasks");

  const auto& initial =
      require_upstream_state(runtime.upstream_states(), "stdio");
  require_status(initial, UpstreamRuntimeStatus::configured,
                 "new runtime upstream should start configured");

  auto tools = runtime.list_tools();
  require(tools.has_value(), "stdio tools/list should succeed");
  require(has_tool(*tools, "stdio.echo"), "stdio tool should be exposed");
  const auto& listed = require_upstream_state(runtime.upstream_states(), "stdio");
  require_status(listed, UpstreamRuntimeStatus::healthy,
                 "successful tools/list should mark upstream healthy");
  require(listed.capabilities.has_value(),
          "successful initialize should record upstream capabilities");

  auto called =
      runtime.call_tool("stdio.echo", Json{{"value", "from-stdio"}});
  require(called.has_value(), "stdio tools/call should succeed");
  require_text_result(*called, "from-stdio");

  auto unknown = runtime.call_tool("missing.echo", Json::object());
  require(!unknown.has_value(), "unknown upstream should fail");

  auto missing_tool = runtime.call_tool("stdio.missing", Json::object());
  require(!missing_tool.has_value(), "unknown upstream tool should fail");
  require_gateway_upstream_error(missing_tool.error(), "stdio");

  auto upstream_error = runtime.call_tool("stdio.fail", Json::object());
  require(!upstream_error.has_value(), "upstream MCP error should fail");
  require(upstream_error.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::PermissionDenied),
          "upstream MCP error code should be preserved");
  require(upstream_error.error().message == "fixture denied",
          "upstream MCP error message should be preserved");
  require(upstream_error.error().detail.find("fixture detail") !=
              std::string::npos,
          "upstream MCP error detail should be preserved");
  require_gateway_upstream_error(upstream_error.error(), "stdio");
  const auto& failed = require_upstream_state(runtime.upstream_states(), "stdio");
  require_status(failed, UpstreamRuntimeStatus::degraded,
                 "upstream MCP error should mark upstream degraded");
  require(failed.last_error.has_value(),
          "degraded upstream should retain last error");

  auto stopped = runtime.stop();
  require(stopped.has_value(), "runtime stop should succeed without endpoint");
  const auto& stopped_state =
      require_upstream_state(runtime.upstream_states(), "stdio");
  require_status(stopped_state, UpstreamRuntimeStatus::stopped,
                 "runtime stop should mark upstream stopped");
}

void test_http_upstream() {
  constexpr auto kPort = 39947;
  const std::string uri = "http://127.0.0.1:" + std::to_string(kPort) + "/mcp";

  auto server = mcp::ServerPeer::builder()
                    .name("cxxmcp-gateway-http-fixture")
                    .version("1.0.0")
                    .streamable_http("127.0.0.1", kPort, "/mcp")
                    .tool<Json, ToolResult>("echo", [](const Json& input) {
                      return ToolResult::text(
                          input.value("value", std::string{}));
                    })
                    .tool<Json, ToolResult>("slow", [](const Json& input) {
                      std::this_thread::sleep_for(
                          std::chrono::milliseconds{
                              input.value("sleepMs", 300)});
                      return ToolResult::text("slow-done");
                    })
                    .build();
  require(server.has_value(), "http fixture server should build");

  auto running = mcp::serve(std::move(*server));
  require(running.has_value(), "http fixture server should start");
  running->wait_until_ready();

  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "http";
  upstream.transport = mcp::gateway::UpstreamTransportKind::streamable_http;
  upstream.streamable_http.uri = uri;
  config.upstreams.push_back(std::move(upstream));
  mcp::gateway::GatewayRuntime runtime(std::move(config));

  auto tools = runtime.list_tools();
  require(tools.has_value(), "http tools/list should succeed");
  require(has_tool(*tools, "http.echo"), "http tool should be exposed");

  auto called = runtime.call_tool("http.echo", Json{{"value", "from-http"}});
  require(called.has_value(), "http tools/call should succeed");
  require_text_result(*called, "from-http");

  auto multi_config = make_stdio_config();
  mcp::gateway::UpstreamServer second;
  second.id = "http";
  second.transport = mcp::gateway::UpstreamTransportKind::streamable_http;
  second.streamable_http.uri = uri;
  multi_config.upstreams.push_back(std::move(second));
  mcp::gateway::GatewayRuntime multi_runtime(std::move(multi_config));
  auto multi_tools = multi_runtime.list_tools();
  require(multi_tools.has_value(), "multi-upstream tools/list should succeed");
  require(has_tool(*multi_tools, "stdio.echo"),
          "multi-upstream list should include stdio tool");
  require(has_tool(*multi_tools, "http.echo"),
          "multi-upstream list should include http tool");

  const auto stopped = running->stop();
  require(stopped.has_value(), "http fixture server should stop");
}

void test_http_timeout() {
  constexpr auto kPort = 39949;
  const std::string uri = "http://127.0.0.1:" + std::to_string(kPort) + "/mcp";

  auto server = mcp::ServerPeer::builder()
                    .name("cxxmcp-gateway-http-timeout-fixture")
                    .version("1.0.0")
                    .streamable_http("127.0.0.1", kPort, "/mcp")
                    .tool<Json, ToolResult>("slow", [](const Json& input) {
                      std::this_thread::sleep_for(
                          std::chrono::milliseconds{
                              input.value("sleepMs", 500)});
                      return ToolResult::text("slow-done");
                    })
                    .build();
  require(server.has_value(), "http timeout fixture should build");

  auto running = mcp::serve(std::move(*server));
  require(running.has_value(), "http timeout fixture should start");
  running->wait_until_ready();

  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "slow";
  upstream.transport = mcp::gateway::UpstreamTransportKind::streamable_http;
  upstream.streamable_http.uri = uri;
  upstream.streamable_http.timeout = std::chrono::milliseconds{50};
  config.upstreams.push_back(std::move(upstream));

  mcp::gateway::GatewayRuntime runtime(std::move(config));
  auto called =
      runtime.call_tool("slow.slow", Json{{"sleepMs", 500}});
  require(!called.has_value(), "slow http upstream should time out");
  require_gateway_upstream_timeout(called.error(), "slow");

  std::this_thread::sleep_for(std::chrono::milliseconds{600});
  const auto stopped = running->stop();
  require(stopped.has_value(), "http timeout fixture should stop");
}

void test_concurrent_http_calls_update_active_state() {
  constexpr auto kPort = 39951;
  const std::string uri = "http://127.0.0.1:" + std::to_string(kPort) + "/mcp";

  auto server = mcp::ServerPeer::builder()
                    .name("cxxmcp-gateway-active-call-fixture")
                    .version("1.0.0")
                    .streamable_http("127.0.0.1", kPort, "/mcp")
                    .tool<Json, ToolResult>("slow", [](const Json& input) {
                      std::this_thread::sleep_for(
                          std::chrono::milliseconds{
                              input.value("sleepMs", 500)});
                      return ToolResult::text("slow-done");
                    })
                    .build();
  require(server.has_value(), "active-call fixture should build");

  auto running = mcp::serve(std::move(*server));
  require(running.has_value(), "active-call fixture should start");
  running->wait_until_ready();

  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "busy";
  upstream.transport = mcp::gateway::UpstreamTransportKind::streamable_http;
  upstream.streamable_http.uri = uri;
  upstream.streamable_http.timeout = std::chrono::seconds{3};
  config.upstreams.push_back(std::move(upstream));

  mcp::gateway::GatewayRuntime runtime(std::move(config));
  std::exception_ptr worker_error;
  std::thread worker([&] {
    try {
      auto called = runtime.call_tool("busy.slow", Json{{"sleepMs", 500}});
      require(called.has_value(), "slow http call should succeed");
      require_text_result(*called, "slow-done");
    } catch (...) {
      worker_error = std::current_exception();
    }
  });

  bool observed_active = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state = require_upstream_state(runtime.upstream_states(), "busy");
    if (state.active_calls >= 1) {
      observed_active = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }

  worker.join();
  if (worker_error) {
    std::rethrow_exception(worker_error);
  }
  require(observed_active, "runtime should expose active upstream call count");

  const auto state = require_upstream_state(runtime.upstream_states(), "busy");
  require(state.active_calls == 0,
          "runtime should clear active upstream call count after completion");
  require_status(state, UpstreamRuntimeStatus::healthy,
                 "successful slow call should mark upstream healthy");

  const auto stopped = running->stop();
  require(stopped.has_value(), "active-call fixture should stop");
}

void test_concurrent_stdio_calls_to_one_upstream() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "stdio_busy";
  mcp::gateway::GatewayRuntime runtime(std::move(config));

  std::exception_ptr first_error;
  std::exception_ptr second_error;

  std::thread first_worker([&] {
    try {
      auto called =
          runtime.call_tool("stdio_busy.slow", Json{{"sleepMs", 600}});
      require(called.has_value(), "first slow stdio call should succeed");
      require_text_result(*called, "slow-done");
    } catch (...) {
      first_error = std::current_exception();
    }
  });

  std::thread second_worker([&] {
    try {
      auto called =
          runtime.call_tool("stdio_busy.slow", Json{{"sleepMs", 600}});
      require(called.has_value(), "second slow stdio call should succeed");
      require_text_result(*called, "slow-done");
    } catch (...) {
      second_error = std::current_exception();
    }
  });

  bool observed_both_active = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state =
        require_upstream_state(runtime.upstream_states(), "stdio_busy");
    if (state.active_calls >= 2) {
      observed_both_active = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }

  first_worker.join();
  second_worker.join();
  if (first_error) {
    std::rethrow_exception(first_error);
  }
  if (second_error) {
    std::rethrow_exception(second_error);
  }
  require(observed_both_active,
          "runtime should expose concurrent active calls to one stdio upstream");

  const auto state =
      require_upstream_state(runtime.upstream_states(), "stdio_busy");
  require(state.active_calls == 0,
          "same-upstream stdio active calls should clear");
  require_status(state, UpstreamRuntimeStatus::healthy,
                 "same-upstream stdio calls should leave upstream healthy");
}

void test_runtime_stop_waits_for_active_stdio_call() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "stdio_stop";
  const auto marker_path =
      std::filesystem::temp_directory_path() /
      ("cxxmcp_gateway_stdio_marker_" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()) +
       ".txt");
  std::error_code ignored;
  std::filesystem::remove(marker_path, ignored);
  config.upstreams.front()
      .process_stdio.env["CXXMCP_GATEWAY_STDIO_MARKER_FILE"] =
      marker_path.string();
  mcp::gateway::GatewayRuntime runtime(std::move(config));

  std::exception_ptr worker_error;
  std::thread worker([&] {
    try {
      auto called =
          runtime.call_tool("stdio_stop.slow", Json{{"sleepMs", 600}});
      require(called.has_value(), "slow stdio call should succeed");
      require_text_result(*called, "slow-done");
    } catch (...) {
      worker_error = std::current_exception();
    }
  });

  bool observed_active = false;
  bool observed_marker = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state =
        require_upstream_state(runtime.upstream_states(), "stdio_stop");
    if (state.active_calls >= 1) {
      observed_active = true;
    }
    if (std::filesystem::exists(marker_path)) {
      observed_marker = true;
    }
    if (observed_active && observed_marker) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(observed_active, "stdio shutdown test should observe active call");
  require(observed_marker, "stdio shutdown test should observe child process marker");

  const auto stop_started = std::chrono::steady_clock::now();
  auto stopped = runtime.stop();
  const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;
  require(stopped.has_value(),
          "runtime stop should succeed while stdio call is active");
  require(stop_elapsed >= std::chrono::milliseconds{200},
          "runtime stop should wait for active stdio calls to drain");

  worker.join();
  if (worker_error) {
    std::rethrow_exception(worker_error);
  }

  const auto state =
      require_upstream_state(runtime.upstream_states(), "stdio_stop");
  require(state.active_calls == 0,
          "runtime stop should clear active stdio call count");
  require_status(state, UpstreamRuntimeStatus::stopped,
                 "runtime stop should mark stdio upstream stopped");
  bool marker_removed = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (!std::filesystem::exists(marker_path)) {
      marker_removed = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(marker_removed,
          "runtime stop should clean up the active stdio child process");

  auto post_stop_call =
      runtime.call_tool("stdio_stop.echo", Json{{"value", "after-stop"}});
  require(!post_stop_call.has_value(),
          "runtime should reject stdio calls after stop");

  auto post_stop_list = runtime.list_tools();
  require(!post_stop_list.has_value(),
          "runtime should reject tools/list after stop");
}

void test_runtime_stop_waits_for_active_http_call() {
  constexpr auto kPort = 39952;
  const std::string uri = "http://127.0.0.1:" + std::to_string(kPort) + "/mcp";

  auto server = mcp::ServerPeer::builder()
                    .name("cxxmcp-gateway-shutdown-fixture")
                    .version("1.0.0")
                    .streamable_http("127.0.0.1", kPort, "/mcp")
                    .tool<Json, ToolResult>("slow", [](const Json& input) {
                      std::this_thread::sleep_for(
                          std::chrono::milliseconds{
                              input.value("sleepMs", 600)});
                      return ToolResult::text("slow-done");
                    })
                    .build();
  require(server.has_value(), "shutdown fixture should build");

  auto running = mcp::serve(std::move(*server));
  require(running.has_value(), "shutdown fixture should start");
  running->wait_until_ready();

  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "shutdown";
  upstream.transport = mcp::gateway::UpstreamTransportKind::streamable_http;
  upstream.streamable_http.uri = uri;
  upstream.streamable_http.timeout = std::chrono::seconds{3};
  config.upstreams.push_back(std::move(upstream));

  mcp::gateway::GatewayRuntime runtime(std::move(config));
  std::exception_ptr worker_error;
  std::thread worker([&] {
    try {
      auto called =
          runtime.call_tool("shutdown.slow", Json{{"sleepMs", 600}});
      require(called.has_value(), "slow shutdown call should succeed");
      require_text_result(*called, "slow-done");
    } catch (...) {
      worker_error = std::current_exception();
    }
  });

  bool observed_active = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state =
        require_upstream_state(runtime.upstream_states(), "shutdown");
    if (state.active_calls >= 1) {
      observed_active = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(observed_active, "shutdown test should observe active upstream call");

  const auto stop_started = std::chrono::steady_clock::now();
  auto stopped_runtime = runtime.stop();
  const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;
  require(stopped_runtime.has_value(),
          "runtime stop should succeed while upstream call is active");
  require(stop_elapsed >= std::chrono::milliseconds{200},
          "runtime stop should wait for active upstream calls to drain");

  worker.join();
  if (worker_error) {
    std::rethrow_exception(worker_error);
  }

  const auto state =
      require_upstream_state(runtime.upstream_states(), "shutdown");
  require(state.active_calls == 0,
          "runtime stop should leave no active upstream calls");
  require_status(state, UpstreamRuntimeStatus::stopped,
                 "runtime stop should mark upstream stopped after drain");

  auto post_stop_call =
      runtime.call_tool("shutdown.slow", Json{{"sleepMs", 1}});
  require(!post_stop_call.has_value(),
          "runtime should reject new upstream calls after stop");

  const auto stopped_server = running->stop();
  require(stopped_server.has_value(), "shutdown fixture should stop");
}

void test_concurrent_calls_to_multiple_upstreams() {
  constexpr auto kFirstPort = 39953;
  constexpr auto kSecondPort = 39954;
  const std::string first_uri =
      "http://127.0.0.1:" + std::to_string(kFirstPort) + "/mcp";
  const std::string second_uri =
      "http://127.0.0.1:" + std::to_string(kSecondPort) + "/mcp";

  auto first_server =
      mcp::ServerPeer::builder()
          .name("cxxmcp-gateway-concurrent-first")
          .version("1.0.0")
          .streamable_http("127.0.0.1", kFirstPort, "/mcp")
          .tool<Json, ToolResult>("slow", [](const Json& input) {
            std::this_thread::sleep_for(std::chrono::milliseconds{
                input.value("sleepMs", 600)});
            return ToolResult::text("first-done");
          })
          .build();
  require(first_server.has_value(), "first concurrent fixture should build");

  auto second_server =
      mcp::ServerPeer::builder()
          .name("cxxmcp-gateway-concurrent-second")
          .version("1.0.0")
          .streamable_http("127.0.0.1", kSecondPort, "/mcp")
          .tool<Json, ToolResult>("slow", [](const Json& input) {
            std::this_thread::sleep_for(std::chrono::milliseconds{
                input.value("sleepMs", 600)});
            return ToolResult::text("second-done");
          })
          .build();
  require(second_server.has_value(), "second concurrent fixture should build");

  auto first_running = mcp::serve(std::move(*first_server));
  require(first_running.has_value(), "first concurrent fixture should start");
  first_running->wait_until_ready();

  auto second_running = mcp::serve(std::move(*second_server));
  require(second_running.has_value(), "second concurrent fixture should start");
  second_running->wait_until_ready();

  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer first;
  first.id = "first";
  first.transport = mcp::gateway::UpstreamTransportKind::streamable_http;
  first.streamable_http.uri = first_uri;
  first.streamable_http.timeout = std::chrono::seconds{3};
  config.upstreams.push_back(std::move(first));

  mcp::gateway::UpstreamServer second;
  second.id = "second";
  second.transport = mcp::gateway::UpstreamTransportKind::streamable_http;
  second.streamable_http.uri = second_uri;
  second.streamable_http.timeout = std::chrono::seconds{3};
  config.upstreams.push_back(std::move(second));

  mcp::gateway::GatewayRuntime runtime(std::move(config));
  std::exception_ptr first_error;
  std::exception_ptr second_error;

  std::thread first_worker([&] {
    try {
      auto called = runtime.call_tool("first.slow", Json{{"sleepMs", 600}});
      require(called.has_value(), "first concurrent call should succeed");
      require_text_result(*called, "first-done");
    } catch (...) {
      first_error = std::current_exception();
    }
  });

  std::thread second_worker([&] {
    try {
      auto called = runtime.call_tool("second.slow", Json{{"sleepMs", 600}});
      require(called.has_value(), "second concurrent call should succeed");
      require_text_result(*called, "second-done");
    } catch (...) {
      second_error = std::current_exception();
    }
  });

  bool observed_both_active = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto states = runtime.upstream_states();
    const auto first_state = require_upstream_state(states, "first");
    const auto second_state = require_upstream_state(states, "second");
    if (first_state.active_calls >= 1 && second_state.active_calls >= 1) {
      observed_both_active = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }

  first_worker.join();
  second_worker.join();
  if (first_error) {
    std::rethrow_exception(first_error);
  }
  if (second_error) {
    std::rethrow_exception(second_error);
  }
  require(observed_both_active,
          "runtime should expose concurrent active calls on multiple upstreams");

  const auto states = runtime.upstream_states();
  const auto first_state = require_upstream_state(states, "first");
  const auto second_state = require_upstream_state(states, "second");
  require(first_state.active_calls == 0,
          "first upstream active calls should clear");
  require(second_state.active_calls == 0,
          "second upstream active calls should clear");
  require_status(first_state, UpstreamRuntimeStatus::healthy,
                 "first concurrent upstream should be healthy");
  require_status(second_state, UpstreamRuntimeStatus::healthy,
                 "second concurrent upstream should be healthy");

  const auto first_stopped = first_running->stop();
  require(first_stopped.has_value(), "first concurrent fixture should stop");
  const auto second_stopped = second_running->stop();
  require(second_stopped.has_value(), "second concurrent fixture should stop");
}

void test_http_unavailable() {
  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "down";
  upstream.transport = mcp::gateway::UpstreamTransportKind::streamable_http;
  upstream.streamable_http.uri = "http://127.0.0.1:39948/mcp";
  upstream.streamable_http.timeout = std::chrono::milliseconds{250};
  config.upstreams.push_back(std::move(upstream));

  mcp::gateway::GatewayRuntime runtime(std::move(config));
  auto tools = runtime.list_tools();
  require(!tools.has_value(), "unavailable http upstream should fail list");
  require_gateway_upstream_error(tools.error(), "down");
}

void test_start_http_invalid_config_fails_before_binding_port() {
  constexpr auto kPort = 39956;

  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "invalid";
  upstream.transport = mcp::gateway::UpstreamTransportKind::process_stdio;
  config.upstreams.push_back(std::move(upstream));

  mcp::gateway::GatewayRuntime runtime(std::move(config));
  auto started = runtime.start_http(
      {.host = "127.0.0.1", .port = kPort, .path = "/mcp"});
  require(!started.has_value(),
          "invalid config should fail before hosted endpoint startup");
  require(started.error().category == "gateway",
          "invalid startup should return gateway validation error");

  mcp::gateway::GatewayConfig valid_config;
  mcp::gateway::UpstreamServer disabled;
  disabled.id = "disabled";
  disabled.enabled = false;
  valid_config.upstreams.push_back(std::move(disabled));
  mcp::gateway::GatewayRuntime reusable_runtime(std::move(valid_config));
  auto rebound = reusable_runtime.start_http(
      {.host = "127.0.0.1", .port = kPort, .path = "/mcp"});
  require(rebound.has_value(),
          "port should remain available after invalid gateway startup");

  auto client = mcp::ClientPeer::builder()
                    .streamable_http("http://127.0.0.1:" +
                                     std::to_string(kPort) + "/mcp")
                    .build();
  require(client.has_value(), "port reuse client should build");
  auto running_client = mcp::serve(std::move(*client));
  require(running_client.has_value(), "port reuse client should start");
  auto initialized = running_client->peer().initialize(
      "cxxmcp-gateway-port-reuse-client", "1.0.0");
  require(initialized.has_value(), "port reuse client should initialize");
  auto stopped_client = running_client->stop();
  require(stopped_client.has_value(), "port reuse client should stop");

  const auto stopped = reusable_runtime.stop();
  require(stopped.has_value(), "port reuse gateway should stop");
}

void test_hosted_gateway_http_endpoint_stops_while_idle() {
  constexpr auto kPort = 39957;

  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "disabled";
  upstream.enabled = false;
  config.upstreams.push_back(std::move(upstream));

  mcp::gateway::GatewayRuntime gateway(std::move(config));
  auto started = gateway.start_http(
      {.host = "127.0.0.1", .port = kPort, .path = "/mcp"});
  require(started.has_value(), "idle hosted gateway endpoint should start");

  const auto started_at = std::chrono::steady_clock::now();
  auto stopped = gateway.stop();
  const auto elapsed = std::chrono::steady_clock::now() - started_at;

  require(stopped.has_value(),
          "idle hosted gateway endpoint should stop without a client");
  require(elapsed < std::chrono::seconds(2),
          "idle hosted gateway stop should return promptly");

  const auto states = gateway.upstream_states();
  const auto state = require_upstream_state(states, "disabled");
  require_status(state, UpstreamRuntimeStatus::stopped,
                 "idle hosted gateway should mark upstream stopped");
}

void test_hosted_gateway_http_endpoint() {
  constexpr auto kPort = 39950;

  mcp::gateway::GatewayRuntime gateway(make_stdio_config());
  auto started = gateway.start_http(
      {.host = "127.0.0.1", .port = kPort, .path = "/mcp"});
  require(started.has_value(), "hosted gateway endpoint should start");

  auto client = mcp::ClientPeer::builder()
                    .streamable_http("http://127.0.0.1:" +
                                     std::to_string(kPort) + "/mcp")
                    .build();
  require(client.has_value(), "downstream client peer should build");

  auto running_client = mcp::serve(std::move(*client));
  require(running_client.has_value(), "downstream client service should start");

  auto initialized = running_client->peer().initialize(
      "cxxmcp-gateway-test-client", "1.0.0");
  require(initialized.has_value(), "downstream initialize should succeed");

  auto capabilities = running_client->peer().server_capabilities();
  require(capabilities.has_value(), "gateway should advertise capabilities");
  require(capabilities->tools.enabled, "gateway should advertise tools");
  require(!capabilities->tools.list_changed,
          "gateway should not advertise tools/listChanged");
  require(!capabilities->resources.enabled,
          "gateway should not advertise resources");
  require(!capabilities->prompts.enabled,
          "gateway should not advertise prompts");
  require(!capabilities->tasks.has_value(),
          "gateway should not advertise tasks");

  auto notified = running_client->peer().notify_initialized();
  require(notified.has_value(), "downstream initialized notification should work");

  auto tools = running_client->peer().list_all_tools();
  require(tools.has_value(), "downstream tools/list should succeed");
  require(has_tool(*tools, "stdio.echo"),
          "downstream tools/list should include routed stdio tool");

  auto called = running_client->peer().call_tool(
      "stdio.echo", Json{{"value", "from-downstream"}});
  require(called.has_value(), "downstream tools/call should route");
  require_text_result(*called, "from-downstream");

  auto stopped_client = running_client->stop();
  require(stopped_client.has_value(), "downstream client should stop");
  auto stopped_gateway = gateway.stop();
  require(stopped_gateway.has_value(), "hosted gateway endpoint should stop");

  auto restarted = gateway.start_http(
      {.host = "127.0.0.1", .port = kPort, .path = "/mcp"});
  require(!restarted.has_value(),
          "stopped gateway runtime should reject endpoint restart");
}

void test_hosted_gateway_rejects_request_before_initialized_notification() {
  constexpr auto kPort = 39960;

  mcp::gateway::GatewayRuntime gateway(make_stdio_config());
  auto started = gateway.start_http(
      {.host = "127.0.0.1", .port = kPort, .path = "/mcp"});
  require(started.has_value(),
          "pre-initialized hosted gateway endpoint should start");

  auto client = mcp::ClientPeer::builder()
                    .streamable_http("http://127.0.0.1:" +
                                     std::to_string(kPort) + "/mcp")
                    .build();
  require(client.has_value(), "pre-initialized client should build");
  auto running_client = mcp::serve(std::move(*client));
  require(running_client.has_value(), "pre-initialized client should start");

  auto initialized = running_client->peer().initialize(
      "cxxmcp-gateway-pre-initialized-client", "1.0.0");
  require(initialized.has_value(), "pre-initialized client should initialize");

  auto early_ping = running_client->peer().ping();
  require(early_ping.has_value(),
          "gateway should allow SDK-owned ping before initialized notification");

  auto early_tools = running_client->peer().list_all_tools();
  require(!early_tools.has_value(),
          "gateway should reject tools/list before initialized notification");
  require(early_tools.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidRequest),
          "pre-initialized tools/list should map to invalid request");
  require(early_tools.error().message.find("not initialized") !=
              std::string::npos,
          "pre-initialized tools/list should report uninitialized session");

  auto notified = running_client->peer().notify_initialized();
  require(notified.has_value(),
          "initialized notification should work after early rejection");
  auto tools = running_client->peer().list_all_tools();
  require(tools.has_value(),
          "tools/list should succeed after initialized notification");
  require(has_tool(*tools, "stdio.echo"),
          "post-initialized tools/list should include routed stdio tool");

  auto stopped_client = running_client->stop();
  require(stopped_client.has_value(), "pre-initialized client should stop");
  auto stopped_gateway = gateway.stop();
  require(stopped_gateway.has_value(),
          "pre-initialized hosted gateway should stop");
}

void test_hosted_gateway_multiple_downstream_clients() {
  constexpr auto kPort = 39958;

  mcp::gateway::GatewayRuntime gateway(make_stdio_config());
  auto started = gateway.start_http(
      {.host = "127.0.0.1", .port = kPort, .path = "/mcp"});
  require(started.has_value(),
          "multi-client hosted gateway endpoint should start");

  auto make_client = [] {
    return mcp::ClientPeer::builder()
        .streamable_http("http://127.0.0.1:39958/mcp")
        .build();
  };

  auto first_client = make_client();
  auto second_client = make_client();
  require(first_client.has_value(), "first downstream client should build");
  require(second_client.has_value(), "second downstream client should build");

  auto first_running = mcp::serve(std::move(*first_client));
  auto second_running = mcp::serve(std::move(*second_client));
  require(first_running.has_value(), "first downstream client should start");
  require(second_running.has_value(), "second downstream client should start");

  auto first_initialized = first_running->peer().initialize(
      "cxxmcp-gateway-multi-client-a", "1.0.0");
  auto second_initialized = second_running->peer().initialize(
      "cxxmcp-gateway-multi-client-b", "1.0.0");
  require(first_initialized.has_value(),
          "first downstream initialize should succeed");
  require(second_initialized.has_value(),
          "second downstream initialize should succeed");

  auto first_notified = first_running->peer().notify_initialized();
  auto second_notified = second_running->peer().notify_initialized();
  require(first_notified.has_value(),
          "first downstream initialized notification should work");
  require(second_notified.has_value(),
          "second downstream initialized notification should work");

  std::exception_ptr first_error;
  std::exception_ptr second_error;
  std::thread first_worker([&] {
    try {
      auto called = first_running->peer().call_tool(
          "stdio.echo", Json{{"value", "from-first-client"}});
      if (!called.has_value()) {
        throw std::runtime_error("first downstream call should route: " +
                                 called.error().message + " / " +
                                 called.error().detail + " / " +
                                 called.error().category);
      }
      require_text_result(*called, "from-first-client");
    } catch (...) {
      first_error = std::current_exception();
    }
  });
  std::thread second_worker([&] {
    try {
      auto called = second_running->peer().call_tool(
          "stdio.echo", Json{{"value", "from-second-client"}});
      if (!called.has_value()) {
        throw std::runtime_error("second downstream call should route: " +
                                 called.error().message + " / " +
                                 called.error().detail + " / " +
                                 called.error().category);
      }
      require_text_result(*called, "from-second-client");
    } catch (...) {
      second_error = std::current_exception();
    }
  });

  first_worker.join();
  second_worker.join();
  if (first_error) {
    std::rethrow_exception(first_error);
  }
  if (second_error) {
    std::rethrow_exception(second_error);
  }

  const auto states = gateway.upstream_states();
  const auto state = require_upstream_state(states, "stdio");
  require(state.active_calls == 0,
          "multi-client calls should clear upstream active call count");
  require_status(state, UpstreamRuntimeStatus::healthy,
                 "multi-client calls should leave upstream healthy");

  auto first_stopped = first_running->stop();
  auto second_stopped = second_running->stop();
  require(first_stopped.has_value(), "first downstream client should stop");
  require(second_stopped.has_value(), "second downstream client should stop");

  auto gateway_stopped = gateway.stop();
  require(gateway_stopped.has_value(),
          "multi-client hosted gateway should stop");
}

void test_hosted_gateway_without_enabled_upstreams_advertises_no_tools() {
  constexpr auto kPort = 39955;

  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "disabled";
  upstream.enabled = false;
  config.upstreams.push_back(std::move(upstream));

  mcp::gateway::GatewayRuntime gateway(std::move(config));
  auto started = gateway.start_http(
      {.host = "127.0.0.1", .port = kPort, .path = "/mcp"});
  require(started.has_value(),
          "hosted gateway with no enabled upstreams should start");

  auto client = mcp::ClientPeer::builder()
                    .streamable_http("http://127.0.0.1:" +
                                     std::to_string(kPort) + "/mcp")
                    .build();
  require(client.has_value(), "no-tools downstream client should build");

  auto running_client = mcp::serve(std::move(*client));
  require(running_client.has_value(),
          "no-tools downstream client service should start");

  auto initialized = running_client->peer().initialize(
      "cxxmcp-gateway-no-tools-client", "1.0.0");
  require(initialized.has_value(), "no-tools initialize should succeed");

  auto capabilities = running_client->peer().server_capabilities();
  require(capabilities.has_value(),
          "gateway should advertise an explicit capability set");
  require(!capabilities->tools.enabled,
          "gateway should not advertise tools without enabled upstreams");
  require(!capabilities->resources.enabled,
          "gateway should not advertise resources without routing support");
  require(!capabilities->prompts.enabled,
          "gateway should not advertise prompts without routing support");
  require(!capabilities->tasks.has_value(),
          "gateway should not advertise tasks without routing support");

  auto stopped_client = running_client->stop();
  require(stopped_client.has_value(), "no-tools downstream client should stop");
  auto stopped_gateway = gateway.stop();
  require(stopped_gateway.has_value(), "no-tools gateway should stop");
}

void test_raw_request_routing_surface() {
  mcp::gateway::GatewayRuntime runtime(make_stdio_config());

  mcp::protocol::JsonRpcRequest unsupported;
  unsupported.method = "resources/list";
  unsupported.id = std::int64_t{1};
  auto unsupported_response = runtime.handle_request(unsupported);
  require(!unsupported_response.has_value(),
          "unsupported methods should remain SDK-owned/nullopt");

  mcp::protocol::JsonRpcRequest invalid_tool_name;
  invalid_tool_name.method = mcp::protocol::ToolsCallMethod;
  invalid_tool_name.id = std::int64_t{2};
  invalid_tool_name.params = Json{{"name", "bad"}, {"arguments", Json::object()}};
  auto invalid_response = runtime.handle_request(invalid_tool_name);
  require(invalid_response.has_value(), "invalid tool call should respond");
  require(invalid_response->error.has_value(), "invalid tool call should error");
  require(invalid_response->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "invalid exposed tool name should map to invalid params");
}

}  // namespace

int main() {
  try {
    test_disabled_upstream();
    test_invalid_config_advertises_no_tools();
    test_stdio_process_start_failure();
    test_tools_list_fail_fast_after_partial_success();
    test_stdio_process_exit_before_initialize();
    test_stdio_malformed_response_before_initialize();
    test_stdio_upstream();
    test_http_upstream();
    test_http_unavailable();
    test_start_http_invalid_config_fails_before_binding_port();
    test_hosted_gateway_http_endpoint_stops_while_idle();
    test_http_timeout();
    test_concurrent_http_calls_update_active_state();
    test_concurrent_stdio_calls_to_one_upstream();
    test_runtime_stop_waits_for_active_stdio_call();
    test_runtime_stop_waits_for_active_http_call();
    test_concurrent_calls_to_multiple_upstreams();
    test_hosted_gateway_http_endpoint();
    test_hosted_gateway_rejects_request_before_initialized_notification();
    test_hosted_gateway_multiple_downstream_clients();
    test_hosted_gateway_without_enabled_upstreams_advertises_no_tools();
    test_raw_request_routing_surface();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "gateway runtime integration failed: " << ex.what() << "\n";
    return 1;
  }
}
