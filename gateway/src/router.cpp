// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/gateway/router.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <unordered_set>
#include <utility>

#include "cxxmcp/gateway/error.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::gateway {
namespace {

constexpr std::size_t kMaxUpstreamIdLength = 128;

bool has_forbidden_upstream_id_char(std::string_view value) {
  return std::any_of(value.begin(), value.end(), [](char ch) {
    const auto byte = static_cast<unsigned char>(ch);
    return byte < 0x21 || byte > 0x7e || ch == '.' || ch == '/' ||
           ch == '\\' || std::isspace(byte) != 0;
  });
}

}  // namespace

core::Result<core::Unit> validate_upstream_id(
    std::string_view upstream_id) {
  if (upstream_id.empty()) {
    return mcp::core::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidParams, "upstream id must not be empty"));
  }
  if (upstream_id.size() > kMaxUpstreamIdLength) {
    return mcp::core::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidParams,
        "upstream id must be at most 128 characters",
        std::string(upstream_id)));
  }
  if (has_forbidden_upstream_id_char(upstream_id)) {
    return mcp::core::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidParams,
        "upstream id must be printable ASCII without '.', whitespace, or path separators",
        std::string(upstream_id)));
  }
  return core::Unit{};
}

core::Result<core::Unit> validate_gateway_config(
    const GatewayConfig& config) {
  if (config.name.empty()) {
    return mcp::core::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidParams, "gateway name must not be empty"));
  }
  if (config.version.empty()) {
    return mcp::core::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidParams,
        "gateway version must not be empty"));
  }

  std::unordered_set<std::string> seen_upstreams;
  for (const auto& upstream : config.upstreams) {
    auto valid_id = validate_upstream_id(upstream.id);
    if (!valid_id) {
      return valid_id;
    }
    if (!seen_upstreams.insert(upstream.id).second) {
      return mcp::core::unexpected(make_gateway_error(
          protocol::ErrorCode::InvalidParams, "duplicate upstream id",
          upstream.id));
    }

    if (!upstream.enabled) {
      continue;
    }

    switch (upstream.transport) {
      case UpstreamTransportKind::process_stdio:
        if (upstream.process_stdio.command.empty()) {
          return mcp::core::unexpected(make_gateway_error(
              protocol::ErrorCode::InvalidParams,
              "enabled process stdio upstream command must not be empty",
              upstream.id));
        }
        break;
      case UpstreamTransportKind::streamable_http:
        if (upstream.streamable_http.uri.empty()) {
          return mcp::core::unexpected(make_gateway_error(
              protocol::ErrorCode::InvalidParams,
              "enabled streamable http upstream uri must not be empty",
              upstream.id));
        }
        if (upstream.streamable_http.timeout.count() <= 0) {
          return mcp::core::unexpected(make_gateway_error(
              protocol::ErrorCode::InvalidParams,
              "enabled streamable http upstream timeout must be positive",
              upstream.id));
        }
        break;
    }
  }

  return core::Unit{};
}

GatewayRouter::GatewayRouter(GatewayConfig config)
    : config_(std::move(config)) {}

const GatewayConfig& GatewayRouter::config() const noexcept { return config_; }

core::Result<core::Unit> GatewayRouter::validate_config() const {
  return validate_gateway_config(config_);
}

std::string GatewayRouter::expose_tool_name(
    std::string_view upstream_id, std::string_view upstream_tool_name) {
  return std::string(upstream_id) + "." + std::string(upstream_tool_name);
}

core::Result<ResolvedToolName> GatewayRouter::resolve_tool_name(
    std::string_view exposed_name) {
  const auto dot = exposed_name.find('.');
  if (dot == std::string_view::npos || dot == 0 ||
      dot + 1 >= exposed_name.size()) {
    return mcp::core::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidParams,
        "gateway tool name must use '<upstream>.<tool>'",
        std::string(exposed_name)));
  }

  const auto upstream_id = exposed_name.substr(0, dot);
  auto valid_id = validate_upstream_id(upstream_id);
  if (!valid_id) {
    return mcp::core::unexpected(valid_id.error());
  }

  return ResolvedToolName{
      .upstream_id = std::string(upstream_id),
      .upstream_tool_name = std::string(exposed_name.substr(dot + 1)),
  };
}

core::Result<ToolRoute> GatewayRouter::resolve_tool_route(
    std::string_view exposed_name) const {
  auto resolved = resolve_tool_name(exposed_name);
  if (!resolved) {
    return mcp::core::unexpected(resolved.error());
  }

  const auto* upstream = find_upstream(resolved->upstream_id);
  if (upstream == nullptr) {
    return mcp::core::unexpected(make_gateway_error(
        protocol::ErrorCode::ToolNotFound, "gateway upstream not found",
        resolved->upstream_id));
  }
  if (!upstream->enabled) {
    return mcp::core::unexpected(make_gateway_error(
        protocol::ErrorCode::ToolNotFound, "gateway upstream is disabled",
        resolved->upstream_id));
  }

  return ToolRoute{
      .upstream = upstream,
      .upstream_tool_name = std::move(resolved->upstream_tool_name),
  };
}

const UpstreamServer* GatewayRouter::find_upstream(
    std::string_view upstream_id) const {
  const auto it = std::find_if(
      config_.upstreams.begin(), config_.upstreams.end(),
      [&](const auto& upstream) { return upstream.id == upstream_id; });
  return it == config_.upstreams.end() ? nullptr : &*it;
}

}  // namespace mcp::gateway
