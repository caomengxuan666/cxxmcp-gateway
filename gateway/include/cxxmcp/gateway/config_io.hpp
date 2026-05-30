// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <string_view>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/gateway/config.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::gateway {

core::Result<GatewayConfig> gateway_config_from_json(
    const protocol::Json& json);
core::Result<GatewayConfig> load_gateway_config_file(
    std::string_view path);

}  // namespace mcp::gateway
