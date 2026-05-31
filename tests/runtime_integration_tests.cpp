// Copyright (c) 2025 [caomengxuan666]

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "cxxmcp/gateway/router.hpp"
#include "cxxmcp/gateway/runtime.hpp"
#include "cxxmcp/peer.hpp"
#include "cxxmcp/protocol/tool.hpp"
#include "cxxmcp/service.hpp"

namespace {

using Json = mcp::protocol::Json;
using Prompt = mcp::protocol::Prompt;
using PromptsGetResult = mcp::protocol::PromptsGetResult;
using Resource = mcp::protocol::Resource;
using ResourceTemplate = mcp::protocol::ResourceTemplate;
using ResourcesReadResult = mcp::protocol::ResourcesReadResult;
using ToolDefinition = mcp::protocol::ToolDefinition;
using ToolResult = mcp::protocol::ToolResult;
using UpstreamRuntimeState = mcp::gateway::UpstreamRuntimeState;
using UpstreamRuntimeStatus = mcp::gateway::UpstreamRuntimeStatus;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::size_t count_regular_files(const std::filesystem::path& directory) {
  std::error_code ignored;
  if (!std::filesystem::exists(directory, ignored)) {
    return 0;
  }
  std::size_t count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory,
                                                               ignored)) {
    if (entry.is_regular_file(ignored)) {
      ++count;
    }
  }
  return count;
}

#ifdef _WIN32
class SocketRuntime final {
 public:
  SocketRuntime() {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
      throw std::runtime_error("WSAStartup failed");
    }
  }

  ~SocketRuntime() { WSACleanup(); }
};

using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

void close_socket(SocketHandle socket) { closesocket(socket); }

bool socket_failed(int result) { return result == SOCKET_ERROR; }

#else
class SocketRuntime final {
 public:
  SocketRuntime() = default;
};

using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;

void close_socket(SocketHandle socket) { close(socket); }

bool socket_failed(int result) { return result < 0; }
#endif

std::uint16_t find_available_loopback_port() {
  SocketRuntime sockets;
  SocketHandle socket = ::socket(AF_INET, SOCK_STREAM, 0);
  if (socket == kInvalidSocket) {
    throw std::runtime_error("failed to create loopback port probe socket");
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(0);
  if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
    close_socket(socket);
    throw std::runtime_error("failed to parse loopback address");
  }

  if (socket_failed(::bind(socket, reinterpret_cast<sockaddr*>(&address),
                           sizeof(address)))) {
    close_socket(socket);
    throw std::runtime_error("failed to bind loopback port probe socket");
  }

  sockaddr_in bound_address{};
#ifdef _WIN32
  int bound_address_length = sizeof(bound_address);
#else
  socklen_t bound_address_length = sizeof(bound_address);
#endif
  if (socket_failed(::getsockname(
          socket, reinterpret_cast<sockaddr*>(&bound_address),
          &bound_address_length))) {
    close_socket(socket);
    throw std::runtime_error("failed to inspect available loopback port");
  }

  const auto port = ntohs(bound_address.sin_port);
  close_socket(socket);
  return port;
}

std::pair<std::uint16_t, std::uint16_t> find_two_distinct_loopback_ports() {
  const auto first = find_available_loopback_port();
  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto second = find_available_loopback_port();
    if (second != first) {
      return {first, second};
    }
  }
  throw std::runtime_error("failed to find distinct loopback ports");
}

void serve_one_raw_http_response(std::uint16_t port, std::string_view body,
                                 std::atomic_bool& ready) {
  SocketHandle server = ::socket(AF_INET, SOCK_STREAM, 0);
  if (server == kInvalidSocket) {
    throw std::runtime_error("failed to create raw HTTP fixture socket");
  }

  const int reuse = 1;
  (void)::setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&reuse), sizeof(reuse));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
    close_socket(server);
    throw std::runtime_error("failed to parse raw HTTP fixture address");
  }

  if (socket_failed(::bind(server, reinterpret_cast<sockaddr*>(&address),
                           sizeof(address)))) {
    close_socket(server);
    throw std::runtime_error("failed to bind raw HTTP fixture");
  }
  if (socket_failed(::listen(server, 1))) {
    close_socket(server);
    throw std::runtime_error("failed to listen on raw HTTP fixture");
  }
  ready.store(true);

  sockaddr_in client_address{};
#ifdef _WIN32
  int client_address_length = sizeof(client_address);
#else
  socklen_t client_address_length = sizeof(client_address);
#endif
  SocketHandle client =
      ::accept(server, reinterpret_cast<sockaddr*>(&client_address),
               &client_address_length);
  if (client == kInvalidSocket) {
    close_socket(server);
    throw std::runtime_error("failed to accept raw HTTP fixture client");
  }

  std::string request;
  char buffer[1024];
  std::size_t expected_request_size = 0;
  while (true) {
#ifdef _WIN32
    const int received = ::recv(client, buffer, sizeof(buffer), 0);
#else
    const auto received = ::recv(client, buffer, sizeof(buffer), 0);
#endif
    if (received == 0) {
      break;
    }
    if (socket_failed(static_cast<int>(received))) {
      close_socket(client);
      close_socket(server);
      throw std::runtime_error("failed to read raw HTTP fixture request");
    }
    request.append(buffer, static_cast<std::size_t>(received));
    const auto header_end = request.find("\r\n\r\n");
    if (header_end == std::string::npos) {
      continue;
    }
    if (expected_request_size == 0) {
      expected_request_size = header_end + 4;
      const auto content_length = request.find("Content-Length:");
      if (content_length != std::string::npos &&
          content_length < header_end) {
        const auto value_start =
            request.find_first_not_of(" \t", content_length + 15);
        if (value_start != std::string::npos && value_start < header_end) {
          const auto value_end = request.find("\r\n", value_start);
          expected_request_size += static_cast<std::size_t>(
              std::stoul(request.substr(value_start,
                                        value_end - value_start)));
        }
      }
    }
    if (request.size() >= expected_request_size) {
      break;
    }
  }

  const std::string response =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: " +
      std::to_string(body.size()) +
      "\r\n"
      "Connection: close\r\n\r\n" +
      std::string(body);
  std::size_t sent = 0;
  while (sent < response.size()) {
#ifdef _WIN32
    const int chunk = ::send(client, response.data() + sent,
                             static_cast<int>(response.size() - sent), 0);
#else
    const auto chunk =
        ::send(client, response.data() + sent, response.size() - sent, 0);
#endif
    if (socket_failed(static_cast<int>(chunk))) {
      close_socket(client);
      close_socket(server);
      throw std::runtime_error("failed to send raw HTTP fixture response");
    }
    sent += static_cast<std::size_t>(chunk);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds{50});

  close_socket(client);
  close_socket(server);
}

std::string post_raw_http(std::uint16_t port, std::string_view path,
                          std::string_view body) {
  SocketRuntime sockets;
  SocketHandle socket = ::socket(AF_INET, SOCK_STREAM, 0);
  if (socket == kInvalidSocket) {
    throw std::runtime_error("failed to create socket");
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
    close_socket(socket);
    throw std::runtime_error("failed to parse loopback address");
  }

  if (socket_failed(::connect(socket, reinterpret_cast<sockaddr*>(&address),
                              sizeof(address)))) {
    close_socket(socket);
    throw std::runtime_error("failed to connect to hosted gateway");
  }

  const std::string request =
      "POST " + std::string(path) + " HTTP/1.1\r\n" +
      "Host: 127.0.0.1:" + std::to_string(port) + "\r\n" +
      "Accept: application/json, text/event-stream\r\n" +
      "Content-Type: application/json\r\n" +
      "Content-Length: " + std::to_string(body.size()) + "\r\n" +
      "Connection: close\r\n\r\n" + std::string(body);

  std::size_t sent = 0;
  while (sent < request.size()) {
#ifdef _WIN32
    const int chunk = ::send(socket, request.data() + sent,
                             static_cast<int>(request.size() - sent), 0);
#else
    const auto chunk =
        ::send(socket, request.data() + sent, request.size() - sent, 0);
#endif
    if (socket_failed(static_cast<int>(chunk))) {
      close_socket(socket);
      throw std::runtime_error("failed to send HTTP request");
    }
    sent += static_cast<std::size_t>(chunk);
  }

#ifdef _WIN32
  shutdown(socket, SD_SEND);
#else
  shutdown(socket, SHUT_WR);
#endif

  std::string response;
  char buffer[4096];
  for (;;) {
#ifdef _WIN32
    const int received = ::recv(socket, buffer, sizeof(buffer), 0);
#else
    const auto received = ::recv(socket, buffer, sizeof(buffer), 0);
#endif
    if (received == 0) {
      break;
    }
    if (socket_failed(static_cast<int>(received))) {
      close_socket(socket);
      throw std::runtime_error("failed to read HTTP response");
    }
    response.append(buffer, static_cast<std::size_t>(received));
  }

  close_socket(socket);
  return response;
}

bool has_tool(const std::vector<ToolDefinition>& tools,
              std::string_view name) {
  return std::any_of(tools.begin(), tools.end(), [&](const auto& tool) {
    return tool.name == name;
  });
}

bool has_resource(const std::vector<Resource>& resources,
                  std::string_view uri) {
  return std::any_of(resources.begin(), resources.end(), [&](const auto& r) {
    return r.uri == uri;
  });
}

bool has_resource_template(const std::vector<ResourceTemplate>& templates,
                           std::string_view uri_template) {
  return std::any_of(templates.begin(), templates.end(), [&](const auto& t) {
    return t.uri_template == uri_template;
  });
}

bool has_prompt(const std::vector<Prompt>& prompts, std::string_view name) {
  return std::any_of(prompts.begin(), prompts.end(), [&](const auto& prompt) {
    return prompt.name == name;
  });
}

bool has_runtime_event(
    const std::vector<mcp::gateway::GatewayRuntimeEvent>& events,
    mcp::gateway::GatewayRuntimeEventKind kind,
    std::string_view upstream_id = {},
    std::optional<UpstreamRuntimeStatus> status = std::nullopt) {
  return std::any_of(events.begin(), events.end(), [&](const auto& event) {
    return event.kind == kind &&
           (upstream_id.empty() || event.upstream_id == upstream_id) &&
           (!status.has_value() || event.upstream_status == *status);
  });
}

const ToolDefinition& require_tool(const std::vector<ToolDefinition>& tools,
                                   std::string_view name) {
  const auto it = std::find_if(tools.begin(), tools.end(), [&](const auto& tool) {
    return tool.name == name;
  });
  if (it == tools.end()) {
    throw std::runtime_error("missing tool " + std::string(name));
  }
  return *it;
}

const Resource& require_resource(const std::vector<Resource>& resources,
                                 std::string_view uri) {
  const auto it =
      std::find_if(resources.begin(), resources.end(), [&](const auto& r) {
        return r.uri == uri;
      });
  if (it == resources.end()) {
    throw std::runtime_error("missing resource " + std::string(uri));
  }
  return *it;
}

const ResourceTemplate& require_resource_template(
    const std::vector<ResourceTemplate>& templates,
    std::string_view uri_template) {
  const auto it =
      std::find_if(templates.begin(), templates.end(), [&](const auto& t) {
        return t.uri_template == uri_template;
      });
  if (it == templates.end()) {
    throw std::runtime_error("missing resource template " +
                             std::string(uri_template));
  }
  return *it;
}

const Prompt& require_prompt(const std::vector<Prompt>& prompts,
                             std::string_view name) {
  const auto it =
      std::find_if(prompts.begin(), prompts.end(), [&](const auto& prompt) {
        return prompt.name == name;
      });
  if (it == prompts.end()) {
    throw std::runtime_error("missing prompt " + std::string(name));
  }
  return *it;
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

void require_text_resource(const ResourcesReadResult& result,
                           std::string_view uri, std::string_view text) {
  require(!result.contents.empty(), "resource read should include content");
  require(result.contents.front().uri == uri,
          "resource read content URI mismatch");
  require(result.contents.front().mime_type == "text/plain",
          "resource read content MIME type mismatch");
  require(result.contents.front().text.has_value(),
          "resource read content should be text");
  require(*result.contents.front().text == text,
          "resource read text mismatch");
}

void require_text_prompt(const PromptsGetResult& result,
                         std::string_view text) {
  require(!result.messages.empty(), "prompt get should include messages");
  require(result.messages.front().role == "user",
          "prompt message role mismatch");
  require(result.messages.front().content.type == "text",
          "prompt message should be text");
  require(result.messages.front().content.text == text,
          "prompt message text mismatch");
}

void require_mvp_server_capability_json_shape(
    const mcp::protocol::ServerCapabilities& capabilities,
    bool expect_completions = false) {
  const auto json = mcp::protocol::server_capabilities_to_json(capabilities);
  require(json.is_object(), "server capabilities should serialize as object");

  require(!json.contains("logging"),
          "MVP server capabilities should not serialize logging");
  require(!json.contains("tasks"),
          "MVP server capabilities should not serialize tasks");
  require(!json.contains("experimental"),
          "MVP server capabilities should not serialize experimental");
  require(!json.contains("extensions"),
          "MVP server capabilities should not serialize extensions");

  if (json.contains("tools")) {
    require(!json.at("tools").contains("listChanged"),
            "MVP tools capability should not serialize listChanged");
  }
  if (json.contains("resources")) {
    require(!json.at("resources").contains("listChanged"),
            "MVP resources capability should not serialize listChanged");
    require(!json.at("resources").contains("subscribe"),
            "MVP resources capability should not serialize subscribe");
  }
  if (json.contains("prompts")) {
    require(!json.at("prompts").contains("listChanged"),
            "MVP prompts capability should not serialize listChanged");
  }

  if (expect_completions) {
    require(json.contains("completions"),
            "completion-capable runtime should serialize completions");
  } else {
    require(!json.contains("completions"),
            "MVP server capabilities should not serialize completions before "
            "upstream support is proven");
  }
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
  if (error.category != "gateway.upstream.protocol") {
    throw std::runtime_error("upstream protocol errors should use gateway "
                             "protocol category, got: " +
                             error.category + " / " + error.message + " / " +
                             error.detail);
  }
}

void require_runtime_stopping_error(const mcp::core::Error& error,
                                    std::string_view operation) {
  require(error.code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidRequest),
          "runtime stopping rejection should use InvalidRequest");
  require(error.message.find("stopping") != std::string::npos,
          "runtime stopping rejection should mention stopping");
  require(error.detail == operation,
          "runtime stopping rejection should preserve operation context");
}

void require_raw_runtime_stopped_error(
    const mcp::protocol::JsonRpcResponse& response,
    std::string_view operation) {
  require(response.error.has_value(),
          "raw runtime stopped response should be an error");
  require(response.error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidRequest),
          "raw runtime stopped response should use InvalidRequest");
  require(response.error->message.find("stopped") != std::string::npos,
          "raw runtime stopped response should mention stopped");
  require(response.error->data.has_value(),
          "raw runtime stopped response should preserve operation context");
  require(response.error->data->is_string(),
          "raw runtime stopped response data should be a string");
  require(response.error->data->get<std::string>() == operation,
          "raw runtime stopped response data should match operation");
}

void require_gateway_unsupported_capability_error(
    const mcp::core::Error& error, std::string_view upstream_id) {
  require(error.code ==
              static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
          "unsupported upstream capability should map to MethodNotFound");
  require(error.message.find("does not support") != std::string::npos,
          "unsupported upstream capability should explain unsupported routing");
  require(error.detail == upstream_id,
          "unsupported upstream capability should preserve upstream context");
  require(error.category == "gateway",
          "unsupported upstream capability should remain gateway-owned");
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

mcp::gateway::GatewayConfig make_disabled_config(std::string id = "disabled") {
  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = std::move(id);
  upstream.enabled = false;
  config.upstreams.push_back(std::move(upstream));
  return config;
}

void test_disabled_upstream() {
  mcp::gateway::GatewayRuntime runtime(make_disabled_config());
  auto capabilities = runtime.server_capabilities();
  require(!capabilities.tools.enabled,
          "disabled-only runtime should not advertise tools");
  require(!capabilities.resources.enabled,
          "runtime should not advertise resources");
  require(!capabilities.prompts.enabled,
          "runtime should not advertise prompts");
  require(!capabilities.completions.enabled,
          "runtime should not advertise completions");
  require(!capabilities.tasks.has_value(),
          "runtime should not advertise tasks");
  require_mvp_server_capability_json_shape(capabilities);

  auto tools = runtime.list_tools();
  require(tools.has_value(), "disabled upstream list should still succeed");
  require(tools->empty(), "disabled upstream should not expose tools");

  auto resources = runtime.list_resources();
  require(resources.has_value(),
          "disabled upstream resource list should still succeed");
  require(resources->empty(), "disabled upstream should not expose resources");

  auto resource_templates = runtime.list_resource_templates();
  require(resource_templates.has_value(),
          "disabled upstream resource template list should still succeed");
  require(resource_templates->empty(),
          "disabled upstream should not expose resource templates");

  auto prompts = runtime.list_prompts();
  require(prompts.has_value(),
          "disabled upstream prompt list should still succeed");
  require(prompts->empty(), "disabled upstream should not expose prompts");

  auto called = runtime.call_tool("disabled.echo", Json::object());
  require(!called.has_value(), "disabled upstream call should fail");
  require(called.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::ToolNotFound),
          "disabled upstream should map to tool-not-found");
  require(called.error().message == "gateway upstream is disabled",
          "disabled upstream should report stable routing error message");
  require(called.error().detail == "disabled",
          "disabled upstream error should preserve upstream id context");

  const auto disabled_uri =
      mcp::gateway::GatewayRouter::expose_resource_uri(
          "disabled", "file:///fixture/readme.txt");
  auto read = runtime.read_resource(disabled_uri);
  require(!read.has_value(), "disabled upstream resource read should fail");
  require(read.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::ResourceNotFound),
          "disabled upstream resource should map to resource-not-found");
  require(read.error().message == "gateway upstream is disabled",
          "disabled resource upstream should report stable routing error");
  require(read.error().detail == "disabled",
          "disabled resource error should preserve upstream id context");

  auto prompt =
      runtime.get_prompt("disabled.summarize", Json{{"text", "ignored"}});
  require(!prompt.has_value(), "disabled upstream prompt get should fail");
  require(prompt.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "disabled upstream prompt should map to invalid params");
  require(prompt.error().message == "gateway upstream is disabled",
          "disabled prompt upstream should report stable routing error");
  require(prompt.error().detail == "disabled",
          "disabled prompt error should preserve upstream id context");

  mcp::protocol::CompleteParams disabled_prompt_completion;
  disabled_prompt_completion.ref =
      mcp::protocol::prompt_completion_reference("disabled.summarize");
  disabled_prompt_completion.argument.name = "text";
  disabled_prompt_completion.argument.value = "ignored";
  auto prompt_completion =
      runtime.complete(std::move(disabled_prompt_completion));
  require(!prompt_completion.has_value(),
          "disabled upstream prompt completion should fail");
  require(prompt_completion.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "disabled prompt completion should map to invalid params");
  require(prompt_completion.error().message == "gateway upstream is disabled",
          "disabled prompt completion should report stable routing error");
  require(prompt_completion.error().detail == "disabled",
          "disabled prompt completion should preserve upstream id context");

  mcp::protocol::CompleteParams disabled_resource_completion;
  disabled_resource_completion.ref =
      mcp::protocol::resource_completion_reference(disabled_uri);
  disabled_resource_completion.argument.name = "path";
  disabled_resource_completion.argument.value = "ignored";
  auto resource_completion =
      runtime.complete(std::move(disabled_resource_completion));
  require(!resource_completion.has_value(),
          "disabled upstream resource completion should fail");
  require(resource_completion.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::ResourceNotFound),
          "disabled resource completion should map to resource-not-found");
  require(resource_completion.error().message == "gateway upstream is disabled",
          "disabled resource completion should report stable routing error");
  require(resource_completion.error().detail == "disabled",
          "disabled resource completion should preserve upstream id context");
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
  require(!capabilities.completions.enabled,
          "invalid runtime config should not advertise completions");
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

  auto resources = runtime.list_resources();
  require(!resources.has_value(),
          "missing stdio process should fail resources/list");
  require_gateway_upstream_error(resources.error(), "missing");

  auto resource_templates = runtime.list_resource_templates();
  require(!resource_templates.has_value(),
          "missing stdio process should fail resources/templates/list");
  require_gateway_upstream_error(resource_templates.error(), "missing");

  auto prompts = runtime.list_prompts();
  require(!prompts.has_value(),
          "missing stdio process should fail prompts/list");
  require_gateway_upstream_error(prompts.error(), "missing");

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

void test_resources_list_fail_fast_after_partial_success() {
  auto config = make_stdio_config();

  mcp::gateway::UpstreamServer missing;
  missing.id = "missing";
  missing.transport = mcp::gateway::UpstreamTransportKind::process_stdio;
  missing.process_stdio.command =
      "cxxmcp-gateway-definitely-missing-upstream-executable";
  config.upstreams.push_back(std::move(missing));

  mcp::gateway::GatewayRuntime runtime(std::move(config));
  auto resources = runtime.list_resources();
  require(!resources.has_value(),
          "resources/list should fail fast when any enabled upstream fails");
  require_gateway_upstream_error(resources.error(), "missing");

  const auto states = runtime.upstream_states();
  const auto stdio = require_upstream_state(states, "stdio");
  require_status(stdio, UpstreamRuntimeStatus::healthy,
                 "successful upstream should keep healthy state after "
                 "fail-fast resources/list");
  require(stdio.capabilities.has_value(),
          "successful resource upstream should retain initialized capabilities");

  const auto failed = require_upstream_state(states, "missing");
  require_status(failed, UpstreamRuntimeStatus::degraded,
                 "failing upstream should be degraded after fail-fast "
                 "resources/list");
  require(failed.last_error.has_value(),
          "failing upstream should retain last error after fail-fast "
          "resources/list");
}

void test_resource_templates_list_fail_fast_after_partial_success() {
  auto config = make_stdio_config();

  mcp::gateway::UpstreamServer missing;
  missing.id = "missing";
  missing.transport = mcp::gateway::UpstreamTransportKind::process_stdio;
  missing.process_stdio.command =
      "cxxmcp-gateway-definitely-missing-upstream-executable";
  config.upstreams.push_back(std::move(missing));

  mcp::gateway::GatewayRuntime runtime(std::move(config));
  auto resource_templates = runtime.list_resource_templates();
  require(!resource_templates.has_value(),
          "resources/templates/list should fail fast when any enabled upstream "
          "fails");
  require_gateway_upstream_error(resource_templates.error(), "missing");

  const auto states = runtime.upstream_states();
  const auto stdio = require_upstream_state(states, "stdio");
  require_status(stdio, UpstreamRuntimeStatus::healthy,
                 "successful upstream should keep healthy state after "
                 "fail-fast resources/templates/list");
  require(stdio.capabilities.has_value(),
          "successful resource template upstream should retain initialized "
          "capabilities");

  const auto failed = require_upstream_state(states, "missing");
  require_status(failed, UpstreamRuntimeStatus::degraded,
                 "failing upstream should be degraded after fail-fast "
                 "resources/templates/list");
  require(failed.last_error.has_value(),
          "failing upstream should retain last error after fail-fast "
          "resources/templates/list");
}

void test_prompts_list_fail_fast_after_partial_success() {
  auto config = make_stdio_config();

  mcp::gateway::UpstreamServer missing;
  missing.id = "missing";
  missing.transport = mcp::gateway::UpstreamTransportKind::process_stdio;
  missing.process_stdio.command =
      "cxxmcp-gateway-definitely-missing-upstream-executable";
  config.upstreams.push_back(std::move(missing));

  mcp::gateway::GatewayRuntime runtime(std::move(config));
  auto prompts = runtime.list_prompts();
  require(!prompts.has_value(),
          "prompts/list should fail fast when any enabled upstream fails");
  require_gateway_upstream_error(prompts.error(), "missing");

  const auto states = runtime.upstream_states();
  const auto stdio = require_upstream_state(states, "stdio");
  require_status(stdio, UpstreamRuntimeStatus::healthy,
                 "successful upstream should keep healthy state after "
                 "fail-fast prompts/list");
  require(stdio.capabilities.has_value(),
          "successful prompt upstream should retain initialized capabilities");

  const auto failed = require_upstream_state(states, "missing");
  require_status(failed, UpstreamRuntimeStatus::degraded,
                 "failing upstream should be degraded after fail-fast "
                 "prompts/list");
  require(failed.last_error.has_value(),
          "failing upstream should retain last error after fail-fast "
          "prompts/list");
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

void test_first_call_upstream_mcp_error_records_capabilities() {
  mcp::gateway::GatewayRuntime runtime(make_stdio_config());

  auto failed = runtime.call_tool("stdio.fail", Json::object());
  require(!failed.has_value(), "first upstream MCP error should fail");
  require(failed.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::PermissionDenied),
          "first upstream MCP error code should be preserved");

  const auto state = require_upstream_state(runtime.upstream_states(), "stdio");
  require_status(state, UpstreamRuntimeStatus::degraded,
                 "first upstream MCP error should mark upstream degraded");
  require(state.last_error.has_value(),
          "first upstream MCP error should record last error");
  require(state.capabilities.has_value(),
          "initialized upstream capabilities should be retained when the "
          "routed call fails");
  require(state.capabilities->tools.enabled,
          "retained capabilities should include upstream tools support");
  require(state.capabilities->completions.enabled,
          "retained capabilities should include upstream completion support");

  const auto capabilities = runtime.server_capabilities();
  require(capabilities.tools.enabled,
          "retained capabilities should keep tools advertised");
  require(capabilities.resources.enabled,
          "retained capabilities should keep resources advertised");
  require(capabilities.prompts.enabled,
          "retained capabilities should keep prompts advertised");
  require(capabilities.completions.enabled,
          "retained capabilities should enable completion advertisement");
  require_mvp_server_capability_json_shape(capabilities, true);

  auto recovered =
      runtime.call_tool("stdio.echo", Json{{"value", "after-error"}});
  require(recovered.has_value(),
          "successful call after upstream MCP error should recover");
  require_text_result(*recovered, "after-error");
  const auto recovered_state =
      require_upstream_state(runtime.upstream_states(), "stdio");
  require_status(recovered_state, UpstreamRuntimeStatus::healthy,
                 "successful call after upstream MCP error should mark "
                 "upstream healthy");
  require(!recovered_state.last_error.has_value(),
          "successful call after upstream MCP error should clear last error");
  require(recovered_state.capabilities.has_value(),
          "successful recovery should keep initialized capabilities");
}

void test_stdio_upstream() {
  mcp::gateway::GatewayRuntime runtime(make_stdio_config());
  auto capabilities = runtime.server_capabilities();
  require(capabilities.tools.enabled,
          "runtime with enabled upstream should advertise tools");
  require(!capabilities.tools.list_changed,
          "runtime should not advertise tools/listChanged");
  require(capabilities.resources.enabled,
          "runtime with enabled upstream should advertise resources");
  require(!capabilities.resources.list_changed,
          "runtime should not advertise resources/listChanged");
  require(!capabilities.resources.subscribe,
          "runtime should not advertise resource subscriptions");
  require(capabilities.prompts.enabled,
          "runtime with enabled upstream should advertise prompts");
  require(!capabilities.prompts.list_changed,
          "runtime should not advertise prompts/listChanged");
  require(!capabilities.completions.enabled,
          "runtime should not advertise completions");
  require(!capabilities.tasks.has_value(),
          "runtime should not advertise tasks");

  const auto& initial =
      require_upstream_state(runtime.upstream_states(), "stdio");
  require_status(initial, UpstreamRuntimeStatus::configured,
                 "new runtime upstream should start configured");

  auto tools = runtime.list_tools();
  require(tools.has_value(), "stdio tools/list should succeed");
  require(has_tool(*tools, "stdio.echo"), "stdio tool should be exposed");
  const auto& echo_tool = require_tool(*tools, "stdio.echo");
  require(echo_tool.title == "Echo", "stdio tool title should be preserved");
  require(echo_tool.description == "Echoes the provided value",
          "stdio tool description should be preserved");
  require(echo_tool.input_schema.at("properties")
              .at("value")
              .at("description") == "Value to echo",
          "stdio tool input schema should be preserved");
  require(echo_tool.meta.has_value(), "stdio tool metadata should be present");
  require(echo_tool.meta->at("fixture") == "stdio",
          "stdio tool upstream metadata should be preserved");
  require(echo_tool.meta->at("preserve").get<bool>(),
          "stdio tool upstream metadata fields should be preserved");
  require(echo_tool.meta->at("gateway").at("upstreamId") == "stdio",
          "stdio tool metadata should include gateway upstream id");
  require(echo_tool.meta->at("gateway").at("upstreamToolName") == "echo",
          "stdio tool metadata should include gateway upstream tool name");
  const auto& listed = require_upstream_state(runtime.upstream_states(), "stdio");
  require_status(listed, UpstreamRuntimeStatus::healthy,
                 "successful tools/list should mark upstream healthy");
  require(listed.capabilities.has_value(),
          "successful initialize should record upstream capabilities");

  auto called =
      runtime.call_tool("stdio.echo", Json{{"value", "from-stdio"}});
  require(called.has_value(), "stdio tools/call should succeed");
  require_text_result(*called, "from-stdio");

  const auto stdio_resource_uri =
      mcp::gateway::GatewayRouter::expose_resource_uri(
          "stdio", "file:///fixture/readme.txt");
  auto resources = runtime.list_resources();
  require(resources.has_value(), "stdio resources/list should succeed");
  require(has_resource(*resources, stdio_resource_uri),
          "stdio resource should be exposed");
  const auto& readme = require_resource(*resources, stdio_resource_uri);
  require(readme.title == "Fixture Readme",
          "stdio resource title should be preserved");
  require(readme.name == "fixture-readme",
          "stdio resource name should be preserved");
  require(readme.description == "Fixture readme resource",
          "stdio resource description should be preserved");
  require(readme.mime_type == "text/plain",
          "stdio resource MIME type should be preserved");
  require(readme.meta.has_value(), "stdio resource metadata should be present");
  require(readme.meta->at("fixture") == "stdio",
          "stdio resource upstream metadata should be preserved");
  require(readme.meta->at("preserve").get<bool>(),
          "stdio resource upstream metadata fields should be preserved");
  require(readme.meta->at("gateway").at("upstreamId") == "stdio",
          "stdio resource metadata should include gateway upstream id");
  require(readme.meta->at("gateway").at("upstreamResourceUri") ==
              "file:///fixture/readme.txt",
          "stdio resource metadata should include upstream resource URI");

  auto read = runtime.read_resource(stdio_resource_uri);
  require(read.has_value(), "stdio resources/read should succeed");
  require_text_resource(*read, stdio_resource_uri,
                        "hello from stdio resource");

  const auto stdio_template_uri =
      mcp::gateway::GatewayRouter::expose_resource_template_uri(
          "stdio", "file:///fixture/{path}");
  auto resource_templates = runtime.list_resource_templates();
  require(resource_templates.has_value(),
          "stdio resources/templates/list should succeed");
  require(has_resource_template(*resource_templates, stdio_template_uri),
          "stdio resource template should be exposed");
  const auto& template_file =
      require_resource_template(*resource_templates, stdio_template_uri);
  require(template_file.title == "Fixture File",
          "stdio resource template title should be preserved");
  require(template_file.name == "fixture-file",
          "stdio resource template name should be preserved");
  require(template_file.description == "Fixture file by path",
          "stdio resource template description should be preserved");
  require(template_file.mime_type == "text/plain",
          "stdio resource template MIME type should be preserved");
  require(template_file.meta.has_value(),
          "stdio resource template metadata should be present");
  require(template_file.meta->at("fixture") == "stdio",
          "stdio resource template upstream metadata should be preserved");
  require(template_file.meta->at("gateway").at("upstreamId") == "stdio",
          "stdio resource template metadata should include gateway upstream id");
  require(template_file.meta->at("gateway")
              .at("upstreamResourceTemplateUri") == "file:///fixture/{path}",
          "stdio resource template metadata should include upstream URI "
          "template");

  auto prompts = runtime.list_prompts();
  require(prompts.has_value(), "stdio prompts/list should succeed");
  require(has_prompt(*prompts, "stdio.summarize"),
          "stdio prompt should be exposed");
  const auto& summary = require_prompt(*prompts, "stdio.summarize");
  require(summary.title == "Fixture Summary",
          "stdio prompt title should be preserved");
  require(summary.description == "Summarize fixture text",
          "stdio prompt description should be preserved");
  require(!summary.arguments.empty(),
          "stdio prompt arguments should be preserved");
  require(summary.arguments.front().name == "text",
          "stdio prompt argument name should be preserved");
  require(summary.meta.has_value(), "stdio prompt metadata should be present");
  require(summary.meta->at("fixture") == "stdio",
          "stdio prompt upstream metadata should be preserved");
  require(summary.meta->at("gateway").at("upstreamId") == "stdio",
          "stdio prompt metadata should include gateway upstream id");
  require(summary.meta->at("gateway").at("upstreamPromptName") ==
              "summarize",
          "stdio prompt metadata should include upstream prompt name");

  auto prompt =
      runtime.get_prompt("stdio.summarize", Json{{"text", "from-stdio"}});
  require(prompt.has_value(), "stdio prompts/get should succeed");
  require(prompt->description == "Summarize fixture text",
          "stdio prompt description should be returned");
  require_text_prompt(*prompt, "Summarize from-stdio");

  auto unknown = runtime.call_tool("missing.echo", Json::object());
  require(!unknown.has_value(), "unknown upstream should fail");
  require(unknown.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::ToolNotFound),
          "unknown upstream should map to tool-not-found");
  require(unknown.error().message == "gateway upstream not found",
          "unknown upstream should report stable routing error message");
  require(unknown.error().detail == "missing",
          "unknown upstream error should preserve upstream id context");

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

  const auto missing_resource_uri =
      mcp::gateway::GatewayRouter::expose_resource_uri(
          "missing", "file:///fixture/readme.txt");
  auto missing_resource = runtime.read_resource(missing_resource_uri);
  require(!missing_resource.has_value(),
          "unknown upstream resource should fail");
  require(missing_resource.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::ResourceNotFound),
          "unknown upstream resource should map to resource-not-found");
  require(missing_resource.error().message == "gateway upstream not found",
          "unknown resource upstream should report stable routing error");
  require(missing_resource.error().detail == "missing",
          "unknown resource upstream error should preserve upstream id");

  const auto fail_resource_uri =
      mcp::gateway::GatewayRouter::expose_resource_uri(
          "stdio", "file:///fixture/fail.txt");
  auto resource_error = runtime.read_resource(fail_resource_uri);
  require(!resource_error.has_value(), "upstream resource MCP error should fail");
  require(resource_error.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::PermissionDenied),
          "upstream resource MCP error code should be preserved");
  require(resource_error.error().message == "resource denied",
          "upstream resource MCP error message should be preserved");
  require(resource_error.error().detail.find("resource detail") !=
              std::string::npos,
          "upstream resource MCP error detail should be preserved");
  require_gateway_upstream_error(resource_error.error(), "stdio");

  auto missing_prompt =
      runtime.get_prompt("missing.summarize", Json{{"text", "ignored"}});
  require(!missing_prompt.has_value(), "unknown upstream prompt should fail");
  require(missing_prompt.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "unknown upstream prompt should map to invalid params");
  require(missing_prompt.error().message == "gateway upstream not found",
          "unknown prompt upstream should report stable routing error");
  require(missing_prompt.error().detail == "missing",
          "unknown prompt upstream error should preserve upstream id");

  auto prompt_error =
      runtime.get_prompt("stdio.fail-prompt", Json::object());
  require(!prompt_error.has_value(), "upstream prompt MCP error should fail");
  require(prompt_error.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::PermissionDenied),
          "upstream prompt MCP error code should be preserved");
  require(prompt_error.error().message == "prompt denied",
          "upstream prompt MCP error message should be preserved");
  require(prompt_error.error().detail.find("prompt detail") !=
              std::string::npos,
          "upstream prompt MCP error detail should be preserved");
  require_gateway_upstream_error(prompt_error.error(), "stdio");

  auto stopped = runtime.stop();
  require(stopped.has_value(), "runtime stop should succeed without endpoint");
  const auto& stopped_state =
      require_upstream_state(runtime.upstream_states(), "stdio");
  require_status(stopped_state, UpstreamRuntimeStatus::stopped,
                 "runtime stop should mark upstream stopped");
}

void test_upstream_client_capabilities_are_minimal() {
  mcp::gateway::GatewayRuntime runtime(make_stdio_config());

  auto result =
      runtime.call_tool("stdio.client-capabilities", Json::object());
  require(result.has_value(),
          "upstream client capability inspection should succeed");
  require_text_result(*result, "roots=0;sampling=0;elicitation=0;tasks=0");

  const auto state = require_upstream_state(runtime.upstream_states(), "stdio");
  require(state.capabilities.has_value(),
          "client capability inspection should record upstream capabilities");
  require(state.active_calls == 0,
          "client capability inspection should not leave active calls");
}

void require_persistent_pool_wait_timeout(const mcp::core::Error& error,
                                          std::string_view upstream_id) {
  require(error.code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidRequest),
          "persistent pool wait timeout should use InvalidRequest");
  require(error.category == "gateway",
          "persistent pool wait timeout should remain gateway-owned");
  require(error.message.find("pool wait timed out") != std::string::npos,
          "persistent pool wait timeout should explain pool wait timeout");
  require(error.detail == upstream_id,
          "persistent pool wait timeout should preserve upstream context");
}

void require_active_call_drain_timeout(const mcp::core::Error& error) {
  require(error.code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidRequest),
          "active call drain timeout should use InvalidRequest");
  require(error.category == "gateway",
          "active call drain timeout should remain gateway-owned");
  require(error.message.find("active upstream calls") != std::string::npos,
          "active call drain timeout should explain active upstream calls");
  require(error.detail == "active upstream calls",
          "active call drain timeout should preserve operation context");
}

void test_runtime_observer_reports_status_without_logger_dependency() {
  std::vector<mcp::gateway::GatewayRuntimeEvent> events;
  mcp::gateway::GatewayRuntimeOptions options;
  options.observer = [&](const mcp::gateway::GatewayRuntimeEvent& event) {
    events.push_back(event);
  };

  mcp::gateway::GatewayRuntime runtime(make_stdio_config(),
                                       std::move(options));

  auto tools = runtime.list_tools();
  require(tools.has_value(), "observed runtime tools/list should succeed");
  require(has_runtime_event(events,
                            mcp::gateway::GatewayRuntimeEventKind::
                                upstream_status_changed,
                            "stdio", UpstreamRuntimeStatus::connecting),
          "observer should see upstream connecting status");
  require(has_runtime_event(events,
                            mcp::gateway::GatewayRuntimeEventKind::
                                upstream_status_changed,
                            "stdio", UpstreamRuntimeStatus::initialized),
          "observer should see upstream initialized status");
  require(has_runtime_event(events,
                            mcp::gateway::GatewayRuntimeEventKind::
                                upstream_status_changed,
                            "stdio", UpstreamRuntimeStatus::healthy),
          "observer should see upstream healthy status");

  auto failed = runtime.call_tool("stdio.fail", Json::object());
  require(!failed.has_value(), "observed runtime upstream error should fail");
  const auto degraded = std::find_if(
      events.begin(), events.end(), [](const auto& event) {
        return event.kind ==
                   mcp::gateway::GatewayRuntimeEventKind::
                       upstream_status_changed &&
               event.upstream_id == "stdio" &&
               event.upstream_status == UpstreamRuntimeStatus::degraded &&
               event.error.has_value();
      });
  require(degraded != events.end(),
          "observer should see degraded status with stable error");
  require_gateway_upstream_error(*degraded->error, "stdio");

  auto stopped = runtime.stop();
  require(stopped.has_value(), "observed runtime stop should succeed");
  require(has_runtime_event(
              events, mcp::gateway::GatewayRuntimeEventKind::runtime_stopping),
          "observer should see runtime stopping event");
  require(has_runtime_event(
              events, mcp::gateway::GatewayRuntimeEventKind::runtime_stopped),
          "observer should see runtime stopped event");
  require(has_runtime_event(events,
                            mcp::gateway::GatewayRuntimeEventKind::
                                upstream_status_changed,
                            "stdio", UpstreamRuntimeStatus::stopped),
          "observer should see upstream stopped status");

  mcp::gateway::GatewayRuntimeOptions throwing_options;
  throwing_options.observer =
      [](const mcp::gateway::GatewayRuntimeEvent&) { throw std::runtime_error("ignored"); };
  mcp::gateway::GatewayRuntime throwing_runtime(make_disabled_config("quiet"),
                                                std::move(throwing_options));
  auto quiet_tools = throwing_runtime.list_tools();
  require(quiet_tools.has_value(),
          "observer exceptions should not affect runtime operations");
  auto quiet_stop = throwing_runtime.stop();
  require(quiet_stop.has_value(),
          "observer exceptions should not affect runtime shutdown");
}

void test_capability_advertisement_uses_initialized_upstream_cache() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "tools_only";
  config.upstreams.front().process_stdio.args = {"--tools-only"};
  mcp::gateway::GatewayRuntime runtime(std::move(config));

  const auto before = runtime.server_capabilities();
  require(before.tools.enabled,
          "before upstream discovery, runtime should keep configured tools "
          "advertisement");
  require(before.resources.enabled,
          "before upstream discovery, runtime should keep configured resources "
          "advertisement");
  require(before.prompts.enabled,
          "before upstream discovery, runtime should keep configured prompts "
          "advertisement");
  require(!before.completions.enabled,
          "runtime should not advertise completions before discovery");
  require_mvp_server_capability_json_shape(before);

  auto refreshed = runtime.refresh_upstream_capabilities();
  require(refreshed.has_value(),
          "tools-only upstream capability refresh should succeed");

  const auto state =
      require_upstream_state(runtime.upstream_states(), "tools_only");
  require(state.capabilities.has_value(),
          "tools-only capability refresh should record capabilities");
  require(state.capabilities->tools.enabled,
          "tools-only upstream should advertise tools upstream");
  require(!state.capabilities->resources.enabled,
          "tools-only upstream should not advertise resources upstream");
  require(!state.capabilities->prompts.enabled,
          "tools-only upstream should not advertise prompts upstream");
  require_status(state, UpstreamRuntimeStatus::healthy,
                 "capability refresh should leave upstream healthy");
  require(state.active_calls == 0,
          "capability refresh should not leave active upstream calls");

  const auto after = runtime.server_capabilities();
  require(after.tools.enabled,
          "runtime should keep tools advertised from initialized upstream "
          "capabilities");
  require(!after.resources.enabled,
          "runtime should drop resources advertisement when initialized "
          "upstream capabilities do not support resources");
  require(!after.prompts.enabled,
          "runtime should drop prompts advertisement when initialized upstream "
          "capabilities do not support prompts");
  require(!after.completions.enabled,
          "runtime should not advertise completions after discovery");
  require(!after.tasks.has_value(),
          "runtime should not advertise tasks after discovery");
  require_mvp_server_capability_json_shape(after);

  mcp::protocol::CompleteParams unsupported_completion;
  unsupported_completion.ref =
      mcp::protocol::prompt_completion_reference("tools_only.echo");
  unsupported_completion.argument.name = "value";
  unsupported_completion.argument.value = "fixture";
  auto unsupported = runtime.complete(std::move(unsupported_completion));
  require(!unsupported.has_value(),
          "tools-only upstream completion should be rejected by gateway");
  require_gateway_unsupported_capability_error(unsupported.error(),
                                              "tools_only");

  const auto unsupported_resource_uri =
      mcp::gateway::GatewayRouter::expose_resource_uri(
          "tools_only", "file:///fixture/readme.txt");
  auto unsupported_resource = runtime.read_resource(unsupported_resource_uri);
  require(!unsupported_resource.has_value(),
          "tools-only upstream resource read should be rejected by gateway");
  require_gateway_unsupported_capability_error(unsupported_resource.error(),
                                              "tools_only");

  auto unsupported_prompt =
      runtime.get_prompt("tools_only.summarize", Json{{"text", "fixture"}});
  require(!unsupported_prompt.has_value(),
          "tools-only upstream prompt get should be rejected by gateway");
  require_gateway_unsupported_capability_error(unsupported_prompt.error(),
                                              "tools_only");

  const auto after_unsupported =
      require_upstream_state(runtime.upstream_states(), "tools_only");
  require_status(after_unsupported, UpstreamRuntimeStatus::healthy,
                 "unsupported completion should not degrade tools-only "
                 "upstream");
  require(after_unsupported.capabilities.has_value(),
          "unsupported completion should retain initialized capabilities");
  require(!after_unsupported.capabilities->completions.enabled,
          "unsupported completion should retain completion-negative cache");
  require(!after_unsupported.capabilities->resources.enabled,
          "unsupported resource should retain resource-negative cache");
  require(!after_unsupported.capabilities->prompts.enabled,
          "unsupported prompt should retain prompt-negative cache");
  require(!after_unsupported.last_error.has_value(),
          "unsupported capability checks should not record an upstream error");
  require(after_unsupported.active_calls == 0,
          "unsupported capability checks should not leave active calls");

  auto tools = runtime.list_tools();
  require(tools.has_value(), "tools-only upstream tools/list should succeed");
  require(has_tool(*tools, "tools_only.echo"),
          "tools-only upstream tool should still be exposed");
}

void test_capability_advertisement_unions_multiple_upstream_caches() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "full";

  mcp::gateway::UpstreamServer tools_only;
  tools_only.id = "tools_only";
  tools_only.transport = mcp::gateway::UpstreamTransportKind::process_stdio;
  tools_only.process_stdio.command = CXXMCP_GATEWAY_STDIO_FIXTURE;
  tools_only.process_stdio.args = {"--tools-only"};
  config.upstreams.push_back(std::move(tools_only));

  mcp::gateway::GatewayRuntime runtime(std::move(config));

  const auto before = runtime.server_capabilities();
  require(before.tools.enabled,
          "multi-upstream runtime should advertise configured tools before "
          "discovery");
  require(before.resources.enabled,
          "multi-upstream runtime should advertise configured resources before "
          "discovery");
  require(before.prompts.enabled,
          "multi-upstream runtime should advertise configured prompts before "
          "discovery");
  require(!before.completions.enabled,
          "multi-upstream runtime should not advertise completions before "
          "discovery");

  for (const auto& state : runtime.upstream_states()) {
    require(!state.capabilities.has_value(),
            "server_capabilities should not initialize upstream caches");
    require(state.active_calls == 0,
            "server_capabilities should not create active upstream calls");
    require_status(state, UpstreamRuntimeStatus::configured,
                   "server_capabilities should leave upstreams configured");
  }

  auto refreshed = runtime.refresh_upstream_capabilities();
  require(refreshed.has_value(),
          "multi-upstream capability refresh should succeed");

  const auto states = runtime.upstream_states();
  const auto full = require_upstream_state(states, "full");
  require(full.capabilities.has_value(),
          "full upstream capability refresh should record capabilities");
  require(full.capabilities->tools.enabled,
          "full upstream should advertise tools upstream");
  require(full.capabilities->resources.enabled,
          "full upstream should advertise resources upstream");
  require(full.capabilities->prompts.enabled,
          "full upstream should advertise prompts upstream");
  require(full.capabilities->completions.enabled,
          "full upstream should advertise completions upstream");
  require_status(full, UpstreamRuntimeStatus::healthy,
                 "full upstream should be healthy after capability refresh");
  require(full.active_calls == 0,
          "full upstream capability refresh should leave no active calls");

  const auto tools = require_upstream_state(states, "tools_only");
  require(tools.capabilities.has_value(),
          "tools-only upstream capability refresh should record capabilities");
  require(tools.capabilities->tools.enabled,
          "tools-only upstream should advertise tools upstream");
  require(!tools.capabilities->resources.enabled,
          "tools-only upstream should not advertise resources upstream");
  require(!tools.capabilities->prompts.enabled,
          "tools-only upstream should not advertise prompts upstream");
  require(!tools.capabilities->completions.enabled,
          "tools-only upstream should not advertise completions upstream");
  require_status(tools, UpstreamRuntimeStatus::healthy,
                 "tools-only upstream should be healthy after capability "
                 "refresh");
  require(tools.active_calls == 0,
          "tools-only upstream capability refresh should leave no active calls");

  const auto after = runtime.server_capabilities();
  require(after.tools.enabled,
          "runtime should advertise tools from multi-upstream capability "
          "union");
  require(after.resources.enabled,
          "runtime should advertise resources when any upstream supports them");
  require(!after.resources.list_changed,
          "multi-upstream capability union should not add resources/listChanged");
  require(!after.resources.subscribe,
          "multi-upstream capability union should not add subscriptions");
  require(after.prompts.enabled,
          "runtime should advertise prompts when any upstream supports them");
  require(!after.prompts.list_changed,
          "multi-upstream capability union should not add prompts/listChanged");
  require(after.completions.enabled,
          "runtime should advertise completions when any upstream supports "
          "them");
  require(!after.tasks.has_value(),
          "multi-upstream capability union should not advertise tasks");
  require_mvp_server_capability_json_shape(after, true);

  auto listed_tools = runtime.list_tools();
  require(listed_tools.has_value(),
          "capability-aware tools/list should keep tools-capable upstreams");
  require(has_tool(*listed_tools, "full.echo"),
          "tools/list should include full upstream tool");
  require(has_tool(*listed_tools, "tools_only.echo"),
          "tools/list should include tools-only upstream tool");

  auto listed_resources = runtime.list_resources();
  require(listed_resources.has_value(),
          "capability-aware resources/list should skip tools-only upstream");
  require(has_resource(*listed_resources,
                       mcp::gateway::GatewayRouter::expose_resource_uri(
                           "full", "file:///fixture/readme.txt")),
          "resources/list should include resource-capable upstream");

  auto listed_templates = runtime.list_resource_templates();
  require(listed_templates.has_value(),
          "capability-aware resources/templates/list should skip tools-only "
          "upstream");
  require(has_resource_template(
              *listed_templates,
              mcp::gateway::GatewayRouter::expose_resource_template_uri(
                  "full", "file:///fixture/{path}")),
          "resources/templates/list should include resource-capable upstream");

  auto listed_prompts = runtime.list_prompts();
  require(listed_prompts.has_value(),
          "capability-aware prompts/list should skip tools-only upstream");
  require(has_prompt(*listed_prompts, "full.summarize"),
          "prompts/list should include prompt-capable upstream");

  const auto after_listing = runtime.upstream_states();
  const auto listed_tools_only =
      require_upstream_state(after_listing, "tools_only");
  require_status(listed_tools_only, UpstreamRuntimeStatus::healthy,
                 "skipped capability-negative upstream should stay healthy");
  require(!listed_tools_only.last_error.has_value(),
          "skipped capability-negative upstream should not record errors");
}

void test_hosted_capability_advertisement_uses_refresh_cache() {
  const auto kPort = find_available_loopback_port();

  auto config = make_stdio_config();
  config.upstreams.front().id = "tools_only";
  config.upstreams.front().process_stdio.args = {"--tools-only"};
  mcp::gateway::GatewayRuntime gateway(std::move(config));

  auto refreshed = gateway.refresh_upstream_capabilities();
  require(refreshed.has_value(),
          "hosted tools-only capability refresh should succeed");

  auto started = gateway.start_http(
      {.host = "127.0.0.1", .port = kPort, .path = "/mcp"});
  require(started.has_value(),
          "hosted tools-only gateway endpoint should start");

  auto client = mcp::ClientPeer::builder()
                    .streamable_http("http://127.0.0.1:" +
                                     std::to_string(kPort) + "/mcp")
                    .build();
  require(client.has_value(), "hosted tools-only client should build");

  auto running_client = mcp::serve(std::move(*client));
  require(running_client.has_value(),
          "hosted tools-only client service should start");

  auto initialized = running_client->peer().initialize(
      "cxxmcp-gateway-tools-only-client", "1.0.0");
  require(initialized.has_value(),
          "hosted tools-only client should initialize");

  auto capabilities = running_client->peer().server_capabilities();
  require(capabilities.has_value(),
          "hosted tools-only gateway should advertise explicit capabilities");
  require(capabilities->tools.enabled,
          "hosted tools-only gateway should advertise tools");
  require(!capabilities->resources.enabled,
          "hosted tools-only gateway should not advertise resources");
  require(!capabilities->prompts.enabled,
          "hosted tools-only gateway should not advertise prompts");
  require(!capabilities->completions.enabled,
          "hosted tools-only gateway should not advertise completions");
  require(!capabilities->tasks.has_value(),
          "hosted tools-only gateway should not advertise tasks");
  require_mvp_server_capability_json_shape(*capabilities);

  auto stopped_client = running_client->stop();
  require(stopped_client.has_value(),
          "hosted tools-only client should stop");
  auto stopped_gateway = gateway.stop();
  require(stopped_gateway.has_value(),
          "hosted tools-only gateway should stop");
}

void test_hosted_capability_advertisement_snapshots_at_start() {
  const auto kPort = find_available_loopback_port();

  auto config = make_stdio_config();
  config.upstreams.front().id = "tools_only";
  config.upstreams.front().process_stdio.args = {"--tools-only"};
  mcp::gateway::GatewayRuntime gateway(std::move(config));

  const auto before_start = gateway.server_capabilities();
  require(before_start.tools.enabled,
          "hosted snapshot gateway should advertise configured tools before "
          "discovery");
  require(before_start.resources.enabled,
          "hosted snapshot gateway should advertise configured resources "
          "before discovery");
  require(before_start.prompts.enabled,
          "hosted snapshot gateway should advertise configured prompts before "
          "discovery");

  auto started = gateway.start_http(
      {.host = "127.0.0.1", .port = kPort, .path = "/mcp"});
  require(started.has_value(),
          "hosted snapshot gateway endpoint should start");

  auto refreshed = gateway.refresh_upstream_capabilities();
  require(refreshed.has_value(),
          "hosted snapshot capability refresh after start should succeed");

  const auto refreshed_capabilities = gateway.server_capabilities();
  require(refreshed_capabilities.tools.enabled,
          "refreshed hosted snapshot runtime should keep tools advertised");
  require(!refreshed_capabilities.resources.enabled,
          "refreshed hosted snapshot runtime should drop resources");
  require(!refreshed_capabilities.prompts.enabled,
          "refreshed hosted snapshot runtime should drop prompts");
  require_mvp_server_capability_json_shape(refreshed_capabilities);

  auto client = mcp::ClientPeer::builder()
                    .streamable_http("http://127.0.0.1:" +
                                     std::to_string(kPort) + "/mcp")
                    .build();
  require(client.has_value(), "hosted snapshot client should build");

  auto running_client = mcp::serve(std::move(*client));
  require(running_client.has_value(),
          "hosted snapshot client service should start");

  auto initialized = running_client->peer().initialize(
      "cxxmcp-gateway-snapshot-client", "1.0.0");
  require(initialized.has_value(),
          "hosted snapshot client should initialize");

  auto hosted_capabilities = running_client->peer().server_capabilities();
  require(hosted_capabilities.has_value(),
          "hosted snapshot client should receive capabilities");
  require(hosted_capabilities->tools.enabled,
          "hosted snapshot should keep tools from start-time snapshot");
  require(hosted_capabilities->resources.enabled,
          "hosted snapshot should keep resources from start-time snapshot");
  require(hosted_capabilities->prompts.enabled,
          "hosted snapshot should keep prompts from start-time snapshot");
  require(!hosted_capabilities->completions.enabled,
          "hosted snapshot should not invent completion support");
  require_mvp_server_capability_json_shape(*hosted_capabilities);

  auto stopped_client = running_client->stop();
  require(stopped_client.has_value(),
          "hosted snapshot client should stop");
  auto stopped_gateway = gateway.stop();
  require(stopped_gateway.has_value(),
          "hosted snapshot gateway should stop");
}

void test_completion_routes_to_stdio_upstream() {
  mcp::gateway::GatewayRuntime runtime(make_stdio_config());

  auto refreshed = runtime.refresh_upstream_capabilities();
  require(refreshed.has_value(),
          "completion test capability refresh should succeed");
  auto capabilities = runtime.server_capabilities();
  require(capabilities.completions.enabled,
          "runtime should advertise completions after refreshed upstream "
          "capabilities support completion");
  require_mvp_server_capability_json_shape(capabilities, true);

  mcp::protocol::CompleteParams prompt_completion;
  prompt_completion.ref =
      mcp::protocol::prompt_completion_reference("stdio.summarize");
  prompt_completion.argument.name = "text";
  prompt_completion.argument.value = "fixture";

  auto prompt_result = runtime.complete(std::move(prompt_completion));
  require(prompt_result.has_value(), "prompt completion should route");
  require(prompt_result->completion.values.size() == 2,
          "prompt completion should preserve upstream candidates");
  require(prompt_result->completion.values[0] == "fixture-summary",
          "prompt completion should rewrite prompt ref to upstream name");
  require(prompt_result->completion.total == 2,
          "prompt completion should preserve total");
  require(prompt_result->completion.has_more == false,
          "prompt completion should preserve hasMore");

  mcp::protocol::CompleteParams resource_completion;
  resource_completion.ref = mcp::protocol::resource_completion_reference(
      mcp::gateway::GatewayRouter::expose_resource_template_uri(
          "stdio", "file:///fixture/{path}"));
  resource_completion.argument.name = "path";
  resource_completion.argument.value = "docs/";

  auto resource_result = runtime.complete(std::move(resource_completion));
  require(resource_result.has_value(),
          "resource template completion should route");
  require(resource_result->completion.values.size() == 2,
          "resource completion should preserve upstream candidates");
  require(resource_result->completion.values[0] == "docs/readme.txt",
          "resource completion should rewrite resource ref to upstream URI "
          "template");

  mcp::protocol::CompleteParams invalid_completion;
  invalid_completion.ref =
      mcp::protocol::prompt_completion_reference("bad");
  invalid_completion.argument.name = "text";
  invalid_completion.argument.value = "fixture";
  auto invalid = runtime.complete(std::move(invalid_completion));
  require(!invalid.has_value(), "invalid gateway completion ref should fail");
  require(invalid.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "invalid gateway completion ref should map to invalid params");

  mcp::protocol::CompleteParams unknown_prompt_completion;
  unknown_prompt_completion.ref =
      mcp::protocol::prompt_completion_reference("missing.summarize");
  unknown_prompt_completion.argument.name = "text";
  unknown_prompt_completion.argument.value = "fixture";
  auto unknown_prompt = runtime.complete(std::move(unknown_prompt_completion));
  require(!unknown_prompt.has_value(),
          "unknown upstream prompt completion should fail");
  require(unknown_prompt.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "unknown prompt completion should map to invalid params");
  require(unknown_prompt.error().message == "gateway upstream not found",
          "unknown prompt completion should report stable routing error");
  require(unknown_prompt.error().detail == "missing",
          "unknown prompt completion should preserve upstream id context");

  mcp::protocol::CompleteParams unknown_resource_completion;
  unknown_resource_completion.ref =
      mcp::protocol::resource_completion_reference(
          mcp::gateway::GatewayRouter::expose_resource_uri(
              "missing", "file:///fixture/{path}"));
  unknown_resource_completion.argument.name = "path";
  unknown_resource_completion.argument.value = "fixture";
  auto unknown_resource =
      runtime.complete(std::move(unknown_resource_completion));
  require(!unknown_resource.has_value(),
          "unknown upstream resource completion should fail");
  require(unknown_resource.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::ResourceNotFound),
          "unknown resource completion should map to resource-not-found");
  require(unknown_resource.error().message == "gateway upstream not found",
          "unknown resource completion should report stable routing error");
  require(unknown_resource.error().detail == "missing",
          "unknown resource completion should preserve upstream id context");

  mcp::protocol::CompleteParams unsupported_completion_ref;
  unsupported_completion_ref.ref.type = "ref/unknown";
  unsupported_completion_ref.argument.name = "value";
  unsupported_completion_ref.argument.value = "fixture";
  auto unsupported_ref =
      runtime.complete(std::move(unsupported_completion_ref));
  require(!unsupported_ref.has_value(),
          "unsupported completion ref type should fail");
  require(unsupported_ref.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "unsupported completion ref type should map to invalid params");
  require(unsupported_ref.error().message ==
              "gateway completion ref type is not supported",
          "unsupported completion ref type should report stable routing error");
  require(unsupported_ref.error().detail == "ref/unknown",
          "unsupported completion ref type should preserve ref context");
}

void test_hosted_completion_routes_after_capability_refresh() {
  const auto kPort = find_available_loopback_port();

  mcp::gateway::GatewayRuntime gateway(make_stdio_config());
  auto refreshed = gateway.refresh_upstream_capabilities();
  require(refreshed.has_value(),
          "hosted completion capability refresh should succeed");

  auto started = gateway.start_http(
      {.host = "127.0.0.1", .port = kPort, .path = "/mcp"});
  require(started.has_value(),
          "hosted completion gateway endpoint should start");

  auto client = mcp::ClientPeer::builder()
                    .streamable_http("http://127.0.0.1:" +
                                     std::to_string(kPort) + "/mcp")
                    .build();
  require(client.has_value(), "hosted completion client should build");

  auto running_client = mcp::serve(std::move(*client));
  require(running_client.has_value(),
          "hosted completion client service should start");

  auto initialized = running_client->peer().initialize(
      "cxxmcp-gateway-completion-client", "1.0.0");
  require(initialized.has_value(),
          "hosted completion client should initialize");
  auto capabilities = running_client->peer().server_capabilities();
  require(capabilities.has_value(),
          "hosted completion gateway should advertise capabilities");
  require(capabilities->completions.enabled,
          "hosted completion gateway should advertise completions after "
          "refresh");
  require_mvp_server_capability_json_shape(*capabilities, true);
  auto notified = running_client->peer().notify_initialized();
  require(notified.has_value(),
          "hosted completion initialized notification should work");

  mcp::protocol::CompleteParams completion;
  completion.ref =
      mcp::protocol::prompt_completion_reference("stdio.summarize");
  completion.argument.name = "text";
  completion.argument.value = "hosted";
  auto result = running_client->peer().complete(completion);
  require(result.has_value(), "hosted completion should route");
  require(result->completion.values.size() == 2,
          "hosted completion should preserve candidates");
  require(result->completion.values[0] == "hosted-summary",
          "hosted completion should rewrite prompt ref to upstream name");
  require(result->completion.total == 2,
          "hosted prompt completion should preserve total");
  require(result->completion.has_more == false,
          "hosted prompt completion should preserve hasMore");

  mcp::protocol::CompleteParams resource_completion;
  resource_completion.ref = mcp::protocol::resource_completion_reference(
      mcp::gateway::GatewayRouter::expose_resource_template_uri(
          "stdio", "file:///fixture/{path}"));
  resource_completion.argument.name = "path";
  resource_completion.argument.value = "hosted/";
  auto resource_result = running_client->peer().complete(resource_completion);
  require(resource_result.has_value(),
          "hosted resource template completion should route");
  require(resource_result->completion.values.size() == 2,
          "hosted resource completion should preserve candidates");
  require(resource_result->completion.values[0] == "hosted/readme.txt",
          "hosted resource completion should rewrite resource ref to "
          "upstream URI template");
  require(resource_result->completion.total == 2,
          "hosted resource completion should preserve total");
  require(resource_result->completion.has_more == false,
          "hosted resource completion should preserve hasMore");

  const auto state = require_upstream_state(gateway.upstream_states(), "stdio");
  require(state.active_calls == 0,
          "hosted completions should not leave active upstream calls");

  auto stopped_client = running_client->stop();
  require(stopped_client.has_value(), "hosted completion client should stop");
  auto stopped_gateway = gateway.stop();
  require(stopped_gateway.has_value(), "hosted completion gateway should stop");
}

void test_repeated_stdio_calls_to_one_upstream() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "repeat";
  const auto marker_path =
      std::filesystem::temp_directory_path() /
      ("cxxmcp_gateway_stdio_repeat_marker_" +
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

  std::exception_ptr first_error;
  std::thread first_worker([&] {
    try {
      auto first = runtime.call_tool("repeat.slow", Json{{"sleepMs", 250}});
      require(first.has_value(), "first repeated stdio call should succeed");
      require_text_result(*first, "slow-done");
    } catch (...) {
      first_error = std::current_exception();
    }
  });

  bool observed_first_marker = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (std::filesystem::exists(marker_path)) {
      observed_first_marker = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(observed_first_marker,
          "first repeated stdio call should create child process marker");

  first_worker.join();
  if (first_error) {
    std::rethrow_exception(first_error);
  }

  bool removed_first_marker = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (!std::filesystem::exists(marker_path)) {
      removed_first_marker = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(removed_first_marker,
          "first repeated stdio call should clean up child process marker");

  auto first_state = require_upstream_state(runtime.upstream_states(), "repeat");
  require(first_state.active_calls == 0,
          "first repeated call should leave no active upstream calls");
  require_status(first_state, UpstreamRuntimeStatus::healthy,
                 "first repeated call should leave upstream healthy");
  require(first_state.capabilities.has_value(),
          "first repeated call should record upstream capabilities");

  auto second = runtime.call_tool("repeat.echo", Json{{"value", "second"}});
  require(second.has_value(), "second repeated stdio call should succeed");
  require_text_result(*second, "second");

  auto second_state = require_upstream_state(runtime.upstream_states(), "repeat");
  require(second_state.active_calls == 0,
          "second repeated call should leave no active upstream calls");
  require_status(second_state, UpstreamRuntimeStatus::healthy,
                 "second repeated call should leave upstream healthy");
  require(second_state.capabilities.has_value(),
          "second repeated call should retain upstream capabilities");
  require(!second_state.last_error.has_value(),
          "successful repeated calls should leave no upstream error");
  require(!std::filesystem::exists(marker_path),
          "second repeated stdio call should leave child process marker "
          "cleaned up");
}

void test_persistent_stdio_session_reuses_upstream_process() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "persistent";
  const auto marker_path =
      std::filesystem::temp_directory_path() /
      ("cxxmcp_gateway_stdio_persistent_marker_" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()) +
       ".txt");
  std::error_code ignored;
  std::filesystem::remove(marker_path, ignored);
  config.upstreams.front()
      .process_stdio.env["CXXMCP_GATEWAY_STDIO_MARKER_FILE"] =
      marker_path.string();

  mcp::gateway::GatewayRuntimeOptions options;
  options.upstream_session_mode =
      mcp::gateway::UpstreamSessionMode::persistent;
  mcp::gateway::GatewayRuntime runtime(std::move(config),
                                       std::move(options));

  auto first = runtime.call_tool("persistent.echo", Json{{"value", "first"}});
  require(first.has_value(), "first persistent stdio call should succeed");
  require_text_result(*first, "first");
  require(std::filesystem::exists(marker_path),
          "persistent stdio session should keep upstream process alive after "
          "first call");

  auto first_state =
      require_upstream_state(runtime.upstream_states(), "persistent");
  require(first_state.active_calls == 0,
          "persistent first call should leave no active upstream calls");
  require_status(first_state, UpstreamRuntimeStatus::healthy,
                 "persistent first call should leave upstream healthy");

  auto second =
      runtime.call_tool("persistent.echo", Json{{"value", "second"}});
  require(second.has_value(), "second persistent stdio call should succeed");
  require_text_result(*second, "second");
  require(std::filesystem::exists(marker_path),
          "persistent stdio session should reuse the existing upstream "
          "process for repeated calls");

  auto stopped = runtime.stop();
  require(stopped.has_value(), "persistent runtime should stop");

  bool marker_removed = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (!std::filesystem::exists(marker_path)) {
      marker_removed = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(marker_removed,
          "persistent runtime stop should clean up upstream process");
}

void test_persistent_capability_refresh_prewarms_stdio_session() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "prewarm";
  const auto marker_path =
      std::filesystem::temp_directory_path() /
      ("cxxmcp_gateway_stdio_prewarm_marker_" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()) +
       ".txt");
  std::error_code ignored;
  std::filesystem::remove(marker_path, ignored);
  config.upstreams.front()
      .process_stdio.env["CXXMCP_GATEWAY_STDIO_MARKER_FILE"] =
      marker_path.string();

  mcp::gateway::GatewayRuntimeOptions options;
  options.upstream_session_mode =
      mcp::gateway::UpstreamSessionMode::persistent;
  mcp::gateway::GatewayRuntime runtime(std::move(config),
                                       std::move(options));

  auto refreshed = runtime.refresh_upstream_capabilities();
  require(refreshed.has_value(),
          "persistent capability refresh should initialize upstream");
  require(std::filesystem::exists(marker_path),
          "persistent capability refresh should keep stdio session alive");

  const auto state = require_upstream_state(runtime.upstream_states(),
                                           "prewarm");
  require(state.capabilities.has_value(),
          "persistent capability refresh should record capabilities");
  require_status(state, UpstreamRuntimeStatus::healthy,
                 "persistent capability refresh should leave upstream healthy");

  auto stopped = runtime.stop();
  require(stopped.has_value(), "persistent prewarm runtime should stop");

  bool marker_removed = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (!std::filesystem::exists(marker_path)) {
      marker_removed = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(marker_removed,
          "persistent prewarm stop should clean up upstream process");
}

void test_persistent_stdio_calls_to_one_upstream_are_serialized() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "persistent_busy";

  mcp::gateway::GatewayRuntimeOptions options;
  options.upstream_session_mode =
      mcp::gateway::UpstreamSessionMode::persistent;
  mcp::gateway::GatewayRuntime runtime(std::move(config),
                                       std::move(options));

  std::exception_ptr first_error;
  std::exception_ptr second_error;
  const auto started = std::chrono::steady_clock::now();
  std::thread first_worker([&] {
    try {
      auto first =
          runtime.call_tool("persistent_busy.slow", Json{{"sleepMs", 250}});
      require(first.has_value(),
              "first persistent same-upstream slow call should succeed");
      require_text_result(*first, "slow-done");
    } catch (...) {
      first_error = std::current_exception();
    }
  });
  std::thread second_worker([&] {
    try {
      auto second =
          runtime.call_tool("persistent_busy.slow", Json{{"sleepMs", 250}});
      require(second.has_value(),
              "second persistent same-upstream slow call should succeed");
      require_text_result(*second, "slow-done");
    } catch (...) {
      second_error = std::current_exception();
    }
  });

  bool observed_two_active = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state =
        require_upstream_state(runtime.upstream_states(), "persistent_busy");
    if (state.active_calls >= 2) {
      observed_two_active = true;
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

  const auto elapsed = std::chrono::steady_clock::now() - started;
  require(observed_two_active,
          "persistent same-upstream calls should report both active calls");
  require(elapsed >= std::chrono::milliseconds{450},
          "persistent same-upstream calls should be serialized by the "
          "per-upstream session mutex");

  const auto state =
      require_upstream_state(runtime.upstream_states(), "persistent_busy");
  require(state.active_calls == 0,
          "persistent serialized calls should clear active calls");
  require_status(state, UpstreamRuntimeStatus::healthy,
                 "persistent serialized calls should leave upstream healthy");
}

void test_persistent_stdio_session_pool_allows_same_upstream_concurrency() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "persistent_pool";

  mcp::gateway::GatewayRuntimeOptions options;
  options.upstream_session_mode =
      mcp::gateway::UpstreamSessionMode::persistent;
  options.persistent_session_pool_size = 2;
  mcp::gateway::GatewayRuntime runtime(std::move(config),
                                       std::move(options));

  auto prewarmed = runtime.refresh_upstream_capabilities();
  require(prewarmed.has_value(),
          "persistent session pool prewarm should initialize sessions");
  {
    const auto state =
        require_upstream_state(runtime.upstream_states(), "persistent_pool");
    require(state.persistent_session_pool_size == 2,
            "persistent session pool state should expose configured size");
    require(state.initialized_persistent_sessions == 2,
            "persistent session pool prewarm should expose initialized slots");
    require(state.busy_persistent_sessions == 0,
            "persistent session pool prewarm should expose no busy slots");
  }

  std::exception_ptr first_error;
  std::exception_ptr second_error;
  const auto started = std::chrono::steady_clock::now();
  std::thread first_worker([&] {
    try {
      auto first =
          runtime.call_tool("persistent_pool.slow", Json{{"sleepMs", 450}});
      require(first.has_value(),
              "first pooled persistent slow call should succeed");
      require_text_result(*first, "slow-done");
    } catch (...) {
      first_error = std::current_exception();
    }
  });
  std::thread second_worker([&] {
    try {
      auto second =
          runtime.call_tool("persistent_pool.slow", Json{{"sleepMs", 450}});
      require(second.has_value(),
              "second pooled persistent slow call should succeed");
      require_text_result(*second, "slow-done");
    } catch (...) {
      second_error = std::current_exception();
    }
  });

  bool observed_two_active = false;
  bool observed_two_busy_slots = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state =
        require_upstream_state(runtime.upstream_states(), "persistent_pool");
    if (state.active_calls >= 2) {
      observed_two_active = true;
      if (state.busy_persistent_sessions == 2) {
        observed_two_busy_slots = true;
        break;
      }
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

  const auto elapsed = std::chrono::steady_clock::now() - started;
  require(observed_two_active,
          "pooled persistent same-upstream calls should report both active "
          "calls");
  require(observed_two_busy_slots,
          "pooled persistent same-upstream calls should expose busy slots");
  require(elapsed < std::chrono::milliseconds{800},
          "pooled persistent same-upstream calls should use separate sessions");

  const auto state =
      require_upstream_state(runtime.upstream_states(), "persistent_pool");
  require(state.active_calls == 0,
          "pooled persistent calls should clear active calls");
  require(state.initialized_persistent_sessions == 2,
          "pooled persistent calls should keep both sessions initialized");
  require(state.busy_persistent_sessions == 0,
          "pooled persistent calls should clear busy slots");
  require_status(state, UpstreamRuntimeStatus::healthy,
                 "pooled persistent calls should leave upstream healthy");
}

void test_persistent_stop_rejects_queued_session_pool_call() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "persistent_queue";

  mcp::gateway::GatewayRuntimeOptions options;
  options.upstream_session_mode =
      mcp::gateway::UpstreamSessionMode::persistent;
  options.persistent_session_pool_size = 1;
  mcp::gateway::GatewayRuntime runtime(std::move(config),
                                       std::move(options));

  std::exception_ptr first_error;
  std::exception_ptr second_error;
  std::thread first_worker([&] {
    try {
      auto first =
          runtime.call_tool("persistent_queue.slow", Json{{"sleepMs", 600}});
      require(first.has_value(),
              "active persistent queue test call should succeed");
      require_text_result(*first, "slow-done");
    } catch (...) {
      first_error = std::current_exception();
    }
  });

  bool observed_first_active = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state =
        require_upstream_state(runtime.upstream_states(), "persistent_queue");
    if (state.active_calls >= 1) {
      observed_first_active = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(observed_first_active,
          "persistent queue test should observe first active call");

  std::thread second_worker([&] {
    try {
      auto second =
          runtime.call_tool("persistent_queue.echo", Json{{"value", "queued"}});
      require(!second.has_value(),
              "queued persistent call should be rejected during stop");
      require_runtime_stopping_error(second.error(), "persistent_queue");
    } catch (...) {
      second_error = std::current_exception();
    }
  });

  bool observed_queued_call = false;
  bool observed_one_busy_slot = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state =
        require_upstream_state(runtime.upstream_states(), "persistent_queue");
    if (state.active_calls >= 2) {
      observed_queued_call = true;
      if (state.persistent_session_pool_size == 1 &&
          state.busy_persistent_sessions == 1) {
        observed_one_busy_slot = true;
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(observed_queued_call,
          "persistent queue test should observe call waiting for pool slot");
  require(observed_one_busy_slot,
          "persistent queue test should expose bounded busy pool slots");

  auto stopped = runtime.stop();
  require(stopped.has_value(),
          "persistent queue test runtime should stop after active call drains");

  first_worker.join();
  second_worker.join();
  if (first_error) {
    std::rethrow_exception(first_error);
  }
  if (second_error) {
    std::rethrow_exception(second_error);
  }

  const auto state =
      require_upstream_state(runtime.upstream_states(), "persistent_queue");
  require(state.active_calls == 0,
          "persistent queue stop should clear active call counters");
  require(state.busy_persistent_sessions == 0,
          "persistent queue stop should clear busy pool slots");
  require_status(state, UpstreamRuntimeStatus::stopped,
                 "persistent queue stop should leave upstream stopped");
}

void test_persistent_pool_acquire_timeout_rejects_queued_call() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "persistent_pool_wait_timeout";

  mcp::gateway::GatewayRuntimeOptions options;
  options.upstream_session_mode =
      mcp::gateway::UpstreamSessionMode::persistent;
  options.persistent_session_pool_size = 1;
  options.persistent_session_acquire_timeout =
      std::chrono::milliseconds{100};
  mcp::gateway::GatewayRuntime runtime(std::move(config),
                                       std::move(options));

  std::exception_ptr first_error;
  std::thread first_worker([&] {
    try {
      auto first = runtime.call_tool("persistent_pool_wait_timeout.slow",
                                     Json{{"sleepMs", 500}});
      require(first.has_value(),
              "active persistent pool wait timeout test call should succeed");
      require_text_result(*first, "slow-done");
    } catch (...) {
      first_error = std::current_exception();
    }
  });

  bool observed_busy_slot = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state = require_upstream_state(
        runtime.upstream_states(), "persistent_pool_wait_timeout");
    if (state.active_calls >= 1 && state.busy_persistent_sessions == 1) {
      observed_busy_slot = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(observed_busy_slot,
          "persistent pool wait timeout test should observe busy slot");

  const auto started = std::chrono::steady_clock::now();
  auto queued = runtime.call_tool("persistent_pool_wait_timeout.echo",
                                  Json{{"value", "queued"}});
  const auto elapsed = std::chrono::steady_clock::now() - started;
  require(!queued.has_value(),
          "queued persistent pool call should fail on acquire timeout");
  require_persistent_pool_wait_timeout(queued.error(),
                                       "persistent_pool_wait_timeout");
  require(elapsed < std::chrono::milliseconds{450},
          "persistent pool acquire timeout should bound queued wait");

  first_worker.join();
  if (first_error) {
    std::rethrow_exception(first_error);
  }

  const auto state = require_upstream_state(runtime.upstream_states(),
                                           "persistent_pool_wait_timeout");
  require(state.active_calls == 0,
          "persistent pool wait timeout should clear active calls");
  require(state.initialized_persistent_sessions == 1,
          "persistent pool wait timeout should keep healthy slot initialized");
  require(state.busy_persistent_sessions == 0,
          "persistent pool wait timeout should clear busy slot");
  require_status(state, UpstreamRuntimeStatus::healthy,
                 "persistent pool wait timeout should leave upstream healthy");
}

void test_persistent_runtime_stop_waits_for_active_stdio_call() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "persistent_stop";
  const auto marker_path =
      std::filesystem::temp_directory_path() /
      ("cxxmcp_gateway_stdio_persistent_stop_marker_" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()) +
       ".txt");
  std::error_code ignored;
  std::filesystem::remove(marker_path, ignored);
  config.upstreams.front()
      .process_stdio.env["CXXMCP_GATEWAY_STDIO_MARKER_FILE"] =
      marker_path.string();

  mcp::gateway::GatewayRuntimeOptions options;
  options.upstream_session_mode =
      mcp::gateway::UpstreamSessionMode::persistent;
  mcp::gateway::GatewayRuntime runtime(std::move(config),
                                       std::move(options));

  std::exception_ptr worker_error;
  std::thread worker([&] {
    try {
      auto result =
          runtime.call_tool("persistent_stop.slow", Json{{"sleepMs", 450}});
      require(result.has_value(),
              "active persistent stdio call should finish during stop");
      require_text_result(*result, "slow-done");
    } catch (...) {
      worker_error = std::current_exception();
    }
  });

  bool observed_active = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state =
        require_upstream_state(runtime.upstream_states(), "persistent_stop");
    if (state.active_calls >= 1 && std::filesystem::exists(marker_path)) {
      observed_active = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(observed_active,
          "persistent stop test should observe active stdio call");

  auto stopped = runtime.stop();
  require(stopped.has_value(),
          "persistent runtime stop should wait for active stdio call");

  worker.join();
  if (worker_error) {
    std::rethrow_exception(worker_error);
  }

  bool marker_removed = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (!std::filesystem::exists(marker_path)) {
      marker_removed = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(marker_removed,
          "persistent runtime stop should clean up active stdio process");
}

void test_persistent_pool_stop_waits_for_timed_out_stdio_call() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "persistent_pool_stop_timeout";
  config.upstreams.front().process_stdio.timeout =
      std::chrono::milliseconds{350};
  const auto marker_dir =
      std::filesystem::temp_directory_path() /
      ("cxxmcp_gateway_stdio_pool_stop_timeout_markers_" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()));
  std::error_code ignored;
  std::filesystem::remove_all(marker_dir, ignored);
  std::filesystem::create_directories(marker_dir, ignored);
  config.upstreams.front()
      .process_stdio.env["CXXMCP_GATEWAY_STDIO_MARKER_DIR"] =
      marker_dir.string();
  const auto slow_marker_path =
      marker_dir /
      ("slow-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()) +
       ".txt");
  std::filesystem::remove(slow_marker_path, ignored);
  config.upstreams.front()
      .process_stdio.env["CXXMCP_GATEWAY_STDIO_SLOW_MARKER_FILE"] =
      slow_marker_path.string();

  mcp::gateway::GatewayRuntimeOptions options;
  options.upstream_session_mode =
      mcp::gateway::UpstreamSessionMode::persistent;
  options.persistent_session_pool_size = 2;
  mcp::gateway::GatewayRuntime runtime(std::move(config),
                                       std::move(options));

  auto prewarmed = runtime.refresh_upstream_capabilities();
  require(prewarmed.has_value(),
          "persistent pool stop timeout test should prewarm sessions");

  bool observed_two_markers = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (count_regular_files(marker_dir) == 2) {
      observed_two_markers = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(observed_two_markers,
          "persistent pool stop timeout test should start two sessions");

  std::exception_ptr worker_error;
  std::thread worker([&] {
    try {
      auto result = runtime.call_tool("persistent_pool_stop_timeout.slow",
                                      Json{{"sleepMs", 600}});
      require(!result.has_value(),
              "hostile persistent pool call should time out");
      require_gateway_upstream_timeout(result.error(),
                                       "persistent_pool_stop_timeout");
    } catch (...) {
      worker_error = std::current_exception();
    }
  });

  bool observed_active = false;
  for (int attempt = 0; attempt < 500; ++attempt) {
    const auto state = require_upstream_state(
        runtime.upstream_states(), "persistent_pool_stop_timeout");
    if (state.active_calls >= 1) {
      observed_active = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(observed_active,
          "persistent pool stop timeout test should observe active call");
  bool observed_slow_marker = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (std::filesystem::exists(slow_marker_path)) {
      observed_slow_marker = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(observed_slow_marker,
          "persistent pool stop timeout test should observe slow handler");

  const auto stop_started = std::chrono::steady_clock::now();
  auto stopped = runtime.stop();
  const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;
  require(stopped.has_value(),
          "persistent pool stop should complete after active call timeout");
  require(stop_elapsed < std::chrono::seconds{5},
          "persistent pool stop should be bounded by upstream timeout");

  worker.join();
  if (worker_error) {
    std::rethrow_exception(worker_error);
  }

  const auto state = require_upstream_state(runtime.upstream_states(),
                                           "persistent_pool_stop_timeout");
  require(state.active_calls == 0,
          "persistent pool stop timeout should clear active calls");
  require(state.initialized_persistent_sessions == 0,
          "persistent pool stop timeout should discard initialized sessions");
  require(state.busy_persistent_sessions == 0,
          "persistent pool stop timeout should release busy sessions");
  require_status(state, UpstreamRuntimeStatus::stopped,
                 "persistent pool stop timeout should leave upstream stopped");
  std::filesystem::remove(slow_marker_path, ignored);
  std::filesystem::remove_all(marker_dir, ignored);
}

void test_persistent_stdio_failure_invalidates_session_for_reconnect() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "persistent_reconnect";
  config.upstreams.front().process_stdio.timeout =
      std::chrono::milliseconds{250};
  const auto marker_path =
      std::filesystem::temp_directory_path() /
      ("cxxmcp_gateway_stdio_persistent_reconnect_marker_" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()) +
       ".txt");
  std::error_code ignored;
  std::filesystem::remove(marker_path, ignored);
  config.upstreams.front()
      .process_stdio.env["CXXMCP_GATEWAY_STDIO_MARKER_FILE"] =
      marker_path.string();

  mcp::gateway::GatewayRuntimeOptions options;
  options.upstream_session_mode =
      mcp::gateway::UpstreamSessionMode::persistent;
  mcp::gateway::GatewayRuntime runtime(std::move(config),
                                       std::move(options));

  auto timed_out =
      runtime.call_tool("persistent_reconnect.slow", Json{{"sleepMs", 1000}});
  require(!timed_out.has_value(),
          "persistent stdio slow call should time out");
  require_gateway_upstream_timeout(timed_out.error(), "persistent_reconnect");

  for (int attempt = 0; attempt < 200 && std::filesystem::exists(marker_path);
       ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(!std::filesystem::exists(marker_path),
          "persistent timeout should discard and stop the failed session");

  auto recovered = runtime.call_tool("persistent_reconnect.echo",
                                     Json{{"value", "recovered"}});
  require(recovered.has_value(),
          "persistent stdio call should reconnect after failed session");
  require_text_result(*recovered, "recovered");
  require(std::filesystem::exists(marker_path),
          "persistent reconnect should keep the replacement session alive");

  const auto state = require_upstream_state(runtime.upstream_states(),
                                           "persistent_reconnect");
  require(state.active_calls == 0,
          "persistent reconnect should clear active calls");
  require_status(state, UpstreamRuntimeStatus::healthy,
                 "persistent reconnect should restore upstream health");
}

void test_persistent_stdio_pool_failure_isolates_failed_slot() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "persistent_pool_reconnect";
  config.upstreams.front().process_stdio.timeout =
      std::chrono::milliseconds{250};

  mcp::gateway::GatewayRuntimeOptions options;
  options.upstream_session_mode =
      mcp::gateway::UpstreamSessionMode::persistent;
  options.persistent_session_pool_size = 2;
  mcp::gateway::GatewayRuntime runtime(std::move(config),
                                       std::move(options));

  auto prewarmed = runtime.refresh_upstream_capabilities();
  require(prewarmed.has_value(),
          "persistent stdio pool reconnect test should prewarm sessions");
  {
    const auto state = require_upstream_state(runtime.upstream_states(),
                                             "persistent_pool_reconnect");
    require(state.persistent_session_pool_size == 2,
            "persistent stdio pool prewarm should keep configured pool size");
    require(state.initialized_persistent_sessions == 2,
            "persistent stdio pool prewarm should initialize both slots");
    require(state.busy_persistent_sessions == 0,
            "persistent stdio pool prewarm should leave no busy slots");
  }

  auto timed_out = runtime.call_tool("persistent_pool_reconnect.slow",
                                     Json{{"sleepMs", 1000}});
  require(!timed_out.has_value(),
          "persistent stdio pool slow call should time out");
  require_gateway_upstream_timeout(timed_out.error(),
                                   "persistent_pool_reconnect");

  {
    const auto state = require_upstream_state(runtime.upstream_states(),
                                             "persistent_pool_reconnect");
    require(state.persistent_session_pool_size == 2,
            "persistent stdio pool timeout should keep configured pool size");
    require(state.initialized_persistent_sessions == 1,
            "persistent stdio pool timeout should expose one healthy slot");
    require(state.busy_persistent_sessions == 0,
            "persistent stdio pool timeout should clear busy slots");
  }

  auto recovered = runtime.call_tool("persistent_pool_reconnect.echo",
                                     Json{{"value", "recovered"}});
  require(recovered.has_value(),
          "persistent stdio pool should reconnect a discarded slot");
  require_text_result(*recovered, "recovered");

  const auto state = require_upstream_state(runtime.upstream_states(),
                                           "persistent_pool_reconnect");
  require(state.active_calls == 0,
          "persistent stdio pool reconnect should clear active calls");
  require(state.initialized_persistent_sessions == 2,
          "persistent stdio pool reconnect should restore initialized slots");
  require(state.busy_persistent_sessions == 0,
          "persistent stdio pool reconnect should clear busy slots");
  require_status(state, UpstreamRuntimeStatus::healthy,
                 "persistent stdio pool reconnect should restore health");

  auto stopped = runtime.stop();
  require(stopped.has_value(),
          "persistent stdio pool reconnect runtime should stop");
  const auto stopped_state = require_upstream_state(
      runtime.upstream_states(), "persistent_pool_reconnect");
  require(stopped_state.initialized_persistent_sessions == 0,
          "persistent stdio pool stop should discard initialized sessions");
  require(stopped_state.busy_persistent_sessions == 0,
          "persistent stdio pool stop should clear busy slots");
  require_status(stopped_state, UpstreamRuntimeStatus::stopped,
                 "persistent stdio pool stop should leave upstream stopped");
}

void test_http_upstream() {
  const auto kPort = find_available_loopback_port();
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
                    .tool<Json, ToolResult>(
                        "fail",
                        [](const Json&) -> mcp::core::Result<ToolResult> {
                          return mcp::core::unexpected(mcp::core::Error{
                              static_cast<int>(
                                  mcp::protocol::ErrorCode::PermissionDenied),
                              "http fixture denied", "http fixture detail",
                              "fixture"});
                        })
                    .prompt(mcp::protocol::Prompt{
                                .title = "HTTP Summary",
                                .name = "summarize",
                                .description = "Summarize HTTP text",
                                .arguments =
                                    {
                                        mcp::protocol::PromptArgument{
                                            .name = "text",
                                            .description = "Text to summarize",
                                            .required = true,
                                            .required_present = true,
                                        },
                                    },
                            },
                            [](const mcp::server::PromptContext& context) {
                              mcp::protocol::PromptsGetResult result;
                              result.description = "Summarize HTTP text";
                              result.messages.push_back(
                                  mcp::protocol::PromptMessage::text(
                                      "user",
                                      "HTTP summarize " +
                                          context.arguments.value(
                                              "text", std::string{})));
                              return result;
                            })
                    .prompt("fail-prompt",
                            [](const mcp::server::PromptContext&)
                                -> mcp::core::Result<
                                    mcp::protocol::PromptsGetResult> {
                              return mcp::core::unexpected(mcp::core::Error{
                                  static_cast<int>(
                                      mcp::protocol::ErrorCode::
                                          PermissionDenied),
                                  "http prompt denied", "http prompt detail",
                                  "fixture"});
                            })
                    .resource(mcp::protocol::Resource{
                                  .title = "HTTP Readme",
                                  .uri = "file:///http/readme.txt",
                                  .name = "http-readme",
                                  .description = "HTTP readme resource",
                                  .mime_type = "text/plain",
                              },
                              [](const mcp::server::ResourceContext& context)
                                  -> mcp::core::Result<
                                      mcp::protocol::ResourcesReadResult> {
                                mcp::protocol::ResourcesReadResult result;
                                result.contents.push_back(
                                    mcp::protocol::ResourceContents{
                                        .uri = context.uri,
                                        .mime_type = "text/plain",
                                        .text = "hello from http resource",
                                    });
                                return result;
                              })
                    .resource("file:///http/fail.txt",
                              [](const mcp::server::ResourceContext&)
                                  -> mcp::core::Result<
                                      mcp::protocol::ResourcesReadResult> {
                                return mcp::core::unexpected(mcp::core::Error{
                                    static_cast<int>(
                                        mcp::protocol::ErrorCode::
                                            PermissionDenied),
                                    "http resource denied",
                                    "http resource detail", "fixture"});
                              })
                    .resource_template(mcp::protocol::ResourceTemplate{
                        .title = "HTTP File",
                        .uri_template = "file:///http/{path}",
                        .name = "http-file",
                        .description = "HTTP file by path",
                        .mime_type = "text/plain",
                    })
                    .completion(
                        [](const mcp::protocol::CompleteParams& params,
                           const mcp::server::CompletionContext&) {
                          mcp::protocol::CompleteResult result;
                          if (params.ref.type == "ref/prompt" &&
                              params.ref.name == "summarize" &&
                              params.argument.name == "text") {
                            result.completion.values = {
                                params.argument.value + "-http-summary",
                                params.argument.value + "-http-brief",
                            };
                            result.completion.total = 2;
                            result.completion.has_more = false;
                            return result;
                          }
                          if (params.ref.type == "ref/resource" &&
                              params.ref.uri == "file:///http/{path}" &&
                              params.argument.name == "path") {
                            result.completion.values = {
                                params.argument.value + "readme.txt",
                                params.argument.value + "config.json",
                            };
                            result.completion.total = 2;
                            result.completion.has_more = false;
                            return result;
                          }
                          result.completion.values = {};
                          result.completion.total = 0;
                          result.completion.has_more = false;
                          return result;
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
  auto refreshed = runtime.refresh_upstream_capabilities();
  require(refreshed.has_value(), "http capability refresh should succeed");
  auto capabilities = runtime.server_capabilities();
  require(capabilities.completions.enabled,
          "http runtime should advertise completions after refreshed "
          "upstream capabilities support completion");

  auto tools = runtime.list_tools();
  require(tools.has_value(), "http tools/list should succeed");
  require(has_tool(*tools, "http.echo"), "http tool should be exposed");

  auto called = runtime.call_tool("http.echo", Json{{"value", "from-http"}});
  require(called.has_value(), "http tools/call should succeed");
  require_text_result(*called, "from-http");

  auto upstream_tool_error = runtime.call_tool("http.fail", Json::object());
  require(!upstream_tool_error.has_value(),
          "http upstream MCP tool error should fail");
  require(upstream_tool_error.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::PermissionDenied),
          "http upstream MCP tool error code should be preserved");
  require(upstream_tool_error.error().message == "http fixture denied",
          "http upstream MCP tool error message should be preserved");
  require(upstream_tool_error.error().detail.find("http fixture detail") !=
              std::string::npos,
          "http upstream MCP tool error detail should be preserved");
  require_gateway_upstream_error(upstream_tool_error.error(), "http");

  const auto http_resource_uri =
      mcp::gateway::GatewayRouter::expose_resource_uri(
          "http", "file:///http/readme.txt");
  auto resources = runtime.list_resources();
  require(resources.has_value(), "http resources/list should succeed");
  require(has_resource(*resources, http_resource_uri),
          "http resource should be exposed");
  auto read = runtime.read_resource(http_resource_uri);
  require(read.has_value(), "http resources/read should succeed");
  require_text_resource(*read, http_resource_uri,
                        "hello from http resource");

  const auto http_error_resource_uri =
      mcp::gateway::GatewayRouter::expose_resource_uri(
          "http", "file:///http/fail.txt");
  auto upstream_resource_error =
      runtime.read_resource(http_error_resource_uri);
  require(!upstream_resource_error.has_value(),
          "http upstream MCP resource error should fail");
  require(upstream_resource_error.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::PermissionDenied),
          "http upstream MCP resource error code should be preserved");
  require(upstream_resource_error.error().message == "http resource denied",
          "http upstream MCP resource error message should be preserved");
  require(upstream_resource_error.error().detail.find(
              "http resource detail") != std::string::npos,
          "http upstream MCP resource error detail should be preserved");
  require_gateway_upstream_error(upstream_resource_error.error(), "http");

  const auto http_template_uri =
      mcp::gateway::GatewayRouter::expose_resource_template_uri(
          "http", "file:///http/{path}");
  auto resource_templates = runtime.list_resource_templates();
  require(resource_templates.has_value(),
          "http resources/templates/list should succeed");
  require(has_resource_template(*resource_templates, http_template_uri),
          "http resource template should be exposed");

  auto prompts = runtime.list_prompts();
  require(prompts.has_value(), "http prompts/list should succeed");
  require(has_prompt(*prompts, "http.summarize"),
          "http prompt should be exposed");
  auto prompt =
      runtime.get_prompt("http.summarize", Json{{"text", "from-http"}});
  require(prompt.has_value(), "http prompts/get should succeed");
  require_text_prompt(*prompt, "HTTP summarize from-http");

  auto upstream_prompt_error =
      runtime.get_prompt("http.fail-prompt", Json::object());
  require(!upstream_prompt_error.has_value(),
          "http upstream MCP prompt error should fail");
  require(upstream_prompt_error.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::PermissionDenied),
          "http upstream MCP prompt error code should be preserved");
  require(upstream_prompt_error.error().message == "http prompt denied",
          "http upstream MCP prompt error message should be preserved");
  require(upstream_prompt_error.error().detail.find("http prompt detail") !=
              std::string::npos,
          "http upstream MCP prompt error detail should be preserved");
  require_gateway_upstream_error(upstream_prompt_error.error(), "http");

  mcp::protocol::CompleteParams prompt_completion;
  prompt_completion.ref =
      mcp::protocol::prompt_completion_reference("http.summarize");
  prompt_completion.argument.name = "text";
  prompt_completion.argument.value = "from-http";
  auto prompt_completion_result =
      runtime.complete(std::move(prompt_completion));
  require(prompt_completion_result.has_value(),
          "http prompt completion should route");
  require(prompt_completion_result->completion.values.size() == 2,
          "http prompt completion should preserve upstream candidates");
  require(prompt_completion_result->completion.values[0] ==
              "from-http-http-summary",
          "http prompt completion should rewrite prompt ref to upstream name");
  require(prompt_completion_result->completion.total == 2,
          "http prompt completion should preserve total");
  require(prompt_completion_result->completion.has_more == false,
          "http prompt completion should preserve hasMore");

  mcp::protocol::CompleteParams resource_completion;
  resource_completion.ref =
      mcp::protocol::resource_completion_reference(http_template_uri);
  resource_completion.argument.name = "path";
  resource_completion.argument.value = "docs/";
  auto resource_completion_result =
      runtime.complete(std::move(resource_completion));
  require(resource_completion_result.has_value(),
          "http resource template completion should route");
  require(resource_completion_result->completion.values.size() == 2,
          "http resource completion should preserve upstream candidates");
  require(resource_completion_result->completion.values[0] ==
              "docs/readme.txt",
          "http resource completion should rewrite resource ref to upstream "
          "URI template");

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
  auto multi_resources = multi_runtime.list_resources();
  require(multi_resources.has_value(),
          "multi-upstream resources/list should succeed");
  require(has_resource(*multi_resources,
                       mcp::gateway::GatewayRouter::expose_resource_uri(
                           "stdio", "file:///fixture/readme.txt")),
          "multi-upstream resource list should include stdio resource");
  require(has_resource(*multi_resources, http_resource_uri),
          "multi-upstream resource list should include http resource");
  auto multi_resource_templates = multi_runtime.list_resource_templates();
  require(multi_resource_templates.has_value(),
          "multi-upstream resources/templates/list should succeed");
  require(has_resource_template(
              *multi_resource_templates,
              mcp::gateway::GatewayRouter::expose_resource_template_uri(
                  "stdio", "file:///fixture/{path}")),
          "multi-upstream resource template list should include stdio "
          "template");
  require(has_resource_template(*multi_resource_templates, http_template_uri),
          "multi-upstream resource template list should include http template");

  auto multi_prompts = multi_runtime.list_prompts();
  require(multi_prompts.has_value(),
          "multi-upstream prompts/list should succeed");
  require(has_prompt(*multi_prompts, "stdio.summarize"),
          "multi-upstream prompt list should include stdio prompt");
  require(has_prompt(*multi_prompts, "http.summarize"),
          "multi-upstream prompt list should include http prompt");

  const auto stopped = running->stop();
  require(stopped.has_value(), "http fixture server should stop");
}

void test_http_timeout() {
  const auto kPort = find_available_loopback_port();
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

  std::this_thread::sleep_for(std::chrono::seconds{2});
  const auto stopped = running->stop();
  require(stopped.has_value(), "http timeout fixture should stop");
}

void test_persistent_http_calls_to_one_upstream_are_serialized() {
  const auto kPort = find_available_loopback_port();
  const std::string uri = "http://127.0.0.1:" + std::to_string(kPort) + "/mcp";

  auto server = mcp::ServerPeer::builder()
                    .name("cxxmcp-gateway-persistent-http-busy")
                    .version("1.0.0")
                    .streamable_http("127.0.0.1", kPort, "/mcp")
                    .tool<Json, ToolResult>("slow", [](const Json& input) {
                      std::this_thread::sleep_for(
                          std::chrono::milliseconds{
                              input.value("sleepMs", 250)});
                      return ToolResult::text("slow-done");
                    })
                    .build();
  require(server.has_value(), "persistent http busy fixture should build");

  auto running = mcp::serve(std::move(*server));
  require(running.has_value(), "persistent http busy fixture should start");
  running->wait_until_ready();

  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "persistent_http_busy";
  upstream.transport = mcp::gateway::UpstreamTransportKind::streamable_http;
  upstream.streamable_http.uri = uri;
  upstream.streamable_http.timeout = std::chrono::seconds{3};
  config.upstreams.push_back(std::move(upstream));

  mcp::gateway::GatewayRuntimeOptions options;
  options.upstream_session_mode =
      mcp::gateway::UpstreamSessionMode::persistent;
  mcp::gateway::GatewayRuntime runtime(std::move(config),
                                       std::move(options));

  std::exception_ptr first_error;
  std::exception_ptr second_error;
  const auto started = std::chrono::steady_clock::now();
  std::thread first_worker([&] {
    try {
      auto first = runtime.call_tool("persistent_http_busy.slow",
                                     Json{{"sleepMs", 250}});
      require(first.has_value(),
              "first persistent http same-upstream call should succeed");
      require_text_result(*first, "slow-done");
    } catch (...) {
      first_error = std::current_exception();
    }
  });
  std::thread second_worker([&] {
    try {
      auto second = runtime.call_tool("persistent_http_busy.slow",
                                      Json{{"sleepMs", 250}});
      require(second.has_value(),
              "second persistent http same-upstream call should succeed");
      require_text_result(*second, "slow-done");
    } catch (...) {
      second_error = std::current_exception();
    }
  });

  bool observed_two_active = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state = require_upstream_state(runtime.upstream_states(),
                                             "persistent_http_busy");
    if (state.active_calls >= 2) {
      observed_two_active = true;
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

  const auto elapsed = std::chrono::steady_clock::now() - started;
  require(observed_two_active,
          "persistent http same-upstream calls should report both active calls");
  require(elapsed >= std::chrono::milliseconds{450},
          "persistent http same-upstream calls should be serialized by the "
          "per-upstream session mutex");

  const auto state =
      require_upstream_state(runtime.upstream_states(), "persistent_http_busy");
  require(state.active_calls == 0,
          "persistent http serialized calls should clear active calls");
  require_status(state, UpstreamRuntimeStatus::healthy,
                 "persistent http serialized calls should leave upstream "
                 "healthy");

  auto stopped_runtime = runtime.stop();
  require(stopped_runtime.has_value(),
          "persistent http busy runtime should stop");
  const auto stopped_server = running->stop();
  require(stopped_server.has_value(),
          "persistent http busy fixture should stop");
}

void test_persistent_http_session_pool_handles_queued_calls() {
  const auto kPort = find_available_loopback_port();
  const std::string uri = "http://127.0.0.1:" + std::to_string(kPort) + "/mcp";

  auto server = mcp::ServerPeer::builder()
                    .name("cxxmcp-gateway-persistent-http-pool")
                    .version("1.0.0")
                    .streamable_http("127.0.0.1", kPort, "/mcp")
                    .tool<Json, ToolResult>("slow", [](const Json& input) {
                      std::this_thread::sleep_for(
                          std::chrono::milliseconds{
                              input.value("sleepMs", 400)});
                      return ToolResult::text("slow-done");
                    })
                    .build();
  require(server.has_value(), "persistent http pool fixture should build");

  auto running = mcp::serve(std::move(*server));
  require(running.has_value(), "persistent http pool fixture should start");
  running->wait_until_ready();

  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "persistent_http_pool";
  upstream.transport = mcp::gateway::UpstreamTransportKind::streamable_http;
  upstream.streamable_http.uri = uri;
  upstream.streamable_http.timeout = std::chrono::seconds{3};
  config.upstreams.push_back(std::move(upstream));

  mcp::gateway::GatewayRuntimeOptions options;
  options.upstream_session_mode =
      mcp::gateway::UpstreamSessionMode::persistent;
  options.persistent_session_pool_size = 2;
  mcp::gateway::GatewayRuntime runtime(std::move(config),
                                       std::move(options));

  auto prewarmed = runtime.refresh_upstream_capabilities();
  require(prewarmed.has_value(),
          "persistent http session pool prewarm should initialize sessions");

  std::exception_ptr first_error;
  std::exception_ptr second_error;
  std::exception_ptr third_error;
  const auto started = std::chrono::steady_clock::now();
  std::thread first_worker([&] {
    try {
      auto first = runtime.call_tool("persistent_http_pool.slow",
                                     Json{{"sleepMs", 400}});
      require(first.has_value(),
              "first pooled persistent http slow call should succeed");
      require_text_result(*first, "slow-done");
    } catch (...) {
      first_error = std::current_exception();
    }
  });
  std::thread second_worker([&] {
    try {
      auto second = runtime.call_tool("persistent_http_pool.slow",
                                      Json{{"sleepMs", 400}});
      require(second.has_value(),
              "second pooled persistent http slow call should succeed");
      require_text_result(*second, "slow-done");
    } catch (...) {
      second_error = std::current_exception();
    }
  });
  std::thread third_worker([&] {
    try {
      auto third = runtime.call_tool("persistent_http_pool.slow",
                                     Json{{"sleepMs", 400}});
      require(third.has_value(),
              "third pooled persistent http slow call should succeed");
      require_text_result(*third, "slow-done");
    } catch (...) {
      third_error = std::current_exception();
    }
  });

  bool observed_three_active = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state = require_upstream_state(runtime.upstream_states(),
                                             "persistent_http_pool");
    if (state.active_calls >= 3) {
      observed_three_active = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }

  first_worker.join();
  second_worker.join();
  third_worker.join();
  if (first_error) {
    std::rethrow_exception(first_error);
  }
  if (second_error) {
    std::rethrow_exception(second_error);
  }
  if (third_error) {
    std::rethrow_exception(third_error);
  }

  const auto elapsed = std::chrono::steady_clock::now() - started;
  require(observed_three_active,
          "pooled persistent http calls should report accepted queued calls");
  require(elapsed >= std::chrono::milliseconds{700},
          "persistent http pool size two should bound three accepted calls");
  require(elapsed < std::chrono::milliseconds{1800},
          "persistent http pool queued calls should drain without hanging");

  const auto state =
      require_upstream_state(runtime.upstream_states(), "persistent_http_pool");
  require(state.active_calls == 0,
          "pooled persistent http calls should clear active calls");
  require_status(state, UpstreamRuntimeStatus::healthy,
                 "pooled persistent http calls should leave upstream healthy");

  auto stopped_runtime = runtime.stop();
  require(stopped_runtime.has_value(),
          "persistent http pool runtime should stop");
  const auto stopped_server = running->stop();
  require(stopped_server.has_value(),
          "persistent http pool fixture should stop");
}

void test_persistent_http_pool_acquire_timeout_rejects_queued_call() {
  const auto kPort = find_available_loopback_port();
  const std::string uri = "http://127.0.0.1:" + std::to_string(kPort) + "/mcp";

  auto server = mcp::ServerPeer::builder()
                    .name("cxxmcp-gateway-persistent-http-pool-wait-timeout")
                    .version("1.0.0")
                    .streamable_http("127.0.0.1", kPort, "/mcp")
                    .tool<Json, ToolResult>("echo", [](const Json& input) {
                      return ToolResult::text(
                          input.value("value", std::string{}));
                    })
                    .tool<Json, ToolResult>("slow", [](const Json& input) {
                      std::this_thread::sleep_for(
                          std::chrono::milliseconds{
                              input.value("sleepMs", 500)});
                      return ToolResult::text("slow-done");
                    })
                    .build();
  require(server.has_value(),
          "persistent http pool wait timeout fixture should build");

  auto running = mcp::serve(std::move(*server));
  require(running.has_value(),
          "persistent http pool wait timeout fixture should start");
  running->wait_until_ready();

  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "persistent_http_pool_wait_timeout";
  upstream.transport = mcp::gateway::UpstreamTransportKind::streamable_http;
  upstream.streamable_http.uri = uri;
  upstream.streamable_http.timeout = std::chrono::seconds{3};
  config.upstreams.push_back(std::move(upstream));

  mcp::gateway::GatewayRuntimeOptions options;
  options.upstream_session_mode =
      mcp::gateway::UpstreamSessionMode::persistent;
  options.persistent_session_pool_size = 1;
  options.persistent_session_acquire_timeout =
      std::chrono::milliseconds{100};
  mcp::gateway::GatewayRuntime runtime(std::move(config),
                                       std::move(options));

  std::exception_ptr first_error;
  std::thread first_worker([&] {
    try {
      auto first = runtime.call_tool("persistent_http_pool_wait_timeout.slow",
                                     Json{{"sleepMs", 500}});
      require(first.has_value(),
              "active persistent http pool wait timeout call should succeed");
      require_text_result(*first, "slow-done");
    } catch (...) {
      first_error = std::current_exception();
    }
  });

  bool observed_busy_slot = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state = require_upstream_state(
        runtime.upstream_states(), "persistent_http_pool_wait_timeout");
    if (state.active_calls >= 1 && state.busy_persistent_sessions == 1) {
      observed_busy_slot = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(observed_busy_slot,
          "persistent http pool wait timeout test should observe busy slot");

  const auto started = std::chrono::steady_clock::now();
  auto queued = runtime.call_tool("persistent_http_pool_wait_timeout.echo",
                                  Json{{"value", "queued"}});
  const auto elapsed = std::chrono::steady_clock::now() - started;
  require(!queued.has_value(),
          "queued persistent http pool call should fail on acquire timeout");
  require_persistent_pool_wait_timeout(queued.error(),
                                       "persistent_http_pool_wait_timeout");
  require(elapsed < std::chrono::milliseconds{450},
          "persistent http pool acquire timeout should bound queued wait");

  first_worker.join();
  if (first_error) {
    std::rethrow_exception(first_error);
  }

  const auto state = require_upstream_state(
      runtime.upstream_states(), "persistent_http_pool_wait_timeout");
  require(state.active_calls == 0,
          "persistent http pool wait timeout should clear active calls");
  require(state.initialized_persistent_sessions == 1,
          "persistent http pool wait timeout should keep slot initialized");
  require(state.busy_persistent_sessions == 0,
          "persistent http pool wait timeout should clear busy slot");
  require_status(state, UpstreamRuntimeStatus::healthy,
                 "persistent http pool wait timeout should leave upstream "
                 "healthy");

  auto stopped_runtime = runtime.stop();
  require(stopped_runtime.has_value(),
          "persistent http pool wait timeout runtime should stop");
  const auto stopped_server = running->stop();
  require(stopped_server.has_value(),
          "persistent http pool wait timeout fixture should stop");
}

void test_persistent_http_failure_invalidates_session_for_reconnect() {
  const auto kPort = find_available_loopback_port();
  const std::string uri = "http://127.0.0.1:" + std::to_string(kPort) + "/mcp";

  auto server = mcp::ServerPeer::builder()
                    .name("cxxmcp-gateway-persistent-http-reconnect")
                    .version("1.0.0")
                    .streamable_http("127.0.0.1", kPort, "/mcp")
                    .tool<Json, ToolResult>("echo", [](const Json& input) {
                      return ToolResult::text(
                          input.value("value", std::string{}));
                    })
                    .tool<Json, ToolResult>("slow", [](const Json& input) {
                      std::this_thread::sleep_for(
                          std::chrono::milliseconds{
                              input.value("sleepMs", 1000)});
                      return ToolResult::text("slow-done");
                    })
                    .build();
  require(server.has_value(),
          "persistent http reconnect fixture should build");

  auto running = mcp::serve(std::move(*server));
  require(running.has_value(),
          "persistent http reconnect fixture should start");
  running->wait_until_ready();

  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "persistent_http_reconnect";
  upstream.transport = mcp::gateway::UpstreamTransportKind::streamable_http;
  upstream.streamable_http.uri = uri;
  upstream.streamable_http.timeout = std::chrono::milliseconds{250};
  config.upstreams.push_back(std::move(upstream));

  mcp::gateway::GatewayRuntimeOptions options;
  options.upstream_session_mode =
      mcp::gateway::UpstreamSessionMode::persistent;
  mcp::gateway::GatewayRuntime runtime(std::move(config),
                                       std::move(options));

  auto timed_out = runtime.call_tool("persistent_http_reconnect.slow",
                                     Json{{"sleepMs", 1000}});
  require(!timed_out.has_value(),
          "persistent http slow call should time out");
  require_gateway_upstream_timeout(timed_out.error(),
                                   "persistent_http_reconnect");

  const auto degraded = require_upstream_state(runtime.upstream_states(),
                                              "persistent_http_reconnect");
  require_status(degraded, UpstreamRuntimeStatus::degraded,
                 "persistent http timeout should mark upstream degraded");
  require(degraded.active_calls == 0,
          "persistent http timeout should clear active calls");

  std::this_thread::sleep_for(std::chrono::milliseconds{1000});
  auto recovered = runtime.call_tool("persistent_http_reconnect.echo",
                                     Json{{"value", "recovered"}});
  require(recovered.has_value(),
          "persistent http call should reconnect after failed session");
  require_text_result(*recovered, "recovered");

  const auto recovered_state = require_upstream_state(
      runtime.upstream_states(), "persistent_http_reconnect");
  require(recovered_state.active_calls == 0,
          "persistent http reconnect should clear active calls");
  require_status(recovered_state, UpstreamRuntimeStatus::healthy,
                 "persistent http reconnect should restore upstream health");

  auto stopped_runtime = runtime.stop();
  require(stopped_runtime.has_value(),
          "persistent http reconnect runtime should stop");
  const auto stopped_server = running->stop();
  require(stopped_server.has_value(),
          "persistent http reconnect fixture should stop");
}

void test_persistent_http_pool_timeout_recovers() {
  const auto kPort = find_available_loopback_port();
  const std::string uri = "http://127.0.0.1:" + std::to_string(kPort) + "/mcp";

  auto server = mcp::ServerPeer::builder()
                    .name("cxxmcp-gateway-persistent-http-pool-timeout")
                    .version("1.0.0")
                    .streamable_http("127.0.0.1", kPort, "/mcp")
                    .tool<Json, ToolResult>("echo", [](const Json& input) {
                      return ToolResult::text(
                          input.value("value", std::string{}));
                    })
                    .tool<Json, ToolResult>("slow", [](const Json& input) {
                      std::this_thread::sleep_for(
                          std::chrono::milliseconds{
                              input.value("sleepMs", 1000)});
                      return ToolResult::text("slow-done");
                    })
                    .build();
  require(server.has_value(),
          "persistent http pool timeout fixture should build");

  auto running = mcp::serve(std::move(*server));
  require(running.has_value(),
          "persistent http pool timeout fixture should start");
  running->wait_until_ready();

  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "persistent_http_pool_timeout";
  upstream.transport = mcp::gateway::UpstreamTransportKind::streamable_http;
  upstream.streamable_http.uri = uri;
  upstream.streamable_http.timeout = std::chrono::milliseconds{250};
  config.upstreams.push_back(std::move(upstream));

  mcp::gateway::GatewayRuntimeOptions options;
  options.upstream_session_mode =
      mcp::gateway::UpstreamSessionMode::persistent;
  options.persistent_session_pool_size = 2;
  mcp::gateway::GatewayRuntime runtime(std::move(config),
                                       std::move(options));

  auto prewarmed = runtime.refresh_upstream_capabilities();
  require(prewarmed.has_value(),
          "persistent http pool timeout test should prewarm sessions");

  auto timed_out = runtime.call_tool("persistent_http_pool_timeout.slow",
                                     Json{{"sleepMs", 1000}});
  require(!timed_out.has_value(),
          "persistent http pool slow call should time out");
  require_gateway_upstream_timeout(timed_out.error(),
                                   "persistent_http_pool_timeout");

  const auto degraded = require_upstream_state(
      runtime.upstream_states(), "persistent_http_pool_timeout");
  require_status(degraded, UpstreamRuntimeStatus::degraded,
                 "persistent http pool timeout should mark upstream degraded");
  require(degraded.active_calls == 0,
          "persistent http pool timeout should clear active calls");

  std::this_thread::sleep_for(std::chrono::milliseconds{1000});
  auto recovered = runtime.call_tool("persistent_http_pool_timeout.echo",
                                     Json{{"value", "recovered"}});
  require(recovered.has_value(),
          "persistent http pool should recover after timeout");
  require_text_result(*recovered, "recovered");

  auto second = runtime.call_tool("persistent_http_pool_timeout.echo",
                                  Json{{"value", "second"}});
  require(second.has_value(),
          "persistent http pool should continue serving after recovery");
  require_text_result(*second, "second");

  const auto recovered_state = require_upstream_state(
      runtime.upstream_states(), "persistent_http_pool_timeout");
  require(recovered_state.active_calls == 0,
          "persistent http pool recovery should clear active calls");
  require_status(recovered_state, UpstreamRuntimeStatus::healthy,
                 "persistent http pool recovery should restore health");

  auto stopped_runtime = runtime.stop();
  require(stopped_runtime.has_value(),
          "persistent http pool timeout runtime should stop");
  const auto stopped_server = running->stop();
  require(stopped_server.has_value(),
          "persistent http pool timeout fixture should stop");
}

void test_stdio_timeout() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "slow_stdio";
  config.upstreams.front().process_stdio.timeout =
      std::chrono::milliseconds{50};

  mcp::gateway::GatewayRuntime runtime(std::move(config));
  auto called =
      runtime.call_tool("slow_stdio.slow", Json{{"sleepMs", 500}});
  require(!called.has_value(), "slow stdio upstream should time out");
  require_gateway_upstream_timeout(called.error(), "slow_stdio");

  const auto state = require_upstream_state(runtime.upstream_states(),
                                           "slow_stdio");
  require(state.active_calls == 0,
          "timed out stdio call should clear active call count");
  require_status(state, UpstreamRuntimeStatus::degraded,
                 "timed out stdio call should mark upstream degraded");
}

void test_concurrent_http_calls_update_active_state() {
  const auto kPort = find_available_loopback_port();
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
  bool observed_initialized_active = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state = require_upstream_state(runtime.upstream_states(), "busy");
    if (state.active_calls >= 1) {
      observed_active = true;
      if (state.status == UpstreamRuntimeStatus::initialized) {
        observed_initialized_active = true;
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }

  worker.join();
  if (worker_error) {
    std::rethrow_exception(worker_error);
  }
  require(observed_active, "runtime should expose active upstream call count");
  require(observed_initialized_active,
          "runtime should expose initialized state during active upstream call");

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

void test_multi_upstream_tools_list_starts_upstreams_concurrently() {
  mcp::gateway::GatewayConfig config;
  for (const auto* id : {"first_list", "second_list"}) {
    mcp::gateway::UpstreamServer upstream;
    upstream.id = id;
    upstream.transport = mcp::gateway::UpstreamTransportKind::process_stdio;
    upstream.process_stdio.command = CXXMCP_GATEWAY_STDIO_FIXTURE;
    upstream.process_stdio.args = {"--startup-delay-ms", "700"};
    config.upstreams.push_back(std::move(upstream));
  }

  mcp::gateway::GatewayRuntime runtime(std::move(config));
  std::exception_ptr worker_error;
  std::thread worker([&] {
    try {
      auto tools = runtime.list_tools();
      require(tools.has_value(), "parallel multi-upstream tools/list succeeds");
      require(has_tool(*tools, "first_list.echo"),
              "parallel tools/list includes first upstream");
      require(has_tool(*tools, "second_list.echo"),
              "parallel tools/list includes second upstream");
    } catch (...) {
      worker_error = std::current_exception();
    }
  });

  bool observed_both_active = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto states = runtime.upstream_states();
    const auto first = require_upstream_state(states, "first_list");
    const auto second = require_upstream_state(states, "second_list");
    if (first.active_calls >= 1 && second.active_calls >= 1) {
      observed_both_active = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }

  worker.join();
  if (worker_error) {
    std::rethrow_exception(worker_error);
  }
  require(observed_both_active,
          "one tools/list should start eligible upstream list operations "
          "concurrently");

  const auto final_states = runtime.upstream_states();
  require(require_upstream_state(final_states, "first_list").active_calls == 0,
          "first parallel listed upstream should clear active calls");
  require(require_upstream_state(final_states, "second_list").active_calls == 0,
          "second parallel listed upstream should clear active calls");
}

void test_tools_list_uses_cached_catalog_until_cleared() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "cached";
  config.upstreams.front().process_stdio.args = {"--startup-delay-ms", "500"};
  const auto marker_path =
      std::filesystem::temp_directory_path() /
      ("cxxmcp_gateway_stdio_cached_list_marker_" +
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
  auto first = runtime.list_tools();
  require(first.has_value(), "initial tools/list should populate cache");
  require(has_tool(*first, "cached.echo"),
          "initial tools/list should include upstream tool");

  for (int attempt = 0; attempt < 200 && std::filesystem::exists(marker_path);
       ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(!std::filesystem::exists(marker_path),
          "initial cached-list upstream process should be cleaned up");

  std::atomic_bool second_done = false;
  std::exception_ptr second_error;
  std::thread second_worker([&] {
    try {
      auto second = runtime.list_tools();
      require(second.has_value(), "cached tools/list should succeed");
      require(has_tool(*second, "cached.echo"),
              "cached tools/list should include upstream tool");
    } catch (...) {
      second_error = std::current_exception();
    }
    second_done = true;
  });

  bool observed_second_marker = false;
  for (int attempt = 0; attempt < 50; ++attempt) {
    if (std::filesystem::exists(marker_path)) {
      observed_second_marker = true;
      break;
    }
    if (second_done.load()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  second_worker.join();
  if (second_error) {
    std::rethrow_exception(second_error);
  }
  require(!observed_second_marker,
          "cached tools/list should not start a new upstream process");

  auto cleared = runtime.clear_cached_catalogs();
  require(cleared.has_value(), "clear_cached_catalogs should succeed");

  std::atomic_bool third_done = false;
  std::exception_ptr third_error;
  std::thread third_worker([&] {
    try {
      auto third = runtime.list_tools();
      require(third.has_value(),
              "tools/list after clearing cache should succeed");
      require(has_tool(*third, "cached.echo"),
              "tools/list after clearing cache should include upstream tool");
    } catch (...) {
      third_error = std::current_exception();
    }
    third_done = true;
  });

  bool observed_third_marker = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (std::filesystem::exists(marker_path)) {
      observed_third_marker = true;
      break;
    }
    if (third_done.load()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  third_worker.join();
  if (third_error) {
    std::rethrow_exception(third_error);
  }
  require(observed_third_marker,
          "tools/list after clearing cache should start upstream again");
}

void test_clear_cached_catalogs_keeps_persistent_session() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "persistent_cached";
  const auto marker_path =
      std::filesystem::temp_directory_path() /
      ("cxxmcp_gateway_stdio_persistent_cached_marker_" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()) +
       ".txt");
  std::error_code ignored;
  std::filesystem::remove(marker_path, ignored);
  config.upstreams.front()
      .process_stdio.env["CXXMCP_GATEWAY_STDIO_MARKER_FILE"] =
      marker_path.string();

  mcp::gateway::GatewayRuntimeOptions options;
  options.upstream_session_mode =
      mcp::gateway::UpstreamSessionMode::persistent;
  mcp::gateway::GatewayRuntime runtime(std::move(config),
                                       std::move(options));

  auto first = runtime.list_tools();
  require(first.has_value(),
          "persistent cached tools/list should populate cache");
  require(has_tool(*first, "persistent_cached.echo"),
          "persistent cached tools/list should include upstream tool");
  require(std::filesystem::exists(marker_path),
          "persistent cached tools/list should keep session alive");

  auto cleared = runtime.clear_cached_catalogs();
  require(cleared.has_value(),
          "clear_cached_catalogs should succeed in persistent mode");
  require(std::filesystem::exists(marker_path),
          "clear_cached_catalogs should not stop persistent upstream session");

  auto second = runtime.list_tools();
  require(second.has_value(),
          "persistent tools/list after cache clear should succeed");
  require(has_tool(*second, "persistent_cached.echo"),
          "persistent tools/list after cache clear should include upstream "
          "tool");
  require(std::filesystem::exists(marker_path),
          "persistent tools/list after cache clear should keep session alive");

  auto stopped = runtime.stop();
  require(stopped.has_value(), "persistent cached runtime should stop");

  bool marker_removed = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (!std::filesystem::exists(marker_path)) {
      marker_removed = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(marker_removed,
          "persistent cached runtime stop should clean up session");
}

void test_cancellation_and_progress_notifications_are_local_noops() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "notify";
  mcp::gateway::GatewayRuntime runtime(std::move(config));

  const auto before_capabilities = runtime.server_capabilities();
  require(before_capabilities.tools.enabled,
          "enabled upstream should advertise tools before notification no-op");
  require(!before_capabilities.tools.list_changed,
          "notification no-op test should not advertise tools/listChanged");
  require(before_capabilities.resources.enabled,
          "notification no-op test should keep resources advertised");
  require(!before_capabilities.resources.list_changed,
          "notification no-op test should not advertise resources/listChanged");
  require(!before_capabilities.resources.subscribe,
          "notification no-op test should not advertise resource "
          "subscriptions");
  require(before_capabilities.prompts.enabled,
          "notification no-op test should keep prompts advertised");
  require(!before_capabilities.prompts.list_changed,
          "notification no-op test should not advertise prompts/listChanged");
  require(!before_capabilities.tasks.has_value(),
          "notification no-op test should not advertise tasks");

  std::exception_ptr worker_error;
  std::thread worker([&] {
    try {
      auto called = runtime.call_tool("notify.slow", Json{{"sleepMs", 600}});
      require(called.has_value(),
              "cancel/progress notifications should not cancel active calls");
      require_text_result(*called, "slow-done");
    } catch (...) {
      worker_error = std::current_exception();
    }
  });

  bool observed_active = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state =
        require_upstream_state(runtime.upstream_states(), "notify");
    if (state.active_calls >= 1) {
      observed_active = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(observed_active,
          "notification no-op test should observe an active upstream call");

  const std::vector<std::pair<std::string, Json>> notifications{
      {std::string(mcp::protocol::ToolsListChangedNotificationMethod),
       Json::object()},
      {std::string(mcp::protocol::ResourcesListChangedNotificationMethod),
       Json::object()},
      {std::string(mcp::protocol::ResourcesUpdatedNotificationMethod),
       Json{{"uri", "file:///fixture/readme.txt"}}},
      {std::string(mcp::protocol::PromptsListChangedNotificationMethod),
       Json::object()},
      {std::string(mcp::protocol::RootsListChangedNotificationMethod),
       Json::object()},
      {std::string(mcp::protocol::LoggingMessageNotificationMethod),
       Json{{"level", "info"}, {"data", "gateway-log-message"}}},
      {std::string(mcp::protocol::TasksStatusNotificationMethod),
       Json{{"taskId", "task-1"}, {"status", "running"}}},
      {std::string(mcp::protocol::ElicitationCompleteNotificationMethod),
       Json{{"requestId", "elicitation-1"}, {"action", "cancel"}}},
      {std::string(mcp::protocol::CancelledNotificationMethod),
       Json{{"requestId", std::int64_t{1}},
            {"reason", "downstream cancelled"}}},
      {std::string(mcp::protocol::ProgressNotificationMethod),
       Json{{"progressToken", "call-1"}, {"progress", 0.5}}},
  };
  for (const auto& [method, params] : notifications) {
    auto accepted = runtime.handle_notification(
        mcp::protocol::make_notification(method, params));
    require(accepted.has_value(),
            "unsupported notifications are accepted as local no-ops in MVP");
  }

  const auto during =
      require_upstream_state(runtime.upstream_states(), "notify");
  require(during.active_calls >= 1,
          "notification no-ops should not clear active call state");

  worker.join();
  if (worker_error) {
    std::rethrow_exception(worker_error);
  }

  const auto state =
      require_upstream_state(runtime.upstream_states(), "notify");
  require(state.active_calls == 0,
          "notification no-op test should clear active call count");
  require_status(state, UpstreamRuntimeStatus::healthy,
                 "notification no-ops should leave successful upstream healthy");

  const auto after_capabilities = runtime.server_capabilities();
  require(after_capabilities.tools.enabled,
          "notification no-ops should not remove tools advertisement");
  require(!after_capabilities.tools.list_changed,
          "notification no-ops should not add tools/listChanged");
  require(after_capabilities.resources.enabled,
          "notification no-ops should keep resources advertised");
  require(!after_capabilities.resources.list_changed,
          "notification no-ops should not add resources/listChanged");
  require(!after_capabilities.resources.subscribe,
          "notification no-ops should not add resource subscriptions");
  require(after_capabilities.prompts.enabled,
          "notification no-ops should keep prompts advertised");
  require(!after_capabilities.prompts.list_changed,
          "notification no-ops should not add prompts/listChanged");
  require(!after_capabilities.tasks.has_value(),
          "notification no-ops should not add tasks");
  require(after_capabilities.completions.enabled,
          "successful upstream call should allow completion advertisement "
          "after capabilities are discovered");
  require_mvp_server_capability_json_shape(after_capabilities, true);
}

void test_runtime_stop_waits_for_active_stdio_call() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "stdio_stop";
  config.upstreams.front().process_stdio.timeout =
      std::chrono::milliseconds{2000};
  const auto marker_path =
      std::filesystem::temp_directory_path() /
      ("cxxmcp_gateway_stdio_marker_" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()) +
       ".txt");
  const auto slow_marker_path =
      std::filesystem::temp_directory_path() /
      ("cxxmcp_gateway_stdio_slow_marker_" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()) +
       ".txt");
  std::error_code ignored;
  std::filesystem::remove(marker_path, ignored);
  std::filesystem::remove(slow_marker_path, ignored);
  config.upstreams.front()
      .process_stdio.env["CXXMCP_GATEWAY_STDIO_MARKER_FILE"] =
      marker_path.string();
  config.upstreams.front()
      .process_stdio.env["CXXMCP_GATEWAY_STDIO_SLOW_MARKER_FILE"] =
      slow_marker_path.string();
  mcp::gateway::GatewayRuntimeOptions options;
  options.active_call_drain_timeout = std::chrono::milliseconds{5000};
  mcp::gateway::GatewayRuntime runtime(std::move(config),
                                       std::move(options));

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
  bool observed_slow_marker = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state =
        require_upstream_state(runtime.upstream_states(), "stdio_stop");
    if (state.active_calls >= 1) {
      observed_active = true;
    }
    if (std::filesystem::exists(marker_path)) {
      observed_marker = true;
    }
    if (std::filesystem::exists(slow_marker_path)) {
      observed_slow_marker = true;
    }
    if (observed_active && observed_marker && observed_slow_marker) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(observed_active, "stdio shutdown test should observe active call");
  require(observed_marker, "stdio shutdown test should observe child process marker");
  require(observed_slow_marker,
          "stdio shutdown test should observe slow handler marker");

  std::exception_ptr stop_error;
  const auto stop_started = std::chrono::steady_clock::now();
  std::thread stopper([&] {
    try {
      auto stopped = runtime.stop();
      require(stopped.has_value(),
              "runtime stop should succeed while stdio call is active");
    } catch (...) {
      stop_error = std::current_exception();
    }
  });

  bool observed_stopping = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state =
        require_upstream_state(runtime.upstream_states(), "stdio_stop");
    if (state.status == UpstreamRuntimeStatus::stopping) {
      observed_stopping = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }

  require(observed_stopping,
          "runtime stop should expose stopping state while stdio call drains");

  auto stopping_list = runtime.list_tools();
  require(!stopping_list.has_value(),
          "runtime should reject tools/list while stopping");
  require_runtime_stopping_error(stopping_list.error(), "tools/list");

  auto stopping_call =
      runtime.call_tool("stdio_stop.echo", Json{{"value", "during-stop"}});
  require(!stopping_call.has_value(),
          "runtime should reject tools/call while stopping");
  require_runtime_stopping_error(stopping_call.error(), "tools/call");

  auto stopping_resources = runtime.list_resources();
  require(!stopping_resources.has_value(),
          "runtime should reject resources/list while stopping");
  require_runtime_stopping_error(stopping_resources.error(), "resources/list");

  auto stopping_resource_templates = runtime.list_resource_templates();
  require(!stopping_resource_templates.has_value(),
          "runtime should reject resources/templates/list while stopping");
  require_runtime_stopping_error(stopping_resource_templates.error(),
                                 "resources/templates/list");

  const auto stopping_resource_uri =
      mcp::gateway::GatewayRouter::expose_resource_uri(
          "stdio_stop", "file:///during-stop.txt");
  auto stopping_read = runtime.read_resource(stopping_resource_uri);
  require(!stopping_read.has_value(),
          "runtime should reject resources/read while stopping");
  require_runtime_stopping_error(stopping_read.error(), "resources/read");

  auto stopping_prompts = runtime.list_prompts();
  require(!stopping_prompts.has_value(),
          "runtime should reject prompts/list while stopping");
  require_runtime_stopping_error(stopping_prompts.error(), "prompts/list");

  auto stopping_prompt = runtime.get_prompt("stdio_stop.summarize",
                                            Json{{"text", "during-stop"}});
  require(!stopping_prompt.has_value(),
          "runtime should reject prompts/get while stopping");
  require_runtime_stopping_error(stopping_prompt.error(), "prompts/get");

  mcp::protocol::CompleteParams stopping_completion;
  stopping_completion.ref =
      mcp::protocol::prompt_completion_reference("stdio_stop.summarize");
  stopping_completion.argument.name = "text";
  stopping_completion.argument.value = "during-stop";
  auto stopping_complete = runtime.complete(std::move(stopping_completion));
  require(!stopping_complete.has_value(),
          "runtime should reject completion/complete while stopping");
  require_runtime_stopping_error(stopping_complete.error(),
                                 "completion/complete");

  auto stopping_refresh = runtime.refresh_upstream_capabilities();
  require(!stopping_refresh.has_value(),
          "runtime should reject capability refresh while stopping");
  require_runtime_stopping_error(stopping_refresh.error(),
                                 "refresh_upstream_capabilities");

  stopper.join();
  if (stop_error) {
    std::rethrow_exception(stop_error);
  }
  const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;
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
  std::filesystem::remove(slow_marker_path, ignored);

  auto post_stop_call =
      runtime.call_tool("stdio_stop.echo", Json{{"value", "after-stop"}});
  require(!post_stop_call.has_value(),
          "runtime should reject stdio calls after stop");

  auto post_stop_list = runtime.list_tools();
  require(!post_stop_list.has_value(),
          "runtime should reject tools/list after stop");
}

void test_runtime_stop_timeout_bounds_active_stdio_call_wait() {
  auto config = make_stdio_config();
  config.upstreams.front().id = "stdio_stop_timeout";
  config.upstreams.front().process_stdio.timeout =
      std::chrono::milliseconds{3000};
  const auto marker_path =
      std::filesystem::temp_directory_path() /
      ("cxxmcp_gateway_stdio_stop_timeout_marker_" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()) +
       ".txt");
  const auto slow_marker_path =
      std::filesystem::temp_directory_path() /
      ("cxxmcp_gateway_stdio_stop_timeout_slow_marker_" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()) +
       ".txt");
  std::error_code ignored;
  std::filesystem::remove(marker_path, ignored);
  std::filesystem::remove(slow_marker_path, ignored);
  config.upstreams.front()
      .process_stdio.env["CXXMCP_GATEWAY_STDIO_MARKER_FILE"] =
      marker_path.string();
  config.upstreams.front()
      .process_stdio.env["CXXMCP_GATEWAY_STDIO_SLOW_MARKER_FILE"] =
      slow_marker_path.string();

  mcp::gateway::GatewayRuntimeOptions options;
  options.active_call_drain_timeout = std::chrono::milliseconds{100};
  mcp::gateway::GatewayRuntime runtime(std::move(config),
                                       std::move(options));

  std::exception_ptr worker_error;
  std::thread worker([&] {
    try {
      auto called = runtime.call_tool("stdio_stop_timeout.slow",
                                      Json{{"sleepMs", 700}});
      require(called.has_value(),
              "active drain timeout test slow stdio call should succeed");
      require_text_result(*called, "slow-done");
    } catch (...) {
      worker_error = std::current_exception();
    }
  });

  bool observed_active = false;
  bool observed_marker = false;
  bool observed_slow_marker = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state =
        require_upstream_state(runtime.upstream_states(), "stdio_stop_timeout");
    if (state.active_calls >= 1) {
      observed_active = true;
    }
    if (std::filesystem::exists(marker_path)) {
      observed_marker = true;
    }
    if (std::filesystem::exists(slow_marker_path)) {
      observed_slow_marker = true;
    }
    if (observed_active && observed_marker && observed_slow_marker) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(observed_active,
          "active drain timeout test should observe active stdio call");
  require(observed_marker,
          "active drain timeout test should observe child process marker");
  require(observed_slow_marker,
          "active drain timeout test should observe slow handler marker");

  const auto stop_started = std::chrono::steady_clock::now();
  auto timed_out = runtime.stop();
  const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;
  require(!timed_out.has_value(),
          "runtime stop should fail when active call drain times out");
  require_active_call_drain_timeout(timed_out.error());
  require(stop_elapsed < std::chrono::milliseconds{500},
          "active call drain timeout should bound runtime stop wait");

  const auto stopping =
      require_upstream_state(runtime.upstream_states(), "stdio_stop_timeout");
  require(stopping.active_calls >= 1,
          "active drain timeout should leave active call observable");
  require_status(stopping, UpstreamRuntimeStatus::stopping,
                 "active drain timeout should leave runtime stopping");

  worker.join();
  if (worker_error) {
    std::rethrow_exception(worker_error);
  }

  auto stopped = runtime.stop();
  require(stopped.has_value(),
          "runtime stop should complete after active stdio call drains");
  const auto stopped_state =
      require_upstream_state(runtime.upstream_states(), "stdio_stop_timeout");
  require(stopped_state.active_calls == 0,
          "active drain timeout follow-up stop should clear active calls");
  require_status(stopped_state, UpstreamRuntimeStatus::stopped,
                 "active drain timeout follow-up stop should mark stopped");
  std::filesystem::remove(slow_marker_path, ignored);
}

void test_runtime_stop_waits_for_active_http_call() {
  const auto kPort = find_available_loopback_port();
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

  auto post_stop_resource_list = runtime.list_resources();
  require(!post_stop_resource_list.has_value(),
          "runtime should reject resources/list after stop");

  auto post_stop_resource_templates = runtime.list_resource_templates();
  require(!post_stop_resource_templates.has_value(),
          "runtime should reject resources/templates/list after stop");

  const auto post_stop_resource_uri =
      mcp::gateway::GatewayRouter::expose_resource_uri(
          "shutdown", "file:///after-stop.txt");
  auto post_stop_resource_read = runtime.read_resource(post_stop_resource_uri);
  require(!post_stop_resource_read.has_value(),
          "runtime should reject resources/read after stop");

  auto post_stop_prompt_list = runtime.list_prompts();
  require(!post_stop_prompt_list.has_value(),
          "runtime should reject prompts/list after stop");

  auto post_stop_prompt =
      runtime.get_prompt("shutdown.prompt", Json::object());
  require(!post_stop_prompt.has_value(),
          "runtime should reject prompts/get after stop");

  auto post_stop_refresh = runtime.refresh_upstream_capabilities();
  require(!post_stop_refresh.has_value(),
          "runtime should reject capability refresh after stop");

  mcp::protocol::CompleteParams post_stop_completion;
  post_stop_completion.ref =
      mcp::protocol::prompt_completion_reference("shutdown.slow");
  post_stop_completion.argument.name = "value";
  post_stop_completion.argument.value = "after-stop";
  auto post_stop_complete = runtime.complete(std::move(post_stop_completion));
  require(!post_stop_complete.has_value(),
          "runtime should reject completions after stop");

  const auto stopped_server = running->stop();
  require(stopped_server.has_value(), "shutdown fixture should stop");
}

void test_runtime_stop_timeout_bounds_active_http_call_wait() {
  const auto kPort = find_available_loopback_port();
  const std::string uri = "http://127.0.0.1:" + std::to_string(kPort) + "/mcp";
  std::atomic_bool slow_entered = false;

  auto server = mcp::ServerPeer::builder()
                    .name("cxxmcp-gateway-shutdown-timeout-fixture")
                    .version("1.0.0")
                    .streamable_http("127.0.0.1", kPort, "/mcp")
                    .tool<Json, ToolResult>("slow", [&](const Json& input) {
                      slow_entered = true;
                      std::this_thread::sleep_for(
                          std::chrono::milliseconds{
                              input.value("sleepMs", 700)});
                      return ToolResult::text("slow-done");
                    })
                    .build();
  require(server.has_value(), "shutdown timeout fixture should build");

  auto running = mcp::serve(std::move(*server));
  require(running.has_value(), "shutdown timeout fixture should start");
  running->wait_until_ready();

  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "shutdown_timeout";
  upstream.transport = mcp::gateway::UpstreamTransportKind::streamable_http;
  upstream.streamable_http.uri = uri;
  upstream.streamable_http.timeout = std::chrono::seconds{3};
  config.upstreams.push_back(std::move(upstream));

  mcp::gateway::GatewayRuntimeOptions options;
  options.active_call_drain_timeout = std::chrono::milliseconds{100};
  mcp::gateway::GatewayRuntime runtime(std::move(config),
                                       std::move(options));

  std::exception_ptr worker_error;
  std::thread worker([&] {
    try {
      auto called = runtime.call_tool("shutdown_timeout.slow",
                                      Json{{"sleepMs", 700}});
      require(called.has_value(),
              "active drain timeout http slow call should succeed");
      require_text_result(*called, "slow-done");
    } catch (...) {
      worker_error = std::current_exception();
    }
  });

  bool observed_active = false;
  bool observed_handler = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state =
        require_upstream_state(runtime.upstream_states(), "shutdown_timeout");
    if (state.active_calls >= 1) {
      observed_active = true;
    }
    if (slow_entered.load()) {
      observed_handler = true;
    }
    if (observed_active && observed_handler) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(observed_active,
          "active drain timeout http test should observe active call");
  require(observed_handler,
          "active drain timeout http test should observe slow handler entry");

  const auto stop_started = std::chrono::steady_clock::now();
  auto timed_out = runtime.stop();
  const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;
  require(!timed_out.has_value(),
          "runtime stop should fail when active http drain times out");
  require_active_call_drain_timeout(timed_out.error());
  require(stop_elapsed < std::chrono::milliseconds{500},
          "active http call drain timeout should bound runtime stop wait");

  const auto stopping =
      require_upstream_state(runtime.upstream_states(), "shutdown_timeout");
  require(stopping.active_calls >= 1,
          "active http drain timeout should leave active call observable");
  require_status(stopping, UpstreamRuntimeStatus::stopping,
                 "active http drain timeout should leave runtime stopping");

  worker.join();
  if (worker_error) {
    std::rethrow_exception(worker_error);
  }

  auto stopped_runtime = runtime.stop();
  require(stopped_runtime.has_value(),
          "runtime stop should complete after active http call drains");
  const auto stopped_state =
      require_upstream_state(runtime.upstream_states(), "shutdown_timeout");
  require(stopped_state.active_calls == 0,
          "active http drain timeout follow-up stop should clear active calls");
  require_status(stopped_state, UpstreamRuntimeStatus::stopped,
                 "active http drain timeout follow-up stop should mark "
                 "stopped");

  const auto stopped_server = running->stop();
  require(stopped_server.has_value(),
          "shutdown timeout fixture should stop");
}

void test_concurrent_calls_to_multiple_upstreams() {
  const auto [kFirstPort, kSecondPort] = find_two_distinct_loopback_ports();
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
  const auto kPort = find_available_loopback_port();

  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "down";
  upstream.transport = mcp::gateway::UpstreamTransportKind::streamable_http;
  upstream.streamable_http.uri =
      "http://127.0.0.1:" + std::to_string(kPort) + "/mcp";
  upstream.streamable_http.timeout = std::chrono::milliseconds{250};
  config.upstreams.push_back(std::move(upstream));

  mcp::gateway::GatewayRuntime runtime(std::move(config));
  auto tools = runtime.list_tools();
  require(!tools.has_value(), "unavailable http upstream should fail list");
  require_gateway_upstream_error(tools.error(), "down");
}

void test_http_malformed_response_before_initialize() {
  const auto kPort = find_available_loopback_port();
  const std::string uri = "http://127.0.0.1:" + std::to_string(kPort) + "/mcp";

  SocketRuntime sockets;
  std::atomic_bool server_ready{false};
  std::exception_ptr server_error;
  std::thread server([&] {
    try {
      serve_one_raw_http_response(kPort, "{not-json}\n", server_ready);
    } catch (...) {
      server_error = std::current_exception();
      server_ready.store(true);
    }
  });

  for (int attempt = 0; attempt < 200 && !server_ready.load(); ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(server_ready.load(), "malformed HTTP fixture should become ready");

  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "malformed_http";
  upstream.transport = mcp::gateway::UpstreamTransportKind::streamable_http;
  upstream.streamable_http.uri = uri;
  upstream.streamable_http.timeout = std::chrono::seconds{3};
  config.upstreams.push_back(std::move(upstream));

  mcp::gateway::GatewayRuntime runtime(std::move(config));
  auto tools = runtime.list_tools();

  server.join();
  if (server_error) {
    std::rethrow_exception(server_error);
  }

  require(!tools.has_value(), "malformed http upstream should fail tools/list");
  require_gateway_upstream_protocol(tools.error(), "malformed_http");

  const auto state =
      require_upstream_state(runtime.upstream_states(), "malformed_http");
  require_status(state, UpstreamRuntimeStatus::degraded,
                 "malformed http response should mark upstream degraded");
  require(state.last_error.has_value(),
          "malformed http response should record last error");
  require_gateway_upstream_protocol(*state.last_error, "malformed_http");
}

void test_start_http_invalid_config_fails_before_binding_port() {
  const auto kPort = find_available_loopback_port();

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

void test_hosted_gateway_rejects_invalid_endpoint_options() {
  const auto kPort = find_available_loopback_port();

  mcp::gateway::GatewayRuntime runtime(make_disabled_config("endpoint"));

  auto wait_before_start = runtime.wait();
  require(!wait_before_start.has_value(),
          "wait should fail before hosted endpoint startup");
  require(wait_before_start.error().message ==
              "gateway http endpoint is not running",
          "wait-before-start should report stable lifecycle error");

  auto empty_host =
      runtime.start_http({.host = "", .port = kPort, .path = "/mcp"});
  require(!empty_host.has_value(), "empty host should fail endpoint startup");
  require(empty_host.error().message == "gateway http host must not be empty",
          "empty host should report stable validation error");

  auto zero_port =
      runtime.start_http({.host = "127.0.0.1", .port = 0, .path = "/mcp"});
  require(!zero_port.has_value(), "zero port should fail endpoint startup");
  require(zero_port.error().message == "gateway http port must not be zero",
          "zero port should report stable validation error");

  auto stopped = runtime.stop();
  require(stopped.has_value(),
          "runtime should stop after rejected endpoint options");
}

void test_hosted_gateway_http_endpoint_stops_while_idle() {
  const auto kPort = find_available_loopback_port();

  mcp::gateway::GatewayRuntime gateway(make_disabled_config());
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

void test_runtime_stop_observer_can_reenter_lifecycle_api() {
  mcp::gateway::GatewayRuntime* runtime_ptr = nullptr;
  bool saw_stopping = false;
  bool wait_failed_without_endpoint = false;
  bool states_remained_available = false;

  mcp::gateway::GatewayRuntimeOptions options;
  options.observer = [&](const mcp::gateway::GatewayRuntimeEvent& event) {
    if (!runtime_ptr ||
        event.kind != mcp::gateway::GatewayRuntimeEventKind::runtime_stopping) {
      return;
    }
    saw_stopping = true;
    auto wait_result = runtime_ptr->wait();
    wait_failed_without_endpoint =
        !wait_result.has_value() &&
        wait_result.error().message == "gateway http endpoint is not running";
    states_remained_available = !runtime_ptr->upstream_states().empty();
  };

  mcp::gateway::GatewayRuntime runtime(make_disabled_config("reentrant"),
                                       std::move(options));
  runtime_ptr = &runtime;
  auto stopped = runtime.stop();
  require(stopped.has_value(),
          "runtime stop should allow observer lifecycle reentry");
  require(saw_stopping, "observer should see runtime stopping event");
  require(wait_failed_without_endpoint,
          "observer should reenter wait without deadlocking service mutex");
  require(states_remained_available,
          "observer should reenter state snapshot during stop");
}

void test_runtime_wait_and_stop_can_overlap() {
  const auto kPort = find_available_loopback_port();

  mcp::gateway::GatewayRuntime gateway(make_disabled_config("wait_stop"));
  auto started = gateway.start_http(
      {.host = "127.0.0.1", .port = kPort, .path = "/mcp"});
  require(started.has_value(),
          "overlapping wait/stop gateway endpoint should start");

  std::atomic_bool wait_returned = false;
  std::atomic_bool wait_succeeded = false;
  std::thread waiter([&] {
    auto waited = gateway.wait();
    wait_succeeded = waited.has_value();
    wait_returned = true;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds{50});
  auto stopped = gateway.stop();
  require(stopped.has_value(),
          "gateway stop should succeed while another thread waits");
  waiter.join();
  require(wait_returned,
          "gateway wait should return after overlapping stop completes");
  require(wait_succeeded, "gateway wait should complete successfully");
}

void test_hosted_gateway_rejects_invalid_json_rpc_request() {
  const auto kPort = find_available_loopback_port();

  mcp::gateway::GatewayRuntime gateway(make_disabled_config());
  auto started = gateway.start_http(
      {.host = "127.0.0.1", .port = kPort, .path = "/mcp"});
  require(started.has_value(), "invalid JSON-RPC gateway should start");

  const auto response = post_raw_http(kPort, "/mcp", "{not-json}");
  require(response.find(" 400 ") != std::string::npos,
          "invalid JSON-RPC request should return HTTP 400");
  require(response.find("-32700") != std::string::npos,
          "invalid JSON-RPC request should map to JSON-RPC parse error");

  auto stopped = gateway.stop();
  require(stopped.has_value(), "invalid JSON-RPC gateway should stop");
}

void test_runtime_move_assignment_stops_existing_endpoint() {
  const auto [kOldPort, kMovedPort] = find_two_distinct_loopback_ports();

  mcp::gateway::GatewayRuntime gateway(make_disabled_config("old"));
  auto started = gateway.start_http(
      {.host = "127.0.0.1", .port = kOldPort, .path = "/mcp"});
  require(started.has_value(),
          "move-assigned gateway source endpoint should start");

  mcp::gateway::GatewayRuntime replacement(make_stdio_config());
  gateway = std::move(replacement);

  mcp::gateway::GatewayRuntime rebound(make_disabled_config("rebound"));
  auto rebound_started = rebound.start_http(
      {.host = "127.0.0.1", .port = kOldPort, .path = "/mcp"});
  require(rebound_started.has_value(),
          "move assignment should stop the replaced gateway endpoint");
  auto rebound_stopped = rebound.stop();
  require(rebound_stopped.has_value(),
          "rebound gateway should stop after port reuse check");

  auto moved_started = gateway.start_http(
      {.host = "127.0.0.1", .port = kMovedPort, .path = "/mcp"});
  require(moved_started.has_value(),
          "move-assigned gateway should retain moved-in runtime");
  auto moved_stopped = gateway.stop();
  require(moved_stopped.has_value(), "move-assigned gateway should stop");
}

void test_hosted_gateway_http_endpoint() {
  const auto kPort = find_available_loopback_port();

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
  require(capabilities->resources.enabled,
          "gateway should advertise resources");
  require(!capabilities->resources.list_changed,
          "gateway should not advertise resources/listChanged");
  require(!capabilities->resources.subscribe,
          "gateway should not advertise resource subscriptions");
  require(capabilities->prompts.enabled,
          "gateway should advertise prompts");
  require(!capabilities->prompts.list_changed,
          "gateway should not advertise prompts/listChanged");
  require(!capabilities->completions.enabled,
          "gateway should not advertise completions");
  require(!capabilities->tasks.has_value(),
          "gateway should not advertise tasks");

  auto notified = running_client->peer().notify_initialized();
  require(notified.has_value(), "downstream initialized notification should work");

  const std::vector<std::pair<std::string, Json>> unsupported_notifications{
      {std::string(mcp::protocol::ToolsListChangedNotificationMethod),
       Json::object()},
      {std::string(mcp::protocol::ResourcesListChangedNotificationMethod),
       Json::object()},
      {std::string(mcp::protocol::ResourcesUpdatedNotificationMethod),
       Json{{"uri", "file:///fixture/readme.txt"}}},
      {std::string(mcp::protocol::PromptsListChangedNotificationMethod),
       Json::object()},
      {std::string(mcp::protocol::RootsListChangedNotificationMethod),
       Json::object()},
      {std::string(mcp::protocol::LoggingMessageNotificationMethod),
       Json{{"level", "info"}, {"data", "gateway-log-message"}}},
  };
  for (const auto& [method, params] : unsupported_notifications) {
    auto unsupported_notification = running_client->peer().raw_notification(
        mcp::protocol::make_notification(method, params));
    require(unsupported_notification.has_value(),
            "unsupported downstream notifications should be ignored");
  }

  auto tools = running_client->peer().list_all_tools();
  require(tools.has_value(), "downstream tools/list should succeed");
  require(has_tool(*tools, "stdio.echo"),
          "downstream tools/list should include routed stdio tool");

  auto called = running_client->peer().call_tool(
      "stdio.echo", Json{{"value", "from-downstream"}});
  require(called.has_value(), "downstream tools/call should route");
  require_text_result(*called, "from-downstream");

  const auto downstream_resource_uri =
      mcp::gateway::GatewayRouter::expose_resource_uri(
          "stdio", "file:///fixture/readme.txt");
  auto resources = running_client->peer().list_all_resources();
  require(resources.has_value(), "downstream resources/list should succeed");
  require(has_resource(*resources, downstream_resource_uri),
          "downstream resources/list should include routed stdio resource");
  auto read = running_client->peer().read_resource(downstream_resource_uri);
  require(read.has_value(), "downstream resources/read should route");
  require_text_resource(*read, downstream_resource_uri,
                        "hello from stdio resource");

  const auto downstream_template_uri =
      mcp::gateway::GatewayRouter::expose_resource_template_uri(
          "stdio", "file:///fixture/{path}");
  auto resource_templates =
      running_client->peer().list_all_resource_templates();
  require(resource_templates.has_value(),
          "downstream resources/templates/list should succeed");
  require(has_resource_template(*resource_templates, downstream_template_uri),
          "downstream resources/templates/list should include routed stdio "
          "template");

  auto prompts = running_client->peer().list_all_prompts();
  require(prompts.has_value(), "downstream prompts/list should succeed");
  require(has_prompt(*prompts, "stdio.summarize"),
          "downstream prompts/list should include routed stdio prompt");
  auto prompt = running_client->peer().get_prompt(
      "stdio.summarize", Json{{"text", "from-downstream"}});
  require(prompt.has_value(), "downstream prompts/get should route");
  require_text_prompt(*prompt, "Summarize from-downstream");

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
  const auto kPort = find_available_loopback_port();

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

void test_hosted_gateway_rejects_request_before_initialize() {
  const auto kPort = find_available_loopback_port();

  mcp::gateway::GatewayRuntime gateway(make_stdio_config());
  auto started = gateway.start_http(
      {.host = "127.0.0.1", .port = kPort, .path = "/mcp"});
  require(started.has_value(),
          "pre-initialize hosted gateway endpoint should start");

  const auto response = post_raw_http(
      kPort, "/mcp",
      R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})");
  require(response.find("HTTP/1.1 404") != std::string::npos,
          "pre-initialize tools/list should be rejected before gateway "
          "routing");
  require(response.find("http transport has no active session") !=
              std::string::npos,
          "pre-initialize tools/list rejection should explain session state");

  const auto state = require_upstream_state(gateway.upstream_states(), "stdio");
  require_status(state, UpstreamRuntimeStatus::configured,
                 "pre-initialize hosted request should not start upstream");
  require(state.active_calls == 0,
          "pre-initialize hosted request should not create active calls");
  require(!state.capabilities.has_value(),
          "pre-initialize hosted request should not initialize capabilities");
  require(!state.last_error.has_value(),
          "pre-initialize hosted request should not record upstream errors");

  auto stopped_gateway = gateway.stop();
  require(stopped_gateway.has_value(),
          "pre-initialize hosted gateway should stop");
}

void test_hosted_gateway_multiple_downstream_clients() {
  const auto kPort = find_available_loopback_port();

  mcp::gateway::GatewayRuntime gateway(make_stdio_config());
  auto started = gateway.start_http(
      {.host = "127.0.0.1", .port = kPort, .path = "/mcp"});
  require(started.has_value(),
          "multi-client hosted gateway endpoint should start");

  auto make_client = [kPort] {
    return mcp::ClientPeer::builder()
        .streamable_http("http://127.0.0.1:" + std::to_string(kPort) + "/mcp")
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

void test_hosted_cancellation_notifications_do_not_cancel_upstream_call() {
  const auto kPort = find_available_loopback_port();

  mcp::gateway::GatewayRuntime gateway(make_stdio_config());
  auto started = gateway.start_http(
      {.host = "127.0.0.1", .port = kPort, .path = "/mcp"});
  require(started.has_value(),
          "hosted notification no-op gateway should start");

  auto make_client = [kPort] {
    return mcp::ClientPeer::builder()
        .streamable_http("http://127.0.0.1:" + std::to_string(kPort) + "/mcp")
        .build();
  };

  auto caller_client = make_client();
  auto notifier_client = make_client();
  require(caller_client.has_value(), "caller downstream client should build");
  require(notifier_client.has_value(),
          "notifier downstream client should build");

  auto caller = mcp::serve(std::move(*caller_client));
  auto notifier = mcp::serve(std::move(*notifier_client));
  require(caller.has_value(), "caller downstream client should start");
  require(notifier.has_value(), "notifier downstream client should start");

  auto caller_initialized = caller->peer().initialize(
      "cxxmcp-gateway-hosted-cancel-caller", "1.0.0");
  auto notifier_initialized = notifier->peer().initialize(
      "cxxmcp-gateway-hosted-cancel-notifier", "1.0.0");
  require(caller_initialized.has_value(),
          "caller downstream initialize should succeed");
  require(notifier_initialized.has_value(),
          "notifier downstream initialize should succeed");

  auto caller_notified = caller->peer().notify_initialized();
  auto notifier_notified = notifier->peer().notify_initialized();
  require(caller_notified.has_value(),
          "caller initialized notification should work");
  require(notifier_notified.has_value(),
          "notifier initialized notification should work");

  std::exception_ptr worker_error;
  std::thread worker([&] {
    try {
      auto called =
          caller->peer().call_tool("stdio.slow", Json{{"sleepMs", 700}});
      require(called.has_value(),
              "hosted cancellation no-op should not cancel active call");
      require_text_result(*called, "slow-done");
    } catch (...) {
      worker_error = std::current_exception();
    }
  });

  bool observed_active = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state =
        require_upstream_state(gateway.upstream_states(), "stdio");
    if (state.active_calls >= 1) {
      observed_active = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(observed_active,
          "hosted cancellation test should observe active upstream call");

  auto cancelled = notifier->peer().raw_notification(
      mcp::protocol::make_notification(
          std::string(mcp::protocol::CancelledNotificationMethod),
          Json{{"requestId", std::int64_t{1}},
               {"reason", "downstream cancelled"}}));
  require(cancelled.has_value(),
          "hosted cancellation notification should be accepted as no-op");

  auto progress = notifier->peer().raw_notification(
      mcp::protocol::make_notification(
          std::string(mcp::protocol::ProgressNotificationMethod),
          Json{{"progressToken", "hosted-call"}, {"progress", 0.5}}));
  require(progress.has_value(),
          "hosted progress notification should be accepted as no-op");

  const auto during = require_upstream_state(gateway.upstream_states(), "stdio");
  require(during.active_calls >= 1,
          "hosted notification no-ops should not clear active upstream call");

  worker.join();
  if (worker_error) {
    std::rethrow_exception(worker_error);
  }

  const auto state = require_upstream_state(gateway.upstream_states(), "stdio");
  require(state.active_calls == 0,
          "hosted cancellation no-op should clear after call completion");
  require_status(state, UpstreamRuntimeStatus::healthy,
                 "hosted cancellation no-op should leave upstream healthy");

  auto capabilities = gateway.server_capabilities();
  require(capabilities.tools.enabled,
          "hosted notification no-ops should keep tools advertised");
  require(!capabilities.tools.list_changed,
          "hosted notification no-ops should not add tools/listChanged");
  require(capabilities.resources.enabled,
          "hosted notification no-ops should keep resources advertised");
  require(!capabilities.resources.list_changed,
          "hosted notification no-ops should not add resources/listChanged");
  require(!capabilities.resources.subscribe,
          "hosted notification no-ops should not add resource subscriptions");
  require(capabilities.prompts.enabled,
          "hosted notification no-ops should keep prompts advertised");
  require(!capabilities.prompts.list_changed,
          "hosted notification no-ops should not add prompts/listChanged");
  require(!capabilities.tasks.has_value(),
          "hosted notification no-ops should not add tasks");

  auto caller_stopped = caller->stop();
  auto notifier_stopped = notifier->stop();
  require(caller_stopped.has_value(), "caller downstream client should stop");
  require(notifier_stopped.has_value(),
          "notifier downstream client should stop");

  auto gateway_stopped = gateway.stop();
  require(gateway_stopped.has_value(),
          "hosted notification no-op gateway should stop");
}

void test_hosted_cancellation_notifications_do_not_cancel_http_upstream_call() {
  const auto kUpstreamPort = find_available_loopback_port();
  const std::string upstream_uri =
      "http://127.0.0.1:" + std::to_string(kUpstreamPort) + "/mcp";
  std::atomic_bool slow_entered = false;

  auto upstream_server =
      mcp::ServerPeer::builder()
          .name("cxxmcp-gateway-hosted-http-cancel-upstream")
          .version("1.0.0")
          .streamable_http("127.0.0.1", kUpstreamPort, "/mcp")
          .tool<Json, ToolResult>("slow", [&](const Json& input) {
            slow_entered = true;
            std::this_thread::sleep_for(std::chrono::milliseconds{
                input.value("sleepMs", 700)});
            return ToolResult::text("slow-done");
          })
          .build();
  require(upstream_server.has_value(),
          "hosted http cancellation upstream should build");
  auto running_upstream = mcp::serve(std::move(*upstream_server));
  require(running_upstream.has_value(),
          "hosted http cancellation upstream should start");
  running_upstream->wait_until_ready();

  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "http_cancel";
  upstream.transport = mcp::gateway::UpstreamTransportKind::streamable_http;
  upstream.streamable_http.uri = upstream_uri;
  upstream.streamable_http.timeout = std::chrono::seconds{3};
  config.upstreams.push_back(std::move(upstream));

  const auto kGatewayPort = find_available_loopback_port();
  mcp::gateway::GatewayRuntime gateway(std::move(config));
  auto started = gateway.start_http(
      {.host = "127.0.0.1", .port = kGatewayPort, .path = "/mcp"});
  require(started.has_value(),
          "hosted http cancellation gateway should start");

  auto make_client = [kGatewayPort] {
    return mcp::ClientPeer::builder()
        .streamable_http("http://127.0.0.1:" +
                         std::to_string(kGatewayPort) + "/mcp")
        .build();
  };

  auto caller_client = make_client();
  auto notifier_client = make_client();
  require(caller_client.has_value(),
          "http cancellation caller client should build");
  require(notifier_client.has_value(),
          "http cancellation notifier client should build");

  auto caller = mcp::serve(std::move(*caller_client));
  auto notifier = mcp::serve(std::move(*notifier_client));
  require(caller.has_value(), "http cancellation caller should start");
  require(notifier.has_value(), "http cancellation notifier should start");

  auto caller_initialized = caller->peer().initialize(
      "cxxmcp-gateway-hosted-http-cancel-caller", "1.0.0");
  auto notifier_initialized = notifier->peer().initialize(
      "cxxmcp-gateway-hosted-http-cancel-notifier", "1.0.0");
  require(caller_initialized.has_value(),
          "http cancellation caller initialize should succeed");
  require(notifier_initialized.has_value(),
          "http cancellation notifier initialize should succeed");

  auto caller_notified = caller->peer().notify_initialized();
  auto notifier_notified = notifier->peer().notify_initialized();
  require(caller_notified.has_value(),
          "http cancellation caller initialized notification should work");
  require(notifier_notified.has_value(),
          "http cancellation notifier initialized notification should work");

  std::exception_ptr worker_error;
  std::thread worker([&] {
    try {
      auto called =
          caller->peer().call_tool("http_cancel.slow", Json{{"sleepMs", 700}});
      require(called.has_value(),
              "hosted http cancellation no-op should not cancel active call");
      require_text_result(*called, "slow-done");
    } catch (...) {
      worker_error = std::current_exception();
    }
  });

  bool observed_active = false;
  bool observed_handler = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state =
        require_upstream_state(gateway.upstream_states(), "http_cancel");
    if (state.active_calls >= 1) {
      observed_active = true;
    }
    if (slow_entered.load()) {
      observed_handler = true;
    }
    if (observed_active && observed_handler) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(observed_active,
          "hosted http cancellation test should observe active upstream call");
  require(observed_handler,
          "hosted http cancellation test should observe upstream handler");

  auto cancelled = notifier->peer().raw_notification(
      mcp::protocol::make_notification(
          std::string(mcp::protocol::CancelledNotificationMethod),
          Json{{"requestId", std::int64_t{1}},
               {"reason", "downstream cancelled"}}));
  require(cancelled.has_value(),
          "hosted http cancellation notification should be accepted as no-op");

  auto progress = notifier->peer().raw_notification(
      mcp::protocol::make_notification(
          std::string(mcp::protocol::ProgressNotificationMethod),
          Json{{"progressToken", "hosted-http-call"}, {"progress", 0.5}}));
  require(progress.has_value(),
          "hosted http progress notification should be accepted as no-op");

  const auto during =
      require_upstream_state(gateway.upstream_states(), "http_cancel");
  require(during.active_calls >= 1,
          "hosted http notification no-ops should not clear active call");

  worker.join();
  if (worker_error) {
    std::rethrow_exception(worker_error);
  }

  const auto state =
      require_upstream_state(gateway.upstream_states(), "http_cancel");
  require(state.active_calls == 0,
          "hosted http cancellation no-op should clear after call completion");
  require_status(state, UpstreamRuntimeStatus::healthy,
                 "hosted http cancellation no-op should leave upstream healthy");

  auto caller_stopped = caller->stop();
  auto notifier_stopped = notifier->stop();
  require(caller_stopped.has_value(),
          "http cancellation caller should stop");
  require(notifier_stopped.has_value(),
          "http cancellation notifier should stop");

  auto gateway_stopped = gateway.stop();
  require(gateway_stopped.has_value(),
          "hosted http cancellation gateway should stop");
  auto upstream_stopped = running_upstream->stop();
  require(upstream_stopped.has_value(),
          "hosted http cancellation upstream should stop");
}

void test_downstream_close_during_active_upstream_call_clears_state() {
  const auto kPort = find_available_loopback_port();

  mcp::gateway::GatewayRuntime gateway(make_stdio_config());
  auto started = gateway.start_http(
      {.host = "127.0.0.1", .port = kPort, .path = "/mcp"});
  require(started.has_value(),
          "downstream-close hosted gateway endpoint should start");

  auto client = mcp::ClientPeer::builder()
                    .streamable_http("http://127.0.0.1:" +
                                     std::to_string(kPort) + "/mcp")
                    .build();
  require(client.has_value(), "downstream-close client should build");

  auto running_client = mcp::serve(std::move(*client));
  require(running_client.has_value(),
          "downstream-close client service should start");

  auto initialized = running_client->peer().initialize(
      "cxxmcp-gateway-downstream-close-client", "1.0.0");
  require(initialized.has_value(),
          "downstream-close client should initialize");
  auto notified = running_client->peer().notify_initialized();
  require(notified.has_value(),
          "downstream-close initialized notification should work");

  std::exception_ptr worker_error;
  std::thread worker([&] {
    try {
      auto called = running_client->peer().call_tool(
          "stdio.slow", Json{{"sleepMs", 800}});
      if (called.has_value()) {
        require_text_result(*called, "slow-done");
      }
    } catch (...) {
      worker_error = std::current_exception();
    }
  });

  bool observed_active = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state =
        require_upstream_state(gateway.upstream_states(), "stdio");
    if (state.active_calls >= 1) {
      observed_active = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(observed_active,
          "downstream-close test should observe active upstream call");

  auto stopped_client = running_client->stop();
  require(stopped_client.has_value(),
          "downstream-close client stop should succeed");

  worker.join();
  if (worker_error) {
    std::rethrow_exception(worker_error);
  }

  const auto state = require_upstream_state(gateway.upstream_states(), "stdio");
  require(state.active_calls == 0,
          "downstream close should not leave active upstream calls");

  auto stopped_gateway = gateway.stop();
  require(stopped_gateway.has_value(),
          "downstream-close hosted gateway should stop");
}

void test_downstream_close_during_active_http_upstream_call_clears_state() {
  const auto kUpstreamPort = find_available_loopback_port();
  const std::string upstream_uri =
      "http://127.0.0.1:" + std::to_string(kUpstreamPort) + "/mcp";
  std::atomic_bool slow_entered = false;

  auto upstream_server =
      mcp::ServerPeer::builder()
          .name("cxxmcp-gateway-downstream-close-http-upstream")
          .version("1.0.0")
          .streamable_http("127.0.0.1", kUpstreamPort, "/mcp")
          .tool<Json, ToolResult>("slow", [&](const Json& input) {
            slow_entered = true;
            std::this_thread::sleep_for(std::chrono::milliseconds{
                input.value("sleepMs", 800)});
            return ToolResult::text("slow-done");
          })
          .build();
  require(upstream_server.has_value(),
          "downstream-close http upstream should build");
  auto running_upstream = mcp::serve(std::move(*upstream_server));
  require(running_upstream.has_value(),
          "downstream-close http upstream should start");
  running_upstream->wait_until_ready();

  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "http_close";
  upstream.transport = mcp::gateway::UpstreamTransportKind::streamable_http;
  upstream.streamable_http.uri = upstream_uri;
  upstream.streamable_http.timeout = std::chrono::seconds{3};
  config.upstreams.push_back(std::move(upstream));

  const auto kGatewayPort = find_available_loopback_port();
  mcp::gateway::GatewayRuntime gateway(std::move(config));
  auto started = gateway.start_http(
      {.host = "127.0.0.1", .port = kGatewayPort, .path = "/mcp"});
  require(started.has_value(),
          "downstream-close http hosted gateway should start");

  auto client = mcp::ClientPeer::builder()
                    .streamable_http("http://127.0.0.1:" +
                                     std::to_string(kGatewayPort) + "/mcp")
                    .build();
  require(client.has_value(), "downstream-close http client should build");

  auto running_client = mcp::serve(std::move(*client));
  require(running_client.has_value(),
          "downstream-close http client service should start");

  auto initialized = running_client->peer().initialize(
      "cxxmcp-gateway-downstream-close-http-client", "1.0.0");
  require(initialized.has_value(),
          "downstream-close http client should initialize");
  auto notified = running_client->peer().notify_initialized();
  require(notified.has_value(),
          "downstream-close http initialized notification should work");

  std::exception_ptr worker_error;
  std::thread worker([&] {
    try {
      auto called = running_client->peer().call_tool(
          "http_close.slow", Json{{"sleepMs", 800}});
      if (called.has_value()) {
        require_text_result(*called, "slow-done");
      }
    } catch (...) {
      worker_error = std::current_exception();
    }
  });

  bool observed_active = false;
  bool observed_handler = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto state =
        require_upstream_state(gateway.upstream_states(), "http_close");
    if (state.active_calls >= 1) {
      observed_active = true;
    }
    if (slow_entered.load()) {
      observed_handler = true;
    }
    if (observed_active && observed_handler) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  require(observed_active,
          "downstream-close http test should observe active upstream call");
  require(observed_handler,
          "downstream-close http test should observe upstream handler");

  auto stopped_client = running_client->stop();
  require(stopped_client.has_value(),
          "downstream-close http client stop should succeed");

  worker.join();
  if (worker_error) {
    std::rethrow_exception(worker_error);
  }

  const auto state =
      require_upstream_state(gateway.upstream_states(), "http_close");
  require(state.active_calls == 0,
          "downstream http close should not leave active upstream calls");

  auto stopped_gateway = gateway.stop();
  require(stopped_gateway.has_value(),
          "downstream-close http hosted gateway should stop");
  auto stopped_upstream = running_upstream->stop();
  require(stopped_upstream.has_value(),
          "downstream-close http upstream should stop");
}

void test_hosted_gateway_without_enabled_upstreams_advertises_no_tools() {
  const auto kPort = find_available_loopback_port();

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
          "gateway should not advertise resources without enabled upstreams");
  require(!capabilities->prompts.enabled,
          "gateway should not advertise prompts without enabled upstreams");
  require(!capabilities->completions.enabled,
          "gateway should not advertise completions without routing support");
  require(!capabilities->tasks.has_value(),
          "gateway should not advertise tasks without routing support");

  auto stopped_client = running_client->stop();
  require(stopped_client.has_value(), "no-tools downstream client should stop");
  auto stopped_gateway = gateway.stop();
  require(stopped_gateway.has_value(), "no-tools gateway should stop");
}

void test_raw_request_routing_surface() {
  mcp::gateway::GatewayRuntime runtime(make_stdio_config());

  const std::vector<std::string> sdk_owned_methods{
      mcp::protocol::PingMethod,
      mcp::protocol::ServerDiscoverMethod,
  };
  for (std::size_t i = 0; i < sdk_owned_methods.size(); ++i) {
    mcp::protocol::JsonRpcRequest sdk_owned;
    sdk_owned.method = sdk_owned_methods[i];
    sdk_owned.id = static_cast<std::int64_t>(1 + i);
    auto sdk_owned_response = runtime.handle_request(sdk_owned);
    require(!sdk_owned_response.has_value(),
            "SDK-owned methods should remain SDK-owned/nullopt");
  }

  mcp::protocol::JsonRpcRequest first_initialize;
  first_initialize.method = mcp::protocol::InitializeMethod;
  first_initialize.id = std::int64_t{10};
  auto first_initialize_response = runtime.handle_request(first_initialize);
  require(!first_initialize_response.has_value(),
          "first initialize should remain SDK-owned/nullopt");

  mcp::protocol::JsonRpcRequest repeated_initialize;
  repeated_initialize.method = mcp::protocol::InitializeMethod;
  repeated_initialize.id = std::int64_t{11};
  auto repeated_initialize_response =
      runtime.handle_request(repeated_initialize);
  require(!repeated_initialize_response.has_value(),
          "repeated initialize should remain SDK-owned/nullopt");

  const std::vector<std::string> unsupported_methods{
      mcp::protocol::ResourcesSubscribeMethod,
      mcp::protocol::ResourcesUnsubscribeMethod,
      mcp::protocol::ToolsGetMethod,
      mcp::protocol::LoggingSetLevelMethod,
      mcp::protocol::SamplingCreateMessageMethod,
      mcp::protocol::ElicitationCreateMethod,
      mcp::protocol::TasksListMethod,
      mcp::protocol::TasksGetMethod,
      mcp::protocol::TasksCancelMethod,
      mcp::protocol::TasksResultMethod,
      mcp::protocol::TasksCreateMethod,
      mcp::protocol::RootsListMethod,
  };
  for (std::size_t i = 0; i < unsupported_methods.size(); ++i) {
    mcp::protocol::JsonRpcRequest unsupported;
    unsupported.method = unsupported_methods[i];
    unsupported.id = static_cast<std::int64_t>(20 + i);
    auto unsupported_response = runtime.handle_request(unsupported);
    require(unsupported_response.has_value(),
            "unsupported gateway methods should respond");
    require(unsupported_response->error.has_value(),
            "unsupported gateway methods should error");
    require(unsupported_response->error->code ==
                static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
            "unsupported gateway method should map to method not found");
    require(unsupported_response->error->data.has_value(),
            "unsupported gateway method should preserve method context");
    require(*unsupported_response->error->data == unsupported_methods[i],
            "unsupported gateway method data should match request method");
  }

  const auto unsupported_state =
      require_upstream_state(runtime.upstream_states(), "stdio");
  require_status(unsupported_state, UpstreamRuntimeStatus::configured,
                 "unsupported methods should not start upstream sessions");
  require(unsupported_state.active_calls == 0,
          "unsupported methods should not create active upstream calls");

  mcp::protocol::JsonRpcRequest invalid_tool_name;
  invalid_tool_name.method = mcp::protocol::ToolsCallMethod;
  invalid_tool_name.id = std::int64_t{3};
  invalid_tool_name.params = Json{{"name", "bad"}, {"arguments", Json::object()}};
  auto invalid_response = runtime.handle_request(invalid_tool_name);
  require(invalid_response.has_value(), "invalid tool call should respond");
  require(invalid_response->error.has_value(), "invalid tool call should error");
  require(invalid_response->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "invalid exposed tool name should map to invalid params");

  mcp::protocol::JsonRpcRequest invalid_tool_arguments;
  invalid_tool_arguments.method = mcp::protocol::ToolsCallMethod;
  invalid_tool_arguments.id = std::int64_t{4};
  invalid_tool_arguments.params =
      Json{{"name", "stdio.echo"}, {"arguments", Json::array()}};
  auto invalid_arguments_response =
      runtime.handle_request(invalid_tool_arguments);
  require(invalid_arguments_response.has_value(),
          "invalid tool arguments should respond");
  require(invalid_arguments_response->error.has_value(),
          "invalid tool arguments should error");
  require(invalid_arguments_response->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidRequest),
          "invalid tool arguments should map to invalid request");

  mcp::protocol::JsonRpcRequest unknown_upstream_tool_call;
  unknown_upstream_tool_call.method = mcp::protocol::ToolsCallMethod;
  unknown_upstream_tool_call.id = std::int64_t{5};
  unknown_upstream_tool_call.params =
      Json{{"name", "missing.echo"}, {"arguments", Json::object()}};
  auto unknown_upstream_response =
      runtime.handle_request(unknown_upstream_tool_call);
  require(unknown_upstream_response.has_value(),
          "unknown upstream tool call should respond");
  require(unknown_upstream_response->error.has_value(),
          "unknown upstream tool call should error");
  require(unknown_upstream_response->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::ToolNotFound),
          "unknown upstream tool call should map to tool not found");

  auto unsupported_notification = runtime.handle_notification(
      mcp::protocol::make_notification(
          std::string(mcp::protocol::ResourcesListChangedNotificationMethod),
          Json::object()));
  require(unsupported_notification.has_value(),
          "unsupported gateway notifications should be ignored");

  mcp::protocol::JsonRpcRequest resource_list;
  resource_list.method = mcp::protocol::ResourcesListMethod;
  resource_list.id = std::int64_t{6};
  resource_list.params = Json::object();
  auto resource_list_response = runtime.handle_request(resource_list);
  require(resource_list_response.has_value(),
          "resources/list raw request should respond");
  require(resource_list_response->result.has_value(),
          "resources/list raw request should succeed");
  const auto parsed_resources =
      mcp::protocol::resources_list_result_from_json(
          *resource_list_response->result);
  require(parsed_resources.has_value(),
          "resources/list raw response should parse");
  const auto raw_resource_uri =
      mcp::gateway::GatewayRouter::expose_resource_uri(
          "stdio", "file:///fixture/readme.txt");
  require(has_resource(parsed_resources->resources, raw_resource_uri),
          "resources/list raw response should include routed resource");

  mcp::protocol::JsonRpcRequest resource_read;
  resource_read.method = mcp::protocol::ResourcesReadMethod;
  resource_read.id = std::int64_t{7};
  resource_read.params = Json{{"uri", raw_resource_uri}};
  auto resource_read_response = runtime.handle_request(resource_read);
  require(resource_read_response.has_value(),
          "resources/read raw request should respond");
  require(resource_read_response->result.has_value(),
          "resources/read raw request should succeed");
  const auto parsed_read = mcp::protocol::resources_read_result_from_json(
      *resource_read_response->result);
  require(parsed_read.has_value(),
          "resources/read raw response should parse");
  require_text_resource(*parsed_read, raw_resource_uri,
                        "hello from stdio resource");

  mcp::protocol::JsonRpcRequest invalid_resource_params;
  invalid_resource_params.method = mcp::protocol::ResourcesReadMethod;
  invalid_resource_params.id = std::int64_t{8};
  invalid_resource_params.params = Json{{"uri", Json::array()}};
  auto invalid_resource_response =
      runtime.handle_request(invalid_resource_params);
  require(invalid_resource_response.has_value(),
          "invalid resources/read should respond");
  require(invalid_resource_response->error.has_value(),
          "invalid resources/read should error");
  require(invalid_resource_response->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidRequest),
          "invalid resources/read params should map to invalid request");

  mcp::protocol::JsonRpcRequest invalid_resource_uri;
  invalid_resource_uri.method = mcp::protocol::ResourcesReadMethod;
  invalid_resource_uri.id = std::int64_t{9};
  invalid_resource_uri.params = Json{{"uri", "file:///not-gateway.txt"}};
  auto invalid_resource_uri_response =
      runtime.handle_request(invalid_resource_uri);
  require(invalid_resource_uri_response.has_value(),
          "invalid gateway resource URI should respond");
  require(invalid_resource_uri_response->error.has_value(),
          "invalid gateway resource URI should error");
  require(invalid_resource_uri_response->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "invalid gateway resource URI should map to invalid params");

  mcp::protocol::JsonRpcRequest resource_templates_list;
  resource_templates_list.method =
      mcp::protocol::ResourcesTemplatesListMethod;
  resource_templates_list.id = std::int64_t{10};
  resource_templates_list.params = Json::object();
  auto resource_templates_response =
      runtime.handle_request(resource_templates_list);
  require(resource_templates_response.has_value(),
          "resources/templates/list raw request should respond");
  require(resource_templates_response->result.has_value(),
          "resources/templates/list raw request should succeed");
  const auto parsed_resource_templates =
      mcp::protocol::resource_templates_list_result_from_json(
          *resource_templates_response->result);
  require(parsed_resource_templates.has_value(),
          "resources/templates/list raw response should parse");
  require(has_resource_template(
              parsed_resource_templates->resource_templates,
              mcp::gateway::GatewayRouter::expose_resource_template_uri(
                  "stdio", "file:///fixture/{path}")),
          "resources/templates/list raw response should include routed "
          "resource template");

  mcp::protocol::JsonRpcRequest prompt_list;
  prompt_list.method = mcp::protocol::PromptsListMethod;
  prompt_list.id = std::int64_t{11};
  prompt_list.params = Json::object();
  auto prompt_list_response = runtime.handle_request(prompt_list);
  require(prompt_list_response.has_value(),
          "prompts/list raw request should respond");
  require(prompt_list_response->result.has_value(),
          "prompts/list raw request should succeed");
  const auto parsed_prompts = mcp::protocol::prompts_list_result_from_json(
      *prompt_list_response->result);
  require(parsed_prompts.has_value(),
          "prompts/list raw response should parse");
  require(has_prompt(parsed_prompts->prompts, "stdio.summarize"),
          "prompts/list raw response should include routed prompt");

  mcp::protocol::JsonRpcRequest prompt_get;
  prompt_get.method = mcp::protocol::PromptsGetMethod;
  prompt_get.id = std::int64_t{12};
  prompt_get.params =
      Json{{"name", "stdio.summarize"},
           {"arguments", Json{{"text", "from-raw"}}}};
  auto prompt_get_response = runtime.handle_request(prompt_get);
  require(prompt_get_response.has_value(),
          "prompts/get raw request should respond");
  require(prompt_get_response->result.has_value(),
          "prompts/get raw request should succeed");
  const auto parsed_prompt = mcp::protocol::prompts_get_result_from_json(
      *prompt_get_response->result);
  require(parsed_prompt.has_value(),
          "prompts/get raw response should parse");
  require_text_prompt(*parsed_prompt, "Summarize from-raw");

  mcp::protocol::CompleteParams raw_completion;
  raw_completion.ref =
      mcp::protocol::prompt_completion_reference("stdio.summarize");
  raw_completion.argument.name = "text";
  raw_completion.argument.value = "raw";
  mcp::protocol::JsonRpcRequest completion_request;
  completion_request.method = mcp::protocol::CompletionCompleteMethod;
  completion_request.id = std::int64_t{15};
  completion_request.params =
      mcp::protocol::complete_params_to_json(raw_completion);
  auto completion_response = runtime.handle_request(completion_request);
  require(completion_response.has_value(),
          "completion/complete raw request should respond");
  require(completion_response->result.has_value(),
          "completion/complete raw request should succeed");
  const auto parsed_completion =
      mcp::protocol::complete_result_from_json(*completion_response->result);
  require(parsed_completion.has_value(),
          "completion/complete raw response should parse");
  require(parsed_completion->completion.values.size() == 2,
          "completion/complete raw response should include candidates");
  require(parsed_completion->completion.values.front() == "raw-summary",
          "completion/complete raw response should route prompt ref");

  mcp::protocol::CompleteParams raw_resource_completion;
  raw_resource_completion.ref =
      mcp::protocol::resource_completion_reference(
          mcp::gateway::GatewayRouter::expose_resource_template_uri(
              "stdio", "file:///fixture/{path}"));
  raw_resource_completion.argument.name = "path";
  raw_resource_completion.argument.value = "raw/";
  mcp::protocol::JsonRpcRequest resource_completion_request;
  resource_completion_request.method =
      mcp::protocol::CompletionCompleteMethod;
  resource_completion_request.id = std::int64_t{16};
  resource_completion_request.params =
      mcp::protocol::complete_params_to_json(raw_resource_completion);
  auto resource_completion_response =
      runtime.handle_request(resource_completion_request);
  require(resource_completion_response.has_value(),
          "resource completion/complete raw request should respond");
  require(resource_completion_response->result.has_value(),
          "resource completion/complete raw request should succeed");
  const auto parsed_resource_completion =
      mcp::protocol::complete_result_from_json(
          *resource_completion_response->result);
  require(parsed_resource_completion.has_value(),
          "resource completion/complete raw response should parse");
  require(parsed_resource_completion->completion.values.size() == 2,
          "resource completion/complete raw response should include "
          "candidates");
  require(parsed_resource_completion->completion.values.front() ==
              "raw/readme.txt",
          "resource completion/complete raw response should route resource "
          "template ref");

  mcp::protocol::JsonRpcRequest invalid_completion_params;
  invalid_completion_params.method = mcp::protocol::CompletionCompleteMethod;
  invalid_completion_params.id = std::int64_t{17};
  invalid_completion_params.params =
      Json{{"ref", Json{{"type", "ref/prompt"}, {"name", Json::array()}}},
           {"argument", Json{{"name", "text"}, {"value", "raw"}}}};
  auto invalid_completion_response =
      runtime.handle_request(invalid_completion_params);
  require(invalid_completion_response.has_value(),
          "invalid completion/complete should respond");
  require(invalid_completion_response->error.has_value(),
          "invalid completion/complete should error");
  require(invalid_completion_response->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidRequest),
          "invalid completion/complete params should map to invalid request");

  mcp::protocol::JsonRpcRequest invalid_prompt_params;
  invalid_prompt_params.method = mcp::protocol::PromptsGetMethod;
  invalid_prompt_params.id = std::int64_t{18};
  invalid_prompt_params.params = Json{{"name", Json::array()}};
  auto invalid_prompt_response =
      runtime.handle_request(invalid_prompt_params);
  require(invalid_prompt_response.has_value(),
          "invalid prompts/get should respond");
  require(invalid_prompt_response->error.has_value(),
          "invalid prompts/get should error");
  require(invalid_prompt_response->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidRequest),
          "invalid prompts/get params should map to invalid request");

  mcp::protocol::JsonRpcRequest invalid_prompt_name;
  invalid_prompt_name.method = mcp::protocol::PromptsGetMethod;
  invalid_prompt_name.id = std::int64_t{19};
  invalid_prompt_name.params =
      Json{{"name", "bad"}, {"arguments", Json::object()}};
  auto invalid_prompt_name_response =
      runtime.handle_request(invalid_prompt_name);
  require(invalid_prompt_name_response.has_value(),
          "invalid gateway prompt name should respond");
  require(invalid_prompt_name_response->error.has_value(),
          "invalid gateway prompt name should error");
  require(invalid_prompt_name_response->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "invalid gateway prompt name should map to invalid params");
}

void test_raw_request_lifecycle_after_stop() {
  mcp::gateway::GatewayRuntime runtime(make_stdio_config());
  auto stopped = runtime.stop();
  require(stopped.has_value(), "raw request lifecycle runtime should stop");

  const std::vector<std::string> sdk_owned_methods{
      mcp::protocol::InitializeMethod,
      mcp::protocol::PingMethod,
      mcp::protocol::ServerDiscoverMethod,
  };
  for (std::size_t i = 0; i < sdk_owned_methods.size(); ++i) {
    mcp::protocol::JsonRpcRequest sdk_owned;
    sdk_owned.method = sdk_owned_methods[i];
    sdk_owned.id = static_cast<std::int64_t>(100 + i);
    auto response = runtime.handle_request(sdk_owned);
    require(!response.has_value(),
            "stopped runtime should keep SDK-owned methods delegated");
  }

  mcp::protocol::JsonRpcRequest tools_list;
  tools_list.method = mcp::protocol::ToolsListMethod;
  tools_list.id = std::int64_t{110};
  auto tools_list_response = runtime.handle_request(tools_list);
  require(tools_list_response.has_value(),
          "stopped runtime raw tools/list should respond");
  require_raw_runtime_stopped_error(*tools_list_response, "tools/list");

  mcp::protocol::JsonRpcRequest tools_call;
  tools_call.method = mcp::protocol::ToolsCallMethod;
  tools_call.id = std::int64_t{111};
  tools_call.params =
      Json{{"name", "stdio.echo"}, {"arguments", Json{{"value", "stopped"}}}};
  auto tools_call_response = runtime.handle_request(tools_call);
  require(tools_call_response.has_value(),
          "stopped runtime raw tools/call should respond");
  require_raw_runtime_stopped_error(*tools_call_response, "tools/call");

  mcp::protocol::JsonRpcRequest resources_list;
  resources_list.method = mcp::protocol::ResourcesListMethod;
  resources_list.id = std::int64_t{112};
  auto resources_list_response = runtime.handle_request(resources_list);
  require(resources_list_response.has_value(),
          "stopped runtime raw resources/list should respond");
  require_raw_runtime_stopped_error(*resources_list_response,
                                    "resources/list");

  mcp::protocol::JsonRpcRequest resources_read;
  resources_read.method = mcp::protocol::ResourcesReadMethod;
  resources_read.id = std::int64_t{113};
  resources_read.params = Json{{"uri",
                                mcp::gateway::GatewayRouter::expose_resource_uri(
                                    "stdio",
                                    "file:///fixture/readme.txt")}};
  auto resources_read_response = runtime.handle_request(resources_read);
  require(resources_read_response.has_value(),
          "stopped runtime raw resources/read should respond");
  require_raw_runtime_stopped_error(*resources_read_response,
                                    "resources/read");

  mcp::protocol::JsonRpcRequest resource_templates_list;
  resource_templates_list.method =
      mcp::protocol::ResourcesTemplatesListMethod;
  resource_templates_list.id = std::int64_t{114};
  auto resource_templates_response =
      runtime.handle_request(resource_templates_list);
  require(resource_templates_response.has_value(),
          "stopped runtime raw resources/templates/list should respond");
  require_raw_runtime_stopped_error(*resource_templates_response,
                                    "resources/templates/list");

  mcp::protocol::JsonRpcRequest prompts_list;
  prompts_list.method = mcp::protocol::PromptsListMethod;
  prompts_list.id = std::int64_t{115};
  auto prompts_list_response = runtime.handle_request(prompts_list);
  require(prompts_list_response.has_value(),
          "stopped runtime raw prompts/list should respond");
  require_raw_runtime_stopped_error(*prompts_list_response, "prompts/list");

  mcp::protocol::JsonRpcRequest prompts_get;
  prompts_get.method = mcp::protocol::PromptsGetMethod;
  prompts_get.id = std::int64_t{116};
  prompts_get.params =
      Json{{"name", "stdio.summarize"},
           {"arguments", Json{{"text", "after-stop"}}}};
  auto prompts_get_response = runtime.handle_request(prompts_get);
  require(prompts_get_response.has_value(),
          "stopped runtime raw prompts/get should respond");
  require_raw_runtime_stopped_error(*prompts_get_response, "prompts/get");

  mcp::protocol::CompleteParams completion;
  completion.ref = mcp::protocol::prompt_completion_reference(
      "stdio.summarize");
  completion.argument.name = "text";
  completion.argument.value = "after-stop";
  mcp::protocol::JsonRpcRequest complete;
  complete.method = mcp::protocol::CompletionCompleteMethod;
  complete.id = std::int64_t{117};
  complete.params = mcp::protocol::complete_params_to_json(completion);
  auto complete_response = runtime.handle_request(complete);
  require(complete_response.has_value(),
          "stopped runtime raw completion/complete should respond");
  require_raw_runtime_stopped_error(*complete_response,
                                    "completion/complete");
}

}  // namespace

int main() {
  try {
    auto run = [](std::string_view name, auto&& fn) {
      std::cerr << "[ RUN      ] " << name << "\n";
      fn();
      std::cerr << "[       OK ] " << name << "\n";
    };
    run("disabled upstream", test_disabled_upstream);
    run("invalid config advertises no tools",
        test_invalid_config_advertises_no_tools);
    run("stdio process start failure", test_stdio_process_start_failure);
    run("tools list fail fast after partial success",
        test_tools_list_fail_fast_after_partial_success);
    run("resources list fail fast after partial success",
        test_resources_list_fail_fast_after_partial_success);
    run("resource templates list fail fast after partial success",
        test_resource_templates_list_fail_fast_after_partial_success);
    run("prompts list fail fast after partial success",
        test_prompts_list_fail_fast_after_partial_success);
    run("stdio process exit before initialize",
        test_stdio_process_exit_before_initialize);
    run("stdio malformed response before initialize",
        test_stdio_malformed_response_before_initialize);
    run("first call upstream mcp error records capabilities",
        test_first_call_upstream_mcp_error_records_capabilities);
    run("stdio upstream", test_stdio_upstream);
    run("upstream client capabilities are minimal",
        test_upstream_client_capabilities_are_minimal);
    run("runtime observer reports status without logger dependency",
        test_runtime_observer_reports_status_without_logger_dependency);
    run("capability advertisement uses initialized upstream cache",
        test_capability_advertisement_uses_initialized_upstream_cache);
    run("capability advertisement unions multiple upstream caches",
        test_capability_advertisement_unions_multiple_upstream_caches);
    run("hosted capability advertisement uses refresh cache",
        test_hosted_capability_advertisement_uses_refresh_cache);
    run("hosted capability advertisement snapshots at start",
        test_hosted_capability_advertisement_snapshots_at_start);
    run("completion routes to stdio upstream",
        test_completion_routes_to_stdio_upstream);
    run("hosted completion routes after capability refresh",
        test_hosted_completion_routes_after_capability_refresh);
    run("repeated stdio calls to one upstream",
        test_repeated_stdio_calls_to_one_upstream);
    run("persistent stdio session reuses upstream process",
        test_persistent_stdio_session_reuses_upstream_process);
    run("persistent capability refresh prewarms stdio session",
        test_persistent_capability_refresh_prewarms_stdio_session);
    run("persistent stdio calls to one upstream are serialized",
        test_persistent_stdio_calls_to_one_upstream_are_serialized);
    run("persistent stdio session pool allows same upstream concurrency",
        test_persistent_stdio_session_pool_allows_same_upstream_concurrency);
    run("persistent stop rejects queued session pool call",
        test_persistent_stop_rejects_queued_session_pool_call);
    run("persistent pool acquire timeout rejects queued call",
        test_persistent_pool_acquire_timeout_rejects_queued_call);
    run("persistent runtime stop waits for active stdio call",
        test_persistent_runtime_stop_waits_for_active_stdio_call);
    run("persistent pool stop waits for timed out stdio call",
        test_persistent_pool_stop_waits_for_timed_out_stdio_call);
    run("persistent stdio failure invalidates session for reconnect",
        test_persistent_stdio_failure_invalidates_session_for_reconnect);
    run("persistent stdio pool failure isolates failed slot",
        test_persistent_stdio_pool_failure_isolates_failed_slot);
    run("http upstream", test_http_upstream);
    run("http unavailable", test_http_unavailable);
    run("http malformed response before initialize",
        test_http_malformed_response_before_initialize);
    run("start http invalid config fails before binding port",
        test_start_http_invalid_config_fails_before_binding_port);
    run("hosted gateway rejects invalid endpoint options",
        test_hosted_gateway_rejects_invalid_endpoint_options);
    run("hosted gateway http endpoint stops while idle",
        test_hosted_gateway_http_endpoint_stops_while_idle);
    run("runtime stop observer can reenter lifecycle api",
        test_runtime_stop_observer_can_reenter_lifecycle_api);
    run("runtime wait and stop can overlap",
        test_runtime_wait_and_stop_can_overlap);
    run("hosted gateway rejects invalid json rpc request",
        test_hosted_gateway_rejects_invalid_json_rpc_request);
    run("runtime move assignment stops existing endpoint",
        test_runtime_move_assignment_stops_existing_endpoint);
    run("http timeout", test_http_timeout);
    run("persistent http calls to one upstream are serialized",
        test_persistent_http_calls_to_one_upstream_are_serialized);
    run("persistent http session pool handles queued calls",
        test_persistent_http_session_pool_handles_queued_calls);
    run("persistent http pool acquire timeout rejects queued call",
        test_persistent_http_pool_acquire_timeout_rejects_queued_call);
    run("persistent http failure invalidates session for reconnect",
        test_persistent_http_failure_invalidates_session_for_reconnect);
    run("persistent http pool timeout recovers",
        test_persistent_http_pool_timeout_recovers);
    run("stdio timeout", test_stdio_timeout);
    run("concurrent http calls update active state",
        test_concurrent_http_calls_update_active_state);
    run("concurrent stdio calls to one upstream",
        test_concurrent_stdio_calls_to_one_upstream);
    run("multi upstream tools list starts upstreams concurrently",
        test_multi_upstream_tools_list_starts_upstreams_concurrently);
    run("tools list uses cached catalog until cleared",
        test_tools_list_uses_cached_catalog_until_cleared);
    run("clear cached catalogs keeps persistent session",
        test_clear_cached_catalogs_keeps_persistent_session);
    run("cancellation and progress notifications are local noops",
        test_cancellation_and_progress_notifications_are_local_noops);
    run("runtime stop waits for active stdio call",
        test_runtime_stop_waits_for_active_stdio_call);
    run("runtime stop timeout bounds active stdio call wait",
        test_runtime_stop_timeout_bounds_active_stdio_call_wait);
    run("runtime stop waits for active http call",
        test_runtime_stop_waits_for_active_http_call);
    run("runtime stop timeout bounds active http call wait",
        test_runtime_stop_timeout_bounds_active_http_call_wait);
    run("concurrent calls to multiple upstreams",
        test_concurrent_calls_to_multiple_upstreams);
    run("hosted gateway http endpoint", test_hosted_gateway_http_endpoint);
    run("hosted gateway rejects request before initialized notification",
        test_hosted_gateway_rejects_request_before_initialized_notification);
    run("hosted gateway rejects request before initialize",
        test_hosted_gateway_rejects_request_before_initialize);
    run("hosted gateway multiple downstream clients",
        test_hosted_gateway_multiple_downstream_clients);
    run("hosted cancellation notifications do not cancel upstream call",
        test_hosted_cancellation_notifications_do_not_cancel_upstream_call);
    run("hosted cancellation notifications do not cancel http upstream call",
        test_hosted_cancellation_notifications_do_not_cancel_http_upstream_call);
    run("downstream close during active upstream call clears state",
        test_downstream_close_during_active_upstream_call_clears_state);
    run("downstream close during active http upstream call clears state",
        test_downstream_close_during_active_http_upstream_call_clears_state);
    run("hosted gateway without enabled upstreams advertises no tools",
        test_hosted_gateway_without_enabled_upstreams_advertises_no_tools);
    run("raw request routing surface", test_raw_request_routing_surface);
    run("raw request lifecycle after stop",
        test_raw_request_lifecycle_after_stop);
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "gateway runtime integration failed: " << ex.what() << "\n";
    return 1;
  }
}
