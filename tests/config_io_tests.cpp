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

void require_config_error(const Json& json, std::string_view expected_detail,
                          std::string_view message) {
  const auto parsed = mcp::gateway::gateway_config_from_json(json);
  require(!parsed.has_value(), message);
  require(parsed.error().category == "gateway.config",
          "config parser should use gateway.config error category");
  require(parsed.error().detail == expected_detail,
          "config type error should include field path");
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

  const Json duplicate_id = Json{
      {"upstreams",
       Json::array({Json{{"id", "dup"},
                         {"transport", "stdio"},
                         {"command", "fixture"}},
                    Json{{"id", "dup"},
                         {"transport", "http"},
                         {"uri", "http://127.0.0.1:3000/mcp"}}})}};
  parsed = mcp::gateway::gateway_config_from_json(duplicate_id);
  require(!parsed.has_value(), "duplicate upstream id should fail validation");
  require(parsed.error().message == "duplicate upstream id",
          "duplicate upstream id should report stable validation message");

  const Json zero_timeout = Json{
      {"upstreams",
       Json::array({Json{{"id", "http"},
                         {"transport", "http"},
                         {"uri", "http://127.0.0.1:3000/mcp"},
                         {"timeoutMs", 0}}})}};
  parsed = mcp::gateway::gateway_config_from_json(zero_timeout);
  require(!parsed.has_value(), "zero HTTP timeout should fail validation");
}

void test_reject_optional_string_type_mismatches() {
  const Json numeric_name = Json{
      {"name", 42},
      {"upstreams",
       Json::array({Json{{"id", "stdio"},
                         {"transport", "stdio"},
                         {"command", "fixture"}}})}};
  auto parsed = mcp::gateway::gateway_config_from_json(numeric_name);
  require(!parsed.has_value(), "non-string gateway name should fail");
  require(parsed.error().detail == "$.name",
          "name type error should include field path");

  const Json numeric_display_name = Json{
      {"upstreams",
       Json::array({Json{{"id", "stdio"},
                         {"transport", "stdio"},
                         {"displayName", 42},
                         {"command", "fixture"}}})}};
  parsed = mcp::gateway::gateway_config_from_json(numeric_display_name);
  require(!parsed.has_value(), "non-string displayName should fail");
  require(parsed.error().detail == "upstreams[0].displayName",
          "displayName type error should include field path");

  const Json numeric_cwd = Json{
      {"upstreams",
       Json::array({Json{{"id", "stdio"},
                         {"transport", "stdio"},
                         {"command", "fixture"},
                         {"cwd", 42}}})}};
  parsed = mcp::gateway::gateway_config_from_json(numeric_cwd);
  require(!parsed.has_value(), "non-string cwd should fail");
  require(parsed.error().detail == "upstreams[0].cwd",
          "cwd type error should include field path");
}

void test_reject_structured_field_type_mismatches() {
  require_config_error(
      Json{{"upstreams",
            Json::array({Json{{"id", "stdio"},
                              {"transport", "stdio"},
                              {"enabled", "yes"},
                              {"command", "fixture"}}})}},
      "upstreams[0].enabled", "non-boolean enabled should fail");

  require_config_error(
      Json{{"upstreams",
            Json::array({Json{{"id", "stdio"},
                              {"transport", "stdio"},
                              {"command", "fixture"},
                              {"args", "--flag"}}})}},
      "upstreams[0].args", "non-array stdio args should fail");

  require_config_error(
      Json{{"upstreams",
            Json::array({Json{{"id", "stdio"},
                              {"transport", "stdio"},
                              {"command", "fixture"},
                              {"args", Json::array({"--flag", 42})}}})}},
      "upstreams[0].args", "non-string stdio args entry should fail");

  require_config_error(
      Json{{"upstreams",
            Json::array({Json{{"id", "stdio"},
                              {"transport", "stdio"},
                              {"command", "fixture"},
                              {"env", Json::array()}}})}},
      "upstreams[0].env", "non-object stdio env should fail");

  require_config_error(
      Json{{"upstreams",
            Json::array({Json{{"id", "stdio"},
                              {"transport", "stdio"},
                              {"command", "fixture"},
                              {"env", Json{{"TOKEN", 42}}}}})}},
      "upstreams[0].env.TOKEN", "non-string stdio env value should fail");

  require_config_error(
      Json{{"upstreams",
            Json::array({Json{{"id", "http"},
                              {"transport", "http"},
                              {"uri", "http://127.0.0.1:3000/mcp"},
                              {"headers", Json::array()}}})}},
      "upstreams[0].headers", "non-object HTTP headers should fail");

  require_config_error(
      Json{{"upstreams",
            Json::array({Json{{"id", "http"},
                              {"transport", "http"},
                              {"uri", "http://127.0.0.1:3000/mcp"},
                              {"headers", Json{{"Authorization", 42}}}}})}},
      "upstreams[0].headers.Authorization",
      "non-string HTTP header value should fail");

  require_config_error(
      Json{{"upstreams",
            Json::array({Json{{"id", "http"},
                              {"transport", "http"},
                              {"uri", "http://127.0.0.1:3000/mcp"},
                              {"timeoutMs", "30000"}}})}},
      "upstreams[0].timeoutMs", "non-integer HTTP timeout should fail");
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
    test_reject_optional_string_type_mismatches();
    test_reject_structured_field_type_mismatches();
    test_parse_disabled_upstreams_without_connection_fields();
    test_load_config_file();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "gateway config_io test failed: " << ex.what() << "\n";
    return 1;
  }
}
