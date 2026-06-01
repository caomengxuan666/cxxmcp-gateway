// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/gateway/config.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::gateway {

struct GatewayConfigDocument {
  GatewayConfig config;
  GatewayRuntimeConfig runtime;
};

using GatewayConfigEnvironmentResolver =
    std::function<std::optional<std::string>(std::string_view name)>;

struct GatewayConfigLoadOptions {
  GatewayConfigEnvironmentResolver environment;
};

core::Result<GatewayConfig> gateway_config_from_json(
    const protocol::Json& json);
core::Result<GatewayConfig> gateway_config_from_json(
    const protocol::Json& json, const GatewayConfigLoadOptions& options);
core::Result<GatewayConfigDocument> gateway_config_document_from_json(
    const protocol::Json& json);
core::Result<GatewayConfigDocument> gateway_config_document_from_json(
    const protocol::Json& json, const GatewayConfigLoadOptions& options);
core::Result<GatewayConfig> gateway_config_from_json_text(
    std::string_view text);
core::Result<GatewayConfig> gateway_config_from_json_text(
    std::string_view text, const GatewayConfigLoadOptions& options);
core::Result<GatewayConfigDocument> gateway_config_document_from_json_text(
    std::string_view text);
core::Result<GatewayConfigDocument> gateway_config_document_from_json_text(
    std::string_view text, const GatewayConfigLoadOptions& options);
core::Result<GatewayConfig> load_gateway_config_file(
    std::string_view path);
core::Result<GatewayConfig> load_gateway_config_file(
    std::string_view path, const GatewayConfigLoadOptions& options);
core::Result<GatewayConfigDocument> load_gateway_config_document_file(
    std::string_view path);
core::Result<GatewayConfigDocument> load_gateway_config_document_file(
    std::string_view path, const GatewayConfigLoadOptions& options);

}  // namespace mcp::gateway
