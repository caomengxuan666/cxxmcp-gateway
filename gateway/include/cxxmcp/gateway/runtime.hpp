// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/gateway/config.hpp"
#include "cxxmcp/gateway/router.hpp"
#include "cxxmcp/protocol/capabilities.hpp"
#include "cxxmcp/protocol/prompt.hpp"
#include "cxxmcp/protocol/resource.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/protocol/tool.hpp"

namespace mcp::gateway {

enum class UpstreamRuntimeStatus {
  configured,
  connecting,
  initialized,
  healthy,
  degraded,
  stopping,
  stopped,
};

struct UpstreamRuntimeState {
  std::string upstream_id;
  UpstreamRuntimeStatus status = UpstreamRuntimeStatus::configured;
  std::size_t active_calls = 0;
  std::optional<protocol::ServerCapabilities> capabilities;
  std::optional<core::Error> last_error;
};

class GatewayRuntime final {
 public:
  explicit GatewayRuntime(GatewayConfig config);
  ~GatewayRuntime();

  GatewayRuntime(const GatewayRuntime&) = delete;
  GatewayRuntime& operator=(const GatewayRuntime&) = delete;
  GatewayRuntime(GatewayRuntime&&) noexcept;
  GatewayRuntime& operator=(GatewayRuntime&&) noexcept;

  const GatewayRouter& router() const noexcept;

  core::Result<std::vector<protocol::ToolDefinition>> list_tools();
  core::Result<protocol::ToolResult> call_tool(
      std::string_view exposed_name,
      protocol::Json arguments = protocol::Json::object());
  core::Result<std::vector<protocol::Resource>> list_resources();
  core::Result<std::vector<protocol::ResourceTemplate>>
  list_resource_templates();
  core::Result<protocol::ResourcesReadResult> read_resource(
      std::string_view exposed_uri);
  core::Result<std::vector<protocol::Prompt>> list_prompts();
  core::Result<protocol::PromptsGetResult> get_prompt(
      std::string_view exposed_name,
      protocol::Json arguments = protocol::Json::object());
  core::Result<core::Unit> handle_notification(
      const protocol::JsonRpcNotification& notification);
  std::optional<protocol::JsonRpcResponse> handle_request(
      const protocol::JsonRpcRequest& request);
  std::vector<UpstreamRuntimeState> upstream_states() const;
  core::Result<core::Unit> refresh_upstream_capabilities();
  protocol::ServerCapabilities server_capabilities() const;

  core::Result<core::Unit> start_http(HttpEndpoint endpoint);
  core::Result<core::Unit> wait();
  core::Result<core::Unit> stop() noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mcp::gateway
