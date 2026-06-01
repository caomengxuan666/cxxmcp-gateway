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

  mcp::gateway::GatewayRuntimeConfig runtime_config;
  runtime_config.upstream_session_mode =
      mcp::gateway::UpstreamSessionMode::persistent;
  runtime_config.persistent_session_pool_size = 2;
  runtime_config.persistent_session_acquire_timeout =
      std::chrono::milliseconds{100};
  runtime_config.active_call_drain_timeout =
      std::chrono::milliseconds{5000};
  runtime_config.prewarm_capabilities = true;
  const auto valid_runtime_config =
      mcp::gateway::validate_gateway_runtime_config(runtime_config);
  require(valid_runtime_config.has_value(),
          "valid gateway runtime config should validate");

  auto invalid_session_mode = runtime_config;
  invalid_session_mode.upstream_session_mode =
      static_cast<mcp::gateway::UpstreamSessionMode>(999);
  const auto invalid_mode =
      mcp::gateway::validate_gateway_runtime_config(invalid_session_mode);
  require(!invalid_mode.has_value(),
          "unsupported runtime session mode should fail validation");

  auto invalid_pool = runtime_config;
  invalid_pool.persistent_session_pool_size = 0;
  const auto invalid_pool_size =
      mcp::gateway::validate_gateway_runtime_config(invalid_pool);
  require(!invalid_pool_size.has_value(),
          "zero persistent pool size should fail validation");

  auto invalid_acquire_timeout = runtime_config;
  invalid_acquire_timeout.persistent_session_acquire_timeout =
      std::chrono::milliseconds{-1};
  const auto negative_acquire_timeout =
      mcp::gateway::validate_gateway_runtime_config(invalid_acquire_timeout);
  require(!negative_acquire_timeout.has_value(),
          "negative persistent acquire timeout should fail validation");

  auto invalid_drain_timeout = runtime_config;
  invalid_drain_timeout.active_call_drain_timeout =
      std::chrono::milliseconds{-1};
  const auto negative_drain_timeout =
      mcp::gateway::validate_gateway_runtime_config(invalid_drain_timeout);
  require(!negative_drain_timeout.has_value(),
          "negative active call drain timeout should fail validation");

  const auto exposed =
      mcp::gateway::GatewayRouter::expose_tool_name("fs", "read_file");
  require(exposed == "fs.read_file", "exposed tool name mismatch");

  const auto resolved =
      mcp::gateway::GatewayRouter::resolve_tool_name("fs.read_file");
  require(resolved.has_value(), "valid gateway tool name should resolve");
  require(resolved->upstream_id == "fs", "upstream id mismatch");
  require(resolved->upstream_tool_name == "read_file",
          "upstream tool name mismatch");

  const auto dotted_tool =
      mcp::gateway::GatewayRouter::resolve_tool_name("fs.package.read_file");
  require(dotted_tool.has_value(),
          "gateway tool name should preserve dots after upstream id");
  require(dotted_tool->upstream_id == "fs",
          "dotted tool upstream id mismatch");
  require(dotted_tool->upstream_tool_name == "package.read_file",
          "dotted upstream tool name should preserve suffix");

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
  const auto dotted_prompt =
      mcp::gateway::GatewayRouter::resolve_prompt_name("fs.team.summarize");
  require(dotted_prompt.has_value(),
          "gateway prompt name should preserve dots after upstream id");
  require(dotted_prompt->upstream_id == "fs",
          "dotted prompt upstream id mismatch");
  require(dotted_prompt->upstream_prompt_name == "team.summarize",
          "dotted upstream prompt name should preserve suffix");
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
  auto expanded_template = exposed_template;
  expanded_template.replace(expanded_template.find("{path}"),
                            std::string_view{"{path}"}.size(),
                            "docs%2Freadme.md");
  const auto resolved_template_resource =
      mcp::gateway::GatewayRouter::resolve_resource_uri(expanded_template);
  require(resolved_template_resource.has_value(),
          "expanded gateway resource template URI should resolve");
  require(resolved_template_resource->upstream_id == "fs",
          "expanded resource template upstream id mismatch");
  require(resolved_template_resource->upstream_uri ==
              "file:///workspace/docs/readme.md",
          "expanded resource template should decode routed resource URI");

  const auto invalid_resource_scheme =
      mcp::gateway::GatewayRouter::resolve_resource_uri("file:///tmp/a.txt");
  require(!invalid_resource_scheme.has_value(),
          "non-gateway resource URI should fail resolution");

  const auto invalid_resource_encoding =
      mcp::gateway::GatewayRouter::resolve_resource_uri(
          "cxxmcp-gateway-resource://fs/%XX");
  require(!invalid_resource_encoding.has_value(),
          "bad resource URI percent encoding should fail resolution");

  const auto invalid_resource_upstream =
      mcp::gateway::GatewayRouter::resolve_resource_uri(
          "cxxmcp-gateway-resource://bad%2Fid/"
          "file%3A%2F%2F%2Ftmp%2Fa.txt");
  require(!invalid_resource_upstream.has_value(),
          "resource URI with invalid decoded upstream id should fail");

  const auto empty_routed_resource_uri =
      mcp::gateway::GatewayRouter::resolve_resource_uri(
          "cxxmcp-gateway-resource://fs/");
  require(!empty_routed_resource_uri.has_value(),
          "resource URI with empty decoded upstream URI should fail");

  const auto empty_id = mcp::gateway::validate_upstream_id("");
  require(!empty_id.has_value(), "empty upstream id should fail");
  require(empty_id.error().category == "gateway",
          "empty upstream id validation should use gateway error category");

  const auto invalid_id =
      mcp::gateway::validate_upstream_id("bad/id");
  require(!invalid_id.has_value(), "path separator in upstream id should fail");
  require(invalid_id.error().category == "gateway",
          "upstream id validation should use gateway error category");

  const auto dotted_id = mcp::gateway::validate_upstream_id("bad.id");
  require(!dotted_id.has_value(), "dot in upstream id should fail");

  const auto whitespace_id =
      mcp::gateway::validate_upstream_id("bad id");
  require(!whitespace_id.has_value(),
          "whitespace in upstream id should fail");

  const auto backslash_id =
      mcp::gateway::validate_upstream_id("bad\\id");
  require(!backslash_id.has_value(),
          "backslash path separator in upstream id should fail");

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

  auto invalid_stdio_timeout_config = config;
  invalid_stdio_timeout_config.upstreams.front().process_stdio.timeout =
      std::chrono::milliseconds{0};
  const auto invalid_stdio_timeout =
      mcp::gateway::validate_gateway_config(invalid_stdio_timeout_config);
  require(!invalid_stdio_timeout.has_value(),
          "non-positive stdio upstream timeout should fail validation");

  auto file_config = config;
  file_config.name = "file-gateway";
  file_config.version = "9.9.9";
  mcp::gateway::GatewayConfig flag_config;
  mcp::gateway::UpstreamServer flag_upstream;
  flag_upstream.id = "flag";
  flag_upstream.transport =
      mcp::gateway::UpstreamTransportKind::streamable_http;
  flag_upstream.streamable_http.uri = "http://127.0.0.1:3000/mcp";
  flag_config.upstreams.push_back(std::move(flag_upstream));

  auto merged_config = mcp::gateway::merge_gateway_config_upstreams(
      std::move(file_config), std::move(flag_config));
  require(merged_config.has_value(),
          "file and command-line upstream config should merge");
  require(merged_config->name == "file-gateway",
          "merged config should preserve file gateway name");
  require(merged_config->version == "9.9.9",
          "merged config should preserve file gateway version");
  require(merged_config->upstreams.size() == 2,
          "merged config should retain file and appended upstreams");
  require(merged_config->upstreams[0].id == "fs",
          "merged config should keep file upstream order first");
  require(merged_config->upstreams[1].id == "flag",
          "merged config should append command-line upstreams");

  mcp::gateway::GatewayConfig duplicate_append;
  duplicate_append.upstreams.push_back(upstream);
  auto duplicate_merge = mcp::gateway::merge_gateway_config_upstreams(
      config, std::move(duplicate_append));
  require(!duplicate_merge.has_value(),
          "merged config should reject duplicate appended upstream ids");
  require(duplicate_merge.error().message == "duplicate upstream id",
          "merged config duplicate rejection should report stable message");

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
  read_file.meta = mcp::protocol::Json{
      {"existing", true},
      {"gateway", mcp::protocol::Json{{"owner", "upstream"}}}};
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
  require((*merged)[0].meta->at("gateway").at("owner") == "upstream",
          "tool catalog merge should preserve upstream gateway metadata");

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
  readme.meta = mcp::protocol::Json{{"existing", true},
                                    {"gateway", "upstream-string"}};
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
  require((*merged_resources)[1].meta->at("gatewayUpstreamOriginal") ==
              "upstream-string",
          "resource catalog merge should preserve non-object gateway metadata");

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
  workspace_template.meta = mcp::protocol::Json{
      {"existing", true},
      {"gateway", mcp::protocol::Json{{"upstreamId", "original"}}}};
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
  require((*merged_templates)[1]
              .meta->at("gatewayUpstreamOriginal")
              .at("upstreamId") == "original",
          "resource template catalog merge should preserve colliding gateway "
          "metadata");

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
  summarize.meta = mcp::protocol::Json{
      {"existing", true},
      {"gateway", mcp::protocol::Json{{"owner", "upstream"}}}};
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
  require((*merged_prompts)[1].meta->at("gateway").at("owner") ==
              "upstream",
          "prompt catalog merge should preserve upstream gateway metadata");

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
  require(upstream_plain.detail == "upstream 'fs'",
          "uncategorized upstream error with empty detail should preserve "
          "upstream context");

  auto detail_timeout = mcp::gateway::annotate_gateway_upstream_error(
      mcp::core::Error{3, "transport failed", "request timeout",
                       "transport"},
      "fs");
  require(detail_timeout.category == "gateway.upstream.timeout",
          "transport timeout detail should normalize to gateway timeout");
  require(detail_timeout.detail == "upstream 'fs': request timeout",
          "transport timeout detail should preserve upstream context");

  auto protocol_timeout = mcp::gateway::annotate_gateway_upstream_error(
      mcp::core::Error{4, "process stdio request timed out", "2",
                       "protocol"},
      "fs");
  require(protocol_timeout.category == "gateway.upstream.timeout",
          "protocol-shaped timeout should normalize to gateway timeout");

  auto gateway_owned = mcp::gateway::annotate_gateway_upstream_error(
      mcp::core::Error{5, "gateway decided", "already annotated",
                       "gateway.upstream.tool"},
      "fs");
  require(gateway_owned.category == "gateway.upstream.tool",
          "gateway-owned upstream categories should not be double-prefixed");
  require(gateway_owned.detail == "upstream 'fs': already annotated",
          "gateway-owned upstream detail should still receive upstream "
          "context");

  auto upstream_mcp_error = mcp::gateway::annotate_gateway_upstream_error(
      mcp::core::Error{
          static_cast<int>(mcp::protocol::ErrorCode::PermissionDenied),
          "fixture denied", R"({"reason":"fixture"})", "tool"},
      "fs");
  require(upstream_mcp_error.code ==
              static_cast<int>(mcp::protocol::ErrorCode::PermissionDenied),
          "upstream MCP error annotation should preserve upstream code");
  require(upstream_mcp_error.message == "fixture denied",
          "upstream MCP error annotation should preserve upstream message");
  require(upstream_mcp_error.category == "gateway.upstream.tool",
          "upstream MCP error category should be scoped under gateway");
  require(upstream_mcp_error.detail ==
              R"(upstream 'fs': {"reason":"fixture"})",
          "upstream MCP error annotation should preserve upstream detail");

  const auto gateway_error = mcp::gateway::make_gateway_error(
      mcp::protocol::ErrorCode::InvalidParams, "bad route", "fs.bad");
  require(gateway_error.category == "gateway",
          "gateway errors should use gateway category");
  require(gateway_error.code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "gateway errors should preserve protocol code");
  require(gateway_error.detail == "fs.bad",
          "gateway errors should preserve detail");

  const auto config_error =
      mcp::gateway::make_gateway_config_error("bad config", "upstreams");
  require(config_error.category == "gateway.config",
          "gateway config errors should use gateway config category");
  require(config_error.code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "gateway config errors should map to invalid params");
  require(config_error.detail == "upstreams",
          "gateway config errors should preserve detail");

  return 0;
}
