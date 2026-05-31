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

core::Result<std::vector<protocol::Resource>> merge_resource_catalogs(
    const std::vector<UpstreamResourceCatalog>& catalogs) {
  std::vector<protocol::Resource> exposed;
  std::unordered_set<std::string> exposed_uris;

  for (const auto& catalog : catalogs) {
    auto valid_id = validate_upstream_id(catalog.upstream_id);
    if (!valid_id) {
      return mcp::core::unexpected(valid_id.error());
    }

    for (auto resource : catalog.resources) {
      const auto upstream_uri = resource.uri;
      if (upstream_uri.empty()) {
        return mcp::core::unexpected(make_gateway_error(
            protocol::ErrorCode::InvalidParams,
            "upstream resource URI must not be empty", catalog.upstream_id));
      }

      resource.uri =
          GatewayRouter::expose_resource_uri(catalog.upstream_id, upstream_uri);
      if (!exposed_uris.insert(resource.uri).second) {
        return mcp::core::unexpected(make_gateway_error(
            protocol::ErrorCode::InvalidParams,
            "duplicate exposed gateway resource URI", resource.uri));
      }

      protocol::Json meta =
          resource.meta.has_value() && resource.meta->is_object()
              ? *resource.meta
              : protocol::Json::object();
      meta["gateway"] = protocol::Json{
          {"upstreamId", catalog.upstream_id},
          {"upstreamResourceUri", upstream_uri},
      };
      resource.meta = std::move(meta);
      exposed.push_back(std::move(resource));
    }
  }

  std::sort(exposed.begin(), exposed.end(), [](const auto& lhs,
                                               const auto& rhs) {
    return lhs.uri < rhs.uri;
  });
  return exposed;
}

core::Result<std::vector<protocol::ResourceTemplate>>
merge_resource_template_catalogs(
    const std::vector<UpstreamResourceTemplateCatalog>& catalogs) {
  std::vector<protocol::ResourceTemplate> exposed;
  std::unordered_set<std::string> exposed_uri_templates;

  for (const auto& catalog : catalogs) {
    auto valid_id = validate_upstream_id(catalog.upstream_id);
    if (!valid_id) {
      return mcp::core::unexpected(valid_id.error());
    }

    for (auto resource_template : catalog.resource_templates) {
      const auto upstream_uri_template = resource_template.uri_template;
      if (upstream_uri_template.empty()) {
        return mcp::core::unexpected(make_gateway_error(
            protocol::ErrorCode::InvalidParams,
            "upstream resource template URI must not be empty",
            catalog.upstream_id));
      }

      resource_template.uri_template =
          GatewayRouter::expose_resource_template_uri(
              catalog.upstream_id, upstream_uri_template);
      if (!exposed_uri_templates.insert(resource_template.uri_template)
               .second) {
        return mcp::core::unexpected(make_gateway_error(
            protocol::ErrorCode::InvalidParams,
            "duplicate exposed gateway resource template URI",
            resource_template.uri_template));
      }

      protocol::Json meta =
          resource_template.meta.has_value() &&
                  resource_template.meta->is_object()
              ? *resource_template.meta
              : protocol::Json::object();
      meta["gateway"] = protocol::Json{
          {"upstreamId", catalog.upstream_id},
          {"upstreamResourceTemplateUri", upstream_uri_template},
      };
      resource_template.meta = std::move(meta);
      exposed.push_back(std::move(resource_template));
    }
  }

  std::sort(exposed.begin(), exposed.end(), [](const auto& lhs,
                                               const auto& rhs) {
    return lhs.uri_template < rhs.uri_template;
  });
  return exposed;
}

core::Result<std::vector<protocol::Prompt>> merge_prompt_catalogs(
    const std::vector<UpstreamPromptCatalog>& catalogs) {
  std::vector<protocol::Prompt> exposed;
  std::unordered_set<std::string> exposed_names;

  for (const auto& catalog : catalogs) {
    auto valid_id = validate_upstream_id(catalog.upstream_id);
    if (!valid_id) {
      return mcp::core::unexpected(valid_id.error());
    }

    for (auto prompt : catalog.prompts) {
      const auto upstream_name = prompt.name;
      if (upstream_name.empty()) {
        return mcp::core::unexpected(make_gateway_error(
            protocol::ErrorCode::InvalidParams,
            "upstream prompt name must not be empty", catalog.upstream_id));
      }

      prompt.name =
          GatewayRouter::expose_prompt_name(catalog.upstream_id, upstream_name);
      if (!exposed_names.insert(prompt.name).second) {
        return mcp::core::unexpected(make_gateway_error(
            protocol::ErrorCode::InvalidParams,
            "duplicate exposed gateway prompt name", prompt.name));
      }

      protocol::Json meta =
          prompt.meta.has_value() && prompt.meta->is_object()
              ? *prompt.meta
              : protocol::Json::object();
      meta["gateway"] = protocol::Json{
          {"upstreamId", catalog.upstream_id},
          {"upstreamPromptName", upstream_name},
      };
      prompt.meta = std::move(meta);
      exposed.push_back(std::move(prompt));
    }
  }

  std::sort(exposed.begin(), exposed.end(), [](const auto& lhs,
                                               const auto& rhs) {
    return lhs.name < rhs.name;
  });
  return exposed;
}

}  // namespace mcp::gateway
