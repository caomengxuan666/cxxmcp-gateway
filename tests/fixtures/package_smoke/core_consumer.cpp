// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/gateway.hpp>
#include <cxxmcp/gateway/catalog.hpp>
#include <cxxmcp/gateway/config.hpp>
#include <cxxmcp/gateway/error.hpp>
#include <cxxmcp/gateway/router.hpp>

#include <string>
#include <utility>

int main() {
  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer base_upstream;
  base_upstream.id = "local";
  base_upstream.process_stdio.command = "fixture-server";
  config.upstreams.push_back(std::move(base_upstream));

  auto valid = mcp::gateway::validate_gateway_config(config);
  if (!valid) {
    return 1;
  }

  mcp::gateway::GatewayConfig appended;
  mcp::gateway::UpstreamServer appended_upstream;
  appended_upstream.id = "remote";
  appended_upstream.transport =
      mcp::gateway::UpstreamTransportKind::streamable_http;
  appended_upstream.streamable_http.uri = "http://127.0.0.1:3000/mcp";
  appended.upstreams.push_back(std::move(appended_upstream));

  auto merged = mcp::gateway::merge_gateway_config_upstreams(
      std::move(config), std::move(appended));
  if (!merged || merged->upstreams.size() != 2 ||
      merged->upstreams[0].id != "local" ||
      merged->upstreams[1].id != "remote") {
    return 2;
  }

  const auto resolved =
      mcp::gateway::GatewayRouter::resolve_tool_name("local.echo");
  if (!resolved || resolved->upstream_id != "local" ||
      resolved->upstream_tool_name != "echo") {
    return 3;
  }

  const auto exposed_resource = mcp::gateway::GatewayRouter::expose_resource_uri(
      "local", "file:///tmp/readme.md");
  const auto resolved_resource =
      mcp::gateway::GatewayRouter::resolve_resource_uri(exposed_resource);
  if (!resolved_resource ||
      resolved_resource->upstream_id != "local" ||
      resolved_resource->upstream_uri != "file:///tmp/readme.md") {
    return 4;
  }

  const auto exposed_prompt =
      mcp::gateway::GatewayRouter::expose_prompt_name("local", "summarize");
  const auto resolved_prompt =
      mcp::gateway::GatewayRouter::resolve_prompt_name(exposed_prompt);
  if (!resolved_prompt ||
      resolved_prompt->upstream_id != "local" ||
      resolved_prompt->upstream_prompt_name != "summarize") {
    return 5;
  }

  mcp::protocol::ToolDefinition tool;
  tool.name = "echo";
  auto tools = mcp::gateway::merge_tool_catalogs(
      {mcp::gateway::UpstreamToolCatalog{.upstream_id = "local",
                                         .tools = {tool}}});
  if (!tools || tools->size() != 1 ||
      tools->front().name != "local.echo" ||
      !tools->front().meta.has_value() ||
      tools->front().meta->at("gateway").at("upstreamId") != "local") {
    return 6;
  }

  mcp::protocol::Resource resource;
  resource.uri = "file:///tmp/readme.md";
  resource.name = "Readme";
  auto resources = mcp::gateway::merge_resource_catalogs(
      {mcp::gateway::UpstreamResourceCatalog{.upstream_id = "local",
                                             .resources = {resource}}});
  if (!resources || resources->size() != 1 ||
      resources->front().uri != exposed_resource ||
      resources->front().meta->at("gateway").at("upstreamResourceUri") !=
          "file:///tmp/readme.md") {
    return 7;
  }

  mcp::protocol::ResourceTemplate resource_template;
  resource_template.uri_template = "file:///tmp/{path}";
  resource_template.name = "Tmp";
  const auto exposed_template =
      mcp::gateway::GatewayRouter::expose_resource_template_uri(
          "local", resource_template.uri_template);
  auto templates = mcp::gateway::merge_resource_template_catalogs(
      {mcp::gateway::UpstreamResourceTemplateCatalog{
          .upstream_id = "local",
          .resource_templates = {resource_template}}});
  if (!templates || templates->size() != 1 ||
      templates->front().uri_template != exposed_template ||
      templates->front()
              .meta->at("gateway")
              .at("upstreamResourceTemplateUri") != "file:///tmp/{path}") {
    return 8;
  }

  mcp::protocol::Prompt prompt;
  prompt.name = "summarize";
  auto prompts = mcp::gateway::merge_prompt_catalogs(
      {mcp::gateway::UpstreamPromptCatalog{.upstream_id = "local",
                                           .prompts = {prompt}}});
  if (!prompts || prompts->size() != 1 ||
      prompts->front().name != exposed_prompt ||
      prompts->front().meta->at("gateway").at("upstreamPromptName") !=
          "summarize") {
    return 9;
  }

  const auto config_error =
      mcp::gateway::make_gateway_config_error("bad config", "field");
  if (config_error.category != "gateway.config" ||
      config_error.detail != "field") {
    return 10;
  }

  return 0;
}
