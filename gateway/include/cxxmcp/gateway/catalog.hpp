// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <string>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/resource.hpp"
#include "cxxmcp/protocol/tool.hpp"

namespace mcp::gateway {

struct UpstreamToolCatalog {
  std::string upstream_id;
  std::vector<protocol::ToolDefinition> tools;
};

struct UpstreamResourceCatalog {
  std::string upstream_id;
  std::vector<protocol::Resource> resources;
};

core::Result<std::vector<protocol::ToolDefinition>> merge_tool_catalogs(
    const std::vector<UpstreamToolCatalog>& catalogs);

core::Result<std::vector<protocol::Resource>> merge_resource_catalogs(
    const std::vector<UpstreamResourceCatalog>& catalogs);

}  // namespace mcp::gateway
