// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/gateway/router.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include "cxxmcp/gateway/error.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::gateway {
namespace {

constexpr std::size_t kMaxUpstreamIdLength = 128;
constexpr std::string_view kGatewayResourceUriPrefix =
    "cxxmcp-gateway-resource://";

bool has_forbidden_upstream_id_char(std::string_view value) {
  return std::any_of(value.begin(), value.end(), [](char ch) {
    const auto byte = static_cast<unsigned char>(ch);
    return byte < 0x21 || byte > 0x7e || ch == '.' || ch == '/' ||
           ch == '\\' || std::isspace(byte) != 0;
  });
}

bool is_unreserved_uri_char(unsigned char ch) {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
         (ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == '_' ||
         ch == '~';
}

char hex_digit(unsigned char value) {
  return static_cast<char>(value < 10 ? ('0' + value) : ('A' + value - 10));
}

std::string percent_encode(std::string_view value) {
  std::string encoded;
  encoded.reserve(value.size());
  for (const auto ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    if (is_unreserved_uri_char(byte)) {
      encoded.push_back(static_cast<char>(byte));
      continue;
    }
    encoded.push_back('%');
    encoded.push_back(hex_digit(static_cast<unsigned char>(byte >> 4)));
    encoded.push_back(hex_digit(static_cast<unsigned char>(byte & 0x0f)));
  }
  return encoded;
}

std::string percent_encode_resource_template(std::string_view value) {
  std::string encoded;
  encoded.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '{') {
      const auto close = value.find('}', i + 1);
      if (close != std::string_view::npos) {
        encoded.append(value.substr(i, close - i + 1));
        i = close;
        continue;
      }
    }
    encoded.append(percent_encode(value.substr(i, 1)));
  }
  return encoded;
}

std::optional<unsigned char> from_hex(char ch) {
  if (ch >= '0' && ch <= '9') {
    return static_cast<unsigned char>(ch - '0');
  }
  if (ch >= 'A' && ch <= 'F') {
    return static_cast<unsigned char>(ch - 'A' + 10);
  }
  if (ch >= 'a' && ch <= 'f') {
    return static_cast<unsigned char>(ch - 'a' + 10);
  }
  return std::nullopt;
}

core::Result<std::string> percent_decode(std::string_view value,
                                         std::string_view context) {
  std::string decoded;
  decoded.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] != '%') {
      decoded.push_back(value[i]);
      continue;
    }
    if (i + 2 >= value.size()) {
      return mcp::core::unexpected(make_gateway_error(
          protocol::ErrorCode::InvalidParams,
          "gateway resource URI has invalid percent encoding",
          std::string(context)));
    }
    const auto high = from_hex(value[i + 1]);
    const auto low = from_hex(value[i + 2]);
    if (!high.has_value() || !low.has_value()) {
      return mcp::core::unexpected(make_gateway_error(
          protocol::ErrorCode::InvalidParams,
          "gateway resource URI has invalid percent encoding",
          std::string(context)));
    }
    decoded.push_back(static_cast<char>((*high << 4) | *low));
    i += 2;
  }
  return decoded;
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

core::Result<core::Unit> validate_gateway_runtime_config(
    const GatewayRuntimeConfig& runtime) {
  switch (runtime.upstream_session_mode) {
    case UpstreamSessionMode::per_call:
    case UpstreamSessionMode::persistent:
      break;
    default:
      return mcp::core::unexpected(make_gateway_error(
          protocol::ErrorCode::InvalidParams,
          "gateway upstream session mode is not supported"));
  }

  if (runtime.persistent_session_pool_size == 0) {
    return mcp::core::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidParams,
        "gateway persistent session pool size must be positive"));
  }

  if (runtime.persistent_session_acquire_timeout.count() < 0) {
    return mcp::core::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidParams,
        "gateway persistent session acquire timeout must be non-negative"));
  }

  if (runtime.active_call_drain_timeout.count() < 0) {
    return mcp::core::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidParams,
        "gateway active call drain timeout must be non-negative"));
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
  for (const auto& tool_name : config.tool_policy.allow_tools) {
    if (tool_name.empty()) {
      return mcp::core::unexpected(make_gateway_error(
          protocol::ErrorCode::InvalidParams,
          "gateway tool allow policy entries must not be empty"));
    }
  }
  for (const auto& tool_name : config.tool_policy.deny_tools) {
    if (tool_name.empty()) {
      return mcp::core::unexpected(make_gateway_error(
          protocol::ErrorCode::InvalidParams,
          "gateway tool deny policy entries must not be empty"));
    }
  }
  for (const auto& resource_uri : config.resource_policy.allow_resources) {
    if (resource_uri.empty()) {
      return mcp::core::unexpected(make_gateway_error(
          protocol::ErrorCode::InvalidParams,
          "gateway resource allow policy entries must not be empty"));
    }
  }
  for (const auto& resource_uri : config.resource_policy.deny_resources) {
    if (resource_uri.empty()) {
      return mcp::core::unexpected(make_gateway_error(
          protocol::ErrorCode::InvalidParams,
          "gateway resource deny policy entries must not be empty"));
    }
  }
  for (const auto& prompt_name : config.prompt_policy.allow_prompts) {
    if (prompt_name.empty()) {
      return mcp::core::unexpected(make_gateway_error(
          protocol::ErrorCode::InvalidParams,
          "gateway prompt allow policy entries must not be empty"));
    }
  }
  for (const auto& prompt_name : config.prompt_policy.deny_prompts) {
    if (prompt_name.empty()) {
      return mcp::core::unexpected(make_gateway_error(
          protocol::ErrorCode::InvalidParams,
          "gateway prompt deny policy entries must not be empty"));
    }
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
        if (upstream.process_stdio.timeout.count() <= 0) {
          return mcp::core::unexpected(make_gateway_error(
              protocol::ErrorCode::InvalidParams,
              "enabled process stdio upstream timeout must be positive",
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

core::Result<GatewayConfig> merge_gateway_config_upstreams(
    GatewayConfig base, GatewayConfig appended) {
  base.upstreams.insert(base.upstreams.end(),
                        std::make_move_iterator(appended.upstreams.begin()),
                        std::make_move_iterator(appended.upstreams.end()));
  auto valid = validate_gateway_config(base);
  if (!valid) {
    return mcp::core::unexpected(valid.error());
  }
  return base;
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

std::string GatewayRouter::expose_prompt_name(
    std::string_view upstream_id, std::string_view upstream_prompt_name) {
  return std::string(upstream_id) + "." + std::string(upstream_prompt_name);
}

core::Result<ResolvedPromptName> GatewayRouter::resolve_prompt_name(
    std::string_view exposed_name) {
  const auto dot = exposed_name.find('.');
  if (dot == std::string_view::npos || dot == 0 ||
      dot + 1 >= exposed_name.size()) {
    return mcp::core::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidParams,
        "gateway prompt name must use '<upstream>.<prompt>'",
        std::string(exposed_name)));
  }

  const auto upstream_id = exposed_name.substr(0, dot);
  auto valid_id = validate_upstream_id(upstream_id);
  if (!valid_id) {
    return mcp::core::unexpected(valid_id.error());
  }

  return ResolvedPromptName{
      .upstream_id = std::string(upstream_id),
      .upstream_prompt_name = std::string(exposed_name.substr(dot + 1)),
  };
}

std::string GatewayRouter::expose_resource_uri(
    std::string_view upstream_id, std::string_view upstream_uri) {
  return std::string(kGatewayResourceUriPrefix) + percent_encode(upstream_id) +
         "/" + percent_encode(upstream_uri);
}

std::string GatewayRouter::expose_resource_template_uri(
    std::string_view upstream_id, std::string_view upstream_uri_template) {
  return std::string(kGatewayResourceUriPrefix) + percent_encode(upstream_id) +
         "/" + percent_encode_resource_template(upstream_uri_template);
}

core::Result<ResolvedResourceUri> GatewayRouter::resolve_resource_uri(
    std::string_view exposed_uri) {
  if (!exposed_uri.starts_with(kGatewayResourceUriPrefix)) {
    return mcp::core::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidParams,
        "gateway resource URI must use 'cxxmcp-gateway-resource://' scheme",
        std::string(exposed_uri)));
  }

  const auto body = exposed_uri.substr(kGatewayResourceUriPrefix.size());
  const auto slash = body.find('/');
  if (slash == std::string_view::npos || slash == 0 ||
      slash + 1 >= body.size()) {
    return mcp::core::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidParams,
        "gateway resource URI must use '<encoded-upstream>/<encoded-uri>'",
        std::string(exposed_uri)));
  }

  auto upstream_id = percent_decode(body.substr(0, slash), exposed_uri);
  if (!upstream_id) {
    return mcp::core::unexpected(upstream_id.error());
  }
  auto valid_id = validate_upstream_id(*upstream_id);
  if (!valid_id) {
    return mcp::core::unexpected(valid_id.error());
  }

  auto upstream_uri = percent_decode(body.substr(slash + 1), exposed_uri);
  if (!upstream_uri) {
    return mcp::core::unexpected(upstream_uri.error());
  }
  if (upstream_uri->empty()) {
    return mcp::core::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidParams,
        "gateway resource URI must preserve a non-empty upstream URI",
        std::string(exposed_uri)));
  }

  return ResolvedResourceUri{
      .upstream_id = std::move(*upstream_id),
      .upstream_uri = std::move(*upstream_uri),
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

core::Result<ResourceRoute> GatewayRouter::resolve_resource_route(
    std::string_view exposed_uri) const {
  auto resolved = resolve_resource_uri(exposed_uri);
  if (!resolved) {
    return mcp::core::unexpected(resolved.error());
  }

  const auto* upstream = find_upstream(resolved->upstream_id);
  if (upstream == nullptr) {
    return mcp::core::unexpected(make_gateway_error(
        protocol::ErrorCode::ResourceNotFound, "gateway upstream not found",
        resolved->upstream_id));
  }
  if (!upstream->enabled) {
    return mcp::core::unexpected(make_gateway_error(
        protocol::ErrorCode::ResourceNotFound, "gateway upstream is disabled",
        resolved->upstream_id));
  }

  return ResourceRoute{
      .upstream = upstream,
      .upstream_uri = std::move(resolved->upstream_uri),
  };
}

core::Result<PromptRoute> GatewayRouter::resolve_prompt_route(
    std::string_view exposed_name) const {
  auto resolved = resolve_prompt_name(exposed_name);
  if (!resolved) {
    return mcp::core::unexpected(resolved.error());
  }

  const auto* upstream = find_upstream(resolved->upstream_id);
  if (upstream == nullptr) {
    return mcp::core::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidParams, "gateway upstream not found",
        resolved->upstream_id));
  }
  if (!upstream->enabled) {
    return mcp::core::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidParams, "gateway upstream is disabled",
        resolved->upstream_id));
  }

  return PromptRoute{
      .upstream = upstream,
      .upstream_prompt_name = std::move(resolved->upstream_prompt_name),
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
