// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/gateway/catalog.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>

#include "cxxmcp/gateway/config.hpp"
#include "cxxmcp/gateway/error.hpp"
#include "cxxmcp/gateway/router.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::gateway {

core::Result<std::vector<protocol::ToolDefinition>> merge_tool_catalogs(
    const std::vector<UpstreamToolCatalog>& catalogs) {
  std::vector<protocol::ToolDefinition> exposed;
  std::unordered_set<std::string> exposed_names;

  for (const auto& catalog : catalogs) {
    auto valid_id = validate_upstream_id(catalog.upstream_id);
    if (!valid_id) {
      return mcp::core::unexpected(valid_id.error());
    }

    for (auto tool : catalog.tools) {
      const auto upstream_name = tool.name;
      if (upstream_name.empty()) {
        return mcp::core::unexpected(make_gateway_error(
            protocol::ErrorCode::InvalidParams,
            "upstream tool name must not be empty", catalog.upstream_id));
      }
      tool.name =
          GatewayRouter::expose_tool_name(catalog.upstream_id, upstream_name);
      if (!exposed_names.insert(tool.name).second) {
        return mcp::core::unexpected(make_gateway_error(
            protocol::ErrorCode::InvalidParams,
            "duplicate exposed gateway tool name", tool.name));
      }

      protocol::Json meta =
          tool.meta.has_value() && tool.meta->is_object() ? *tool.meta
                                                          : protocol::Json::object();
      meta["gateway"] = protocol::Json{
          {"upstreamId", catalog.upstream_id},
          {"upstreamToolName", upstream_name},
      };
      tool.meta = std::move(meta);
      exposed.push_back(std::move(tool));
    }
  }

  std::sort(exposed.begin(), exposed.end(), [](const auto& lhs,
                                               const auto& rhs) {
    return lhs.name < rhs.name;
  });
  return exposed;
}

}  // namespace mcp::gateway
