// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <string>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/tool.hpp"

namespace mcp::gateway {

struct UpstreamToolCatalog {
  std::string upstream_id;
  std::vector<protocol::ToolDefinition> tools;
};

core::Result<std::vector<protocol::ToolDefinition>> merge_tool_catalogs(
    const std::vector<UpstreamToolCatalog>& catalogs);

}  // namespace mcp::gateway
