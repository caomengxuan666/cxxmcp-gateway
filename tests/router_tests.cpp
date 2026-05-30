// Copyright (c) 2025 [caomengxuan666]

#include <chrono>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "cxxmcp/gateway/catalog.hpp"
#include "cxxmcp/gateway/error.hpp"
#include "cxxmcp/gateway/router.hpp"
#include "cxxmcp/protocol/prompt.hpp"
#include "cxxmcp/protocol/resource.hpp"
#include "cxxmcp/protocol/types.hpp"
#include "cxxmcp/protocol/tool.hpp"

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

}  // namespace

int main() {
  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = "fs";
  upstream.process_stdio.command = "fixture-server";
  config.upstreams.push_back(upstream);

  const auto valid_config = mcp::gateway::validate_gateway_config(config);
  require(valid_config.has_value(), "valid gateway config should validate");

  const auto exposed =
      mcp::gateway::GatewayRouter::expose_tool_name("fs", "read_file");
  require(exposed == "fs.read_file", "exposed tool name mismatch");

  const auto resolved =
      mcp::gateway::GatewayRouter::resolve_tool_name("fs.read_file");
  require(resolved.has_value(), "valid gateway tool name should resolve");
  require(resolved->upstream_id == "fs", "upstream id mismatch");
  require(resolved->upstream_tool_name == "read_file",
          "upstream tool name mismatch");

  const auto invalid = mcp::gateway::GatewayRouter::resolve_tool_name("broken");
  require(!invalid.has_value(), "invalid gateway tool name should fail");

  const auto exposed_prompt =
      mcp::gateway::GatewayRouter::expose_prompt_name("fs", "summarize");
  require(exposed_prompt == "fs.summarize",
          "exposed prompt name mismatch");
  const auto resolved_prompt =
      mcp::gateway::GatewayRouter::resolve_prompt_name("fs.summarize");
  require(resolved_prompt.has_value(),
          "valid gateway prompt name should resolve");
  require(resolved_prompt->upstream_id == "fs",
          "prompt upstream id mismatch");
  require(resolved_prompt->upstream_prompt_name == "summarize",
          "upstream prompt name mismatch");
  const auto invalid_prompt =
      mcp::gateway::GatewayRouter::resolve_prompt_name("broken");
  require(!invalid_prompt.has_value(),
          "invalid gateway prompt name should fail");

  const auto exposed_resource = mcp::gateway::GatewayRouter::expose_resource_uri(
      "fs:local", "file:///tmp/a b.txt?x=1#frag");
  require(exposed_resource ==
              "cxxmcp-gateway-resource://fs%3Alocal/"
              "file%3A%2F%2F%2Ftmp%2Fa%20b.txt%3Fx%3D1%23frag",
          "exposed resource URI should percent-encode route parts");
  const auto resolved_resource =
      mcp::gateway::GatewayRouter::resolve_resource_uri(exposed_resource);
  require(resolved_resource.has_value(),
          "valid gateway resource URI should resolve");
  require(resolved_resource->upstream_id == "fs:local",
          "resource URI upstream id mismatch");
  require(resolved_resource->upstream_uri == "file:///tmp/a b.txt?x=1#frag",
          "resource URI upstream URI mismatch");

  const auto exposed_template =
      mcp::gateway::GatewayRouter::expose_resource_template_uri(
          "fs", "file:///workspace/{path}");
  require(exposed_template ==
              "cxxmcp-gateway-resource://fs/"
              "file%3A%2F%2F%2Fworkspace%2F{path}",
          "exposed resource template URI should preserve template variables");

  const auto invalid_resource_scheme =
      mcp::gateway::GatewayRouter::resolve_resource_uri("file:///tmp/a.txt");
  require(!invalid_resource_scheme.has_value(),
          "non-gateway resource URI should fail resolution");

  const auto invalid_resource_encoding =
      mcp::gateway::GatewayRouter::resolve_resource_uri(
          "cxxmcp-gateway-resource://fs/%XX");
  require(!invalid_resource_encoding.has_value(),
          "bad resource URI percent encoding should fail resolution");

  const auto empty_id = mcp::gateway::validate_upstream_id("");
  require(!empty_id.has_value(), "empty upstream id should fail");
  require(empty_id.error().category == "gateway",
          "empty upstream id validation should use gateway error category");

  const auto invalid_id =
      mcp::gateway::validate_upstream_id("bad/id");
  require(!invalid_id.has_value(), "path separator in upstream id should fail");
  require(invalid_id.error().category == "gateway",
          "upstream id validation should use gateway error category");

  const auto non_ascii_id =
      mcp::gateway::validate_upstream_id("bad\xff");
  require(!non_ascii_id.has_value(), "non-ASCII upstream id should fail");

  const auto control_id =
      mcp::gateway::validate_upstream_id(std::string{"bad"} + '\x1f');
  require(!control_id.has_value(), "control char in upstream id should fail");

  const auto long_id =
      mcp::gateway::validate_upstream_id(std::string(129, 'a'));
  require(!long_id.has_value(), "overlong upstream id should fail");

  auto duplicate_config = config;
  duplicate_config.upstreams.push_back(config.upstreams.front());
  const auto duplicate =
      mcp::gateway::validate_gateway_config(duplicate_config);
  require(!duplicate.has_value(), "duplicate upstream ids should fail");

  auto invalid_http_timeout_config = config;
  invalid_http_timeout_config.upstreams.front().transport =
      mcp::gateway::UpstreamTransportKind::streamable_http;
  invalid_http_timeout_config.upstreams.front().streamable_http.uri =
      "http://127.0.0.1:3000/mcp";
  invalid_http_timeout_config.upstreams.front().streamable_http.timeout =
      std::chrono::milliseconds{0};
  const auto invalid_http_timeout =
      mcp::gateway::validate_gateway_config(invalid_http_timeout_config);
  require(!invalid_http_timeout.has_value(),
          "non-positive HTTP upstream timeout should fail validation");

  mcp::gateway::GatewayRouter router(config);
  const auto route = router.resolve_tool_route("fs.read_file");
  require(route.has_value(), "known enabled upstream route should resolve");
  require(route->upstream != nullptr, "route should include upstream");
  require(route->upstream->id == "fs", "route upstream id mismatch");
  require(route->upstream_tool_name == "read_file",
          "route upstream tool name mismatch");

  auto disabled_config = config;
  disabled_config.upstreams.front().enabled = false;
  mcp::gateway::GatewayRouter disabled_router(std::move(disabled_config));
  const auto disabled_route = disabled_router.resolve_tool_route("fs.read_file");
  require(!disabled_route.has_value(), "disabled upstream route should fail");
  const auto disabled_prompt_route =
      disabled_router.resolve_prompt_route("fs.summarize");
  require(!disabled_prompt_route.has_value(),
          "disabled prompt route should fail");

  mcp::protocol::ToolDefinition read_file;
  read_file.name = "read_file";
  read_file.meta = mcp::protocol::Json{{"existing", true}};
  mcp::protocol::ToolDefinition write_file;
  write_file.name = "write_file";

  const auto merged = mcp::gateway::merge_tool_catalogs(
      {mcp::gateway::UpstreamToolCatalog{
          .upstream_id = "fs",
          .tools = {write_file, read_file},
      }});
  require(merged.has_value(), "tool catalog merge should succeed");
  require(merged->size() == 2, "tool catalog merge size mismatch");
  require((*merged)[0].name == "fs.read_file",
          "tool catalog merge should sort exposed names");
  require((*merged)[0].meta.has_value(),
          "tool catalog merge should attach metadata");
  require((*merged)[0].meta->at("existing").get<bool>(),
          "tool catalog merge should preserve existing metadata");
  require((*merged)[0].meta->at("gateway").at("upstreamId") == "fs",
          "tool catalog merge should include upstream id metadata");
  require((*merged)[0].meta->at("gateway").at("upstreamToolName") ==
              "read_file",
          "tool catalog merge should include upstream tool name metadata");

  const auto duplicate_tools = mcp::gateway::merge_tool_catalogs(
      {mcp::gateway::UpstreamToolCatalog{
          .upstream_id = "fs",
          .tools = {read_file, read_file},
      }});
  require(!duplicate_tools.has_value(),
          "duplicate exposed tool names should fail catalog merge");
  require(duplicate_tools.error().category == "gateway",
          "catalog merge should use gateway error category");

  mcp::protocol::ToolDefinition empty_name_tool;
  empty_name_tool.name = "";
  const auto empty_tool_name = mcp::gateway::merge_tool_catalogs(
      {mcp::gateway::UpstreamToolCatalog{
          .upstream_id = "fs",
          .tools = {empty_name_tool},
      }});
  require(!empty_tool_name.has_value(),
          "catalog merge should reject empty upstream tool names");
  require(empty_tool_name.error().category == "gateway",
          "empty tool name error should use gateway category");

  mcp::protocol::Resource readme;
  readme.uri = "file:///tmp/readme.md";
  readme.name = "Readme";
  readme.meta = mcp::protocol::Json{{"existing", true}};
  mcp::protocol::Resource config_resource;
  config_resource.uri = "file:///tmp/config.json";
  config_resource.name = "Config";

  const auto merged_resources = mcp::gateway::merge_resource_catalogs(
      {mcp::gateway::UpstreamResourceCatalog{
          .upstream_id = "fs",
          .resources = {readme, config_resource},
      }});
  require(merged_resources.has_value(), "resource catalog merge should succeed");
  require(merged_resources->size() == 2, "resource catalog merge size mismatch");
  require((*merged_resources)[0].uri.find("config.json") != std::string::npos,
          "resource catalog merge should sort exposed URIs");
  require((*merged_resources)[1].meta.has_value(),
          "resource catalog merge should attach metadata");
  require((*merged_resources)[1].meta->at("existing").get<bool>(),
          "resource catalog merge should preserve existing metadata");
  require((*merged_resources)[1].meta->at("gateway").at("upstreamId") == "fs",
          "resource catalog merge should include upstream id metadata");
  require((*merged_resources)[1]
              .meta->at("gateway")
              .at("upstreamResourceUri") == "file:///tmp/readme.md",
          "resource catalog merge should include upstream resource URI metadata");

  const auto duplicate_resources = mcp::gateway::merge_resource_catalogs(
      {mcp::gateway::UpstreamResourceCatalog{
          .upstream_id = "fs",
          .resources = {readme, readme},
      }});
  require(!duplicate_resources.has_value(),
          "duplicate exposed resource URIs should fail catalog merge");

  mcp::protocol::Resource empty_uri_resource;
  empty_uri_resource.name = "empty";
  const auto empty_resource_uri = mcp::gateway::merge_resource_catalogs(
      {mcp::gateway::UpstreamResourceCatalog{
          .upstream_id = "fs",
          .resources = {empty_uri_resource},
      }});
  require(!empty_resource_uri.has_value(),
          "catalog merge should reject empty upstream resource URIs");

  mcp::protocol::ResourceTemplate workspace_template;
  workspace_template.uri_template = "file:///workspace/{path}";
  workspace_template.name = "Workspace";
  workspace_template.meta = mcp::protocol::Json{{"existing", true}};
  mcp::protocol::ResourceTemplate tmp_template;
  tmp_template.uri_template = "file:///tmp/{path}";
  tmp_template.name = "Tmp";

  const auto merged_templates =
      mcp::gateway::merge_resource_template_catalogs(
          {mcp::gateway::UpstreamResourceTemplateCatalog{
              .upstream_id = "fs",
              .resource_templates = {workspace_template, tmp_template},
          }});
  require(merged_templates.has_value(),
          "resource template catalog merge should succeed");
  require(merged_templates->size() == 2,
          "resource template catalog merge size mismatch");
  require((*merged_templates)[0].uri_template.find("tmp") !=
              std::string::npos,
          "resource template catalog merge should sort exposed URI templates");
  require((*merged_templates)[1].meta.has_value(),
          "resource template catalog merge should attach metadata");
  require((*merged_templates)[1].meta->at("existing").get<bool>(),
          "resource template catalog merge should preserve existing metadata");
  require((*merged_templates)[1].meta->at("gateway").at("upstreamId") == "fs",
          "resource template catalog merge should include upstream id");
  require((*merged_templates)[1]
              .meta->at("gateway")
              .at("upstreamResourceTemplateUri") ==
          "file:///workspace/{path}",
          "resource template catalog merge should include upstream URI "
          "template");

  const auto duplicate_templates =
      mcp::gateway::merge_resource_template_catalogs(
          {mcp::gateway::UpstreamResourceTemplateCatalog{
              .upstream_id = "fs",
              .resource_templates = {workspace_template, workspace_template},
          }});
  require(!duplicate_templates.has_value(),
          "duplicate exposed resource template URIs should fail catalog merge");

  mcp::protocol::ResourceTemplate empty_template_uri;
  empty_template_uri.name = "empty";
  const auto empty_template =
      mcp::gateway::merge_resource_template_catalogs(
          {mcp::gateway::UpstreamResourceTemplateCatalog{
              .upstream_id = "fs",
              .resource_templates = {empty_template_uri},
          }});
  require(!empty_template.has_value(),
          "catalog merge should reject empty upstream resource template URIs");

  mcp::protocol::Prompt summarize;
  summarize.name = "summarize";
  summarize.meta = mcp::protocol::Json{{"existing", true}};
  mcp::protocol::Prompt rewrite;
  rewrite.name = "rewrite";

  const auto merged_prompts = mcp::gateway::merge_prompt_catalogs(
      {mcp::gateway::UpstreamPromptCatalog{
          .upstream_id = "fs",
          .prompts = {rewrite, summarize},
      }});
  require(merged_prompts.has_value(), "prompt catalog merge should succeed");
  require(merged_prompts->size() == 2, "prompt catalog merge size mismatch");
  require((*merged_prompts)[0].name == "fs.rewrite",
          "prompt catalog merge should sort exposed names");
  require((*merged_prompts)[1].meta.has_value(),
          "prompt catalog merge should attach metadata");
  require((*merged_prompts)[1].meta->at("existing").get<bool>(),
          "prompt catalog merge should preserve existing metadata");
  require((*merged_prompts)[1].meta->at("gateway").at("upstreamId") == "fs",
          "prompt catalog merge should include upstream id metadata");
  require((*merged_prompts)[1]
              .meta->at("gateway")
              .at("upstreamPromptName") == "summarize",
          "prompt catalog merge should include upstream prompt name metadata");

  const auto duplicate_prompts = mcp::gateway::merge_prompt_catalogs(
      {mcp::gateway::UpstreamPromptCatalog{
          .upstream_id = "fs",
          .prompts = {summarize, summarize},
      }});
  require(!duplicate_prompts.has_value(),
          "duplicate exposed prompt names should fail catalog merge");

  mcp::protocol::Prompt empty_name_prompt;
  const auto empty_prompt_name = mcp::gateway::merge_prompt_catalogs(
      {mcp::gateway::UpstreamPromptCatalog{
          .upstream_id = "fs",
          .prompts = {empty_name_prompt},
      }});
  require(!empty_prompt_name.has_value(),
          "catalog merge should reject empty upstream prompt names");

  auto upstream_timeout = mcp::gateway::annotate_gateway_upstream_error(
      mcp::core::Error{1, "Connection timed out", "socket timeout",
                       "transport"},
      "fs");
  require(upstream_timeout.category == "gateway.upstream.timeout",
          "upstream timeout should normalize to gateway timeout category");
  require(upstream_timeout.detail.find("upstream 'fs'") != std::string::npos,
          "upstream error detail should include upstream id");
  require(upstream_timeout.detail.find("socket timeout") != std::string::npos,
          "upstream error detail should preserve original detail");

  auto upstream_plain = mcp::gateway::annotate_gateway_upstream_error(
      mcp::core::Error{2, "upstream failed", "", ""}, "fs");
  require(upstream_plain.category == "gateway.upstream",
          "uncategorized upstream error should use gateway upstream category");

  return 0;
}
