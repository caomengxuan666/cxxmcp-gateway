// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/gateway/config.hpp"

namespace mcp::gateway {

struct ResolvedToolName {
  std::string upstream_id;
  std::string upstream_tool_name;
};

struct ResolvedResourceUri {
  std::string upstream_id;
  std::string upstream_uri;
};

struct ToolRoute {
  const UpstreamServer* upstream = nullptr;
  std::string upstream_tool_name;
};

class GatewayRouter final {
 public:
  explicit GatewayRouter(GatewayConfig config);

  const GatewayConfig& config() const noexcept;

  core::Result<core::Unit> validate_config() const;
  core::Result<ToolRoute> resolve_tool_route(
      std::string_view exposed_name) const;

  static std::string expose_tool_name(std::string_view upstream_id,
                                      std::string_view upstream_tool_name);
  static core::Result<ResolvedToolName> resolve_tool_name(
      std::string_view exposed_name);
  static std::string expose_resource_uri(std::string_view upstream_id,
                                         std::string_view upstream_uri);
  static core::Result<ResolvedResourceUri> resolve_resource_uri(
      std::string_view exposed_uri);

 private:
  const UpstreamServer* find_upstream(std::string_view upstream_id) const;

  GatewayConfig config_;
};

}  // namespace mcp::gateway
