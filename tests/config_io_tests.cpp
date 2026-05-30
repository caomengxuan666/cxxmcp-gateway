// Copyright (c) 2025 [caomengxuan666]

#include <stdexcept>
#include <iostream>
#include <string>
#include <string_view>

#include "cxxmcp/gateway/config_io.hpp"

namespace {

using Json = mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void test_parse_json_config() {
  const Json json = {
      {"name", "gateway-test"},
      {"version", "1.2.3"},
      {"upstreams",
       Json::array({
           Json{{"id", "stdio"},
                {"transport", "stdio"},
                {"command", "fixture"},
                {"args", Json::array({"--flag", "${GATEWAY_ROOT}"})},
                {"cwd", "${GATEWAY_CWD}"},
                {"env",
                 Json{{"A", "B"},
                      {"TOKEN", "${GATEWAY_TOKEN}"}}}},
           Json{{"id", "http"},
                {"transport", "http"},
                {"displayName", "HTTP upstream"},
                {"enabled", true},
                {"uri", "http://127.0.0.1:3000/mcp"},
                {"timeoutMs", 1234},
                {"headers", Json{{"Authorization", "Bearer token"}}}},
       })},
  };

  auto parsed = mcp::gateway::gateway_config_from_json(json);
  require(parsed.has_value(), "valid JSON gateway config should parse");
  require(parsed->name == "gateway-test", "gateway name should parse");
  require(parsed->version == "1.2.3", "gateway version should parse");
  require(parsed->upstreams.size() == 2, "upstreams should parse");
  require(parsed->upstreams[0].id == "stdio", "stdio id should parse");
  require(parsed->upstreams[0].process_stdio.command == "fixture",
          "stdio command should parse");
  require(parsed->upstreams[0].process_stdio.args.size() == 2,
          "stdio args should parse");
  require(parsed->upstreams[0].process_stdio.args.at(1) == "${GATEWAY_ROOT}",
          "stdio args should preserve literal environment placeholders");
  require(parsed->upstreams[0].process_stdio.cwd == "${GATEWAY_CWD}",
          "stdio cwd should preserve literal environment placeholders");
  require(parsed->upstreams[0].process_stdio.env.at("A") == "B",
          "stdio env should parse");
  require(parsed->upstreams[0].process_stdio.env.at("TOKEN") ==
              "${GATEWAY_TOKEN}",
          "stdio env should preserve literal environment placeholders");
  require(parsed->upstreams[1].id == "http", "http id should parse");
  require(parsed->upstreams[1].display_name == "HTTP upstream",
          "display name should parse");
  require(parsed->upstreams[1].streamable_http.uri ==
              "http://127.0.0.1:3000/mcp",
          "http uri should parse");
  require(parsed->upstreams[1].streamable_http.timeout.count() == 1234,
          "http timeout should parse");
  require(parsed->upstreams[1].streamable_http.headers.at("Authorization") ==
              "Bearer token",
          "http headers should parse");
}

void test_reject_invalid_config() {
  const Json missing_upstreams = Json{{"name", "bad"}};
  auto parsed = mcp::gateway::gateway_config_from_json(missing_upstreams);
  require(!parsed.has_value(), "missing upstreams should fail");
  require(parsed.error().category == "gateway.config",
          "config parser should use gateway.config error category");

  const Json invalid_transport = Json{
      {"upstreams",
       Json::array({Json{{"id", "bad"}, {"transport", "websocket"}}})}};
  parsed = mcp::gateway::gateway_config_from_json(invalid_transport);
  require(!parsed.has_value(), "unknown transport should fail");

  const Json enabled_stdio_missing_command = Json{
      {"upstreams",
       Json::array({Json{{"id", "stdio"}, {"transport", "stdio"}}})}};
  parsed =
      mcp::gateway::gateway_config_from_json(enabled_stdio_missing_command);
  require(!parsed.has_value(), "enabled stdio command should be required");

  const Json enabled_http_missing_uri = Json{
      {"upstreams",
       Json::array({Json{{"id", "http"}, {"transport", "http"}}})}};
  parsed = mcp::gateway::gateway_config_from_json(enabled_http_missing_uri);
  require(!parsed.has_value(), "enabled HTTP uri should be required");

  const Json empty_id = Json{
      {"upstreams",
       Json::array({Json{{"id", ""},
                         {"transport", "stdio"},
                         {"command", "fixture"}}})}};
  parsed = mcp::gateway::gateway_config_from_json(empty_id);
  require(!parsed.has_value(), "empty upstream id should fail validation");

  const Json invalid_id = Json{
      {"upstreams",
       Json::array({Json{{"id", "bad/id"},
                         {"transport", "stdio"},
                         {"command", "fixture"}}})}};
  parsed = mcp::gateway::gateway_config_from_json(invalid_id);
  require(!parsed.has_value(), "invalid upstream id should fail validation");

  const Json zero_timeout = Json{
      {"upstreams",
       Json::array({Json{{"id", "http"},
                         {"transport", "http"},
                         {"uri", "http://127.0.0.1:3000/mcp"},
                         {"timeoutMs", 0}}})}};
  parsed = mcp::gateway::gateway_config_from_json(zero_timeout);
  require(!parsed.has_value(), "zero HTTP timeout should fail validation");
}

void test_parse_disabled_upstreams_without_connection_fields() {
  const Json json = {
      {"upstreams",
       Json::array({
           Json{{"id", "disabled_stdio"},
                {"transport", "stdio"},
                {"enabled", false}},
           Json{{"id", "disabled_http"},
                {"transport", "http"},
                {"enabled", false}},
       })},
  };

  auto parsed = mcp::gateway::gateway_config_from_json(json);
  require(parsed.has_value(),
          "disabled upstreams should not require connection fields");
  require(parsed->upstreams.size() == 2, "disabled upstreams should parse");
  require(!parsed->upstreams[0].enabled, "disabled stdio flag should parse");
  require(parsed->upstreams[0].process_stdio.command.empty(),
          "disabled stdio command should remain empty");
  require(!parsed->upstreams[1].enabled, "disabled HTTP flag should parse");
  require(parsed->upstreams[1].streamable_http.uri.empty(),
          "disabled HTTP uri should remain empty");
}

void test_load_config_file() {
  auto loaded = mcp::gateway::load_gateway_config_file(
      CXXMCP_GATEWAY_CONFIG_IO_FIXTURE);
  require(loaded.has_value(), "config fixture should load");
  require(loaded->upstreams.size() == 1, "config fixture upstream should load");
  require(loaded->upstreams.front().id == "fixture",
          "config fixture upstream id should load");
}

}  // namespace

int main() {
  try {
    test_parse_json_config();
    test_reject_invalid_config();
    test_parse_disabled_upstreams_without_connection_fields();
    test_load_config_file();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "gateway config_io test failed: " << ex.what() << "\n";
    return 1;
  }
}
