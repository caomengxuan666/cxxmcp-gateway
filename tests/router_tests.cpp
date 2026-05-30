// Copyright (c) 2025 [caomengxuan666]

#include <chrono>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "cxxmcp/gateway/catalog.hpp"
#include "cxxmcp/gateway/error.hpp"
#include "cxxmcp/gateway/router.hpp"
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
