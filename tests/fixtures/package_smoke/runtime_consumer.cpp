// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/gateway/runtime.hpp>
#include <cxxmcp/gateway/config.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>

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

namespace {

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

}  // namespace

static_assert(
    std::is_move_constructible_v<mcp::gateway::GatewayRuntime>,
    "GatewayRuntime should be move constructible for package consumers");
static_assert(std::is_move_assignable_v<mcp::gateway::GatewayRuntime>,
              "GatewayRuntime should be move assignable for package consumers");
static_assert(
    !std::is_copy_constructible_v<mcp::gateway::GatewayRuntime>,
    "GatewayRuntime should not be copy constructible for package consumers");
static_assert(!std::is_copy_assignable_v<mcp::gateway::GatewayRuntime>,
              "GatewayRuntime should not be copy assignable for package "
              "consumers");

int main() {
  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "disabled";
  upstream.enabled = false;
  config.upstreams.push_back(std::move(upstream));

  auto valid = mcp::gateway::validate_gateway_config(config);
  if (!valid) {
    return 1;
  }

  std::size_t observed_events = 0;
  mcp::gateway::GatewayRuntimeConfig runtime_config;
  runtime_config.upstream_session_mode =
      mcp::gateway::UpstreamSessionMode::persistent;
  runtime_config.persistent_session_pool_size = 2;
  runtime_config.persistent_session_acquire_timeout =
      std::chrono::milliseconds{100};
  runtime_config.active_call_drain_timeout = std::chrono::milliseconds{5000};
  auto options = mcp::gateway::make_gateway_runtime_options(
      runtime_config,
      [&](const mcp::gateway::GatewayRuntimeEvent&) { ++observed_events; });
  if (!options) {
    return 1;
  }

  mcp::gateway::GatewayRuntime runtime(std::move(config),
                                       std::move(*options));
  mcp::gateway::GatewayRuntime moved_runtime(std::move(runtime));

  mcp::gateway::GatewayConfig replacement_config;
  mcp::gateway::UpstreamServer replacement_upstream;
  replacement_upstream.id = "replacement";
  replacement_upstream.enabled = false;
  replacement_config.upstreams.push_back(std::move(replacement_upstream));
  mcp::gateway::GatewayRuntime assigned_runtime(std::move(replacement_config));
  assigned_runtime = std::move(moved_runtime);

  auto capabilities = assigned_runtime.server_capabilities();
  auto cleared = assigned_runtime.clear_cached_catalogs();
  if (!cleared) {
    return 1;
  }
  auto refreshed = assigned_runtime.refresh_upstream_capabilities();
  if (!refreshed) {
    return 1;
  }

  auto tools = assigned_runtime.list_tools();
  auto resources = assigned_runtime.list_resources();
  auto resource_templates = assigned_runtime.list_resource_templates();
  auto prompts = assigned_runtime.list_prompts();
  if (!tools || !tools->empty() || !resources || !resources->empty() ||
      !resource_templates || !resource_templates->empty() || !prompts ||
      !prompts->empty()) {
    return 1;
  }

  auto disabled_tool = assigned_runtime.call_tool("disabled.echo");
  auto disabled_prompt = assigned_runtime.get_prompt("disabled.summarize");
  if (disabled_tool || disabled_prompt) {
    return 1;
  }

  auto invalid_start = assigned_runtime.start_http(
      {.host = "", .port = 3000, .path = "/mcp"});
  if (invalid_start) {
    return 1;
  }
  auto wait_before_start = assigned_runtime.wait();
  if (wait_before_start) {
    return 1;
  }

  const auto port = find_available_loopback_port();
  auto valid_start = assigned_runtime.start_http(
      {.host = "127.0.0.1", .port = port, .path = "/mcp"});
  if (!valid_start) {
    return 1;
  }

  auto notification = assigned_runtime.handle_notification(
      mcp::protocol::make_notification(
          mcp::protocol::CancelledNotificationMethod,
          mcp::protocol::Json{{"requestId", std::int64_t{42}},
                              {"reason", "package-smoke"}}));
  if (!notification) {
    return 1;
  }

  mcp::protocol::JsonRpcRequest tools_list;
  tools_list.method = mcp::protocol::ToolsListMethod;
  tools_list.id = std::int64_t{43};
  auto tools_list_response = assigned_runtime.handle_request(tools_list);
  if (!tools_list_response.has_value() ||
      !tools_list_response->has_result() ||
      !tools_list_response->result->contains("tools") ||
      !tools_list_response->result->at("tools").is_array() ||
      !tools_list_response->result->at("tools").empty()) {
    return 1;
  }

  auto stopped = assigned_runtime.stop();
  if (!stopped) {
    return 1;
  }

  const auto states = assigned_runtime.upstream_states();
  return states.size() == 1 && !capabilities.tools.enabled &&
                 states.front().persistent_session_pool_size == 2 &&
                 states.front().initialized_persistent_sessions == 0 &&
                 states.front().busy_persistent_sessions == 0 &&
                 observed_events > 0
             ? 0
             : 1;
}
