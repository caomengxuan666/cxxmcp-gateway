// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <string>
#include <string_view>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::gateway {

core::Error make_gateway_error(protocol::ErrorCode code, std::string message,
                               std::string detail = {});
core::Error make_gateway_config_error(std::string message,
                                      std::string detail = {});
core::Error annotate_gateway_upstream_error(core::Error error,
                                            std::string_view upstream_id);

}  // namespace mcp::gateway
