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

void require_config_document_error(const Json& json,
                                   std::string_view expected_detail,
                                   std::string_view message) {
  const auto parsed = mcp::gateway::gateway_config_document_from_json(json);
  require(!parsed.has_value(), message);
  require(parsed.error().category == "gateway.config",
          "config document parser should use gateway.config error category");
  require(parsed.error().detail == expected_detail,
          "config document type error should include field path");
}

void test_parse_json_config() {
  const Json json = {
      {"name", "gateway-test"},
      {"version", "1.2.3"},
      {"runtime",
       Json{{"upstreamSessionMode", "persistent"},
            {"prewarmCapabilities", true}}},
      {"upstreams",
       Json::array({
           Json{{"id", "stdio"},
                {"transport", "stdio"},
                {"command", "fixture"},
                {"args", Json::array({"--flag", "${GATEWAY_ROOT}"})},
                {"cwd", "${GATEWAY_CWD}"},
                {"timeoutMs", 2345},
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
  require(parsed->upstreams[0].process_stdio.timeout.count() == 2345,
          "stdio timeout should parse");
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

void test_parse_json_config_document() {
  const Json json = {
      {"runtime",
       Json{{"upstreamSessionMode", "persistent"},
            {"persistentSessionPoolSize", 3},
            {"prewarmCapabilities", true}}},
      {"upstreams",
       Json::array({Json{{"id", "stdio"},
                         {"transport", "stdio"},
                         {"command", "fixture"}}})},
  };

  auto parsed = mcp::gateway::gateway_config_document_from_json(json);
  require(parsed.has_value(), "valid JSON gateway document should parse");
  require(parsed->config.upstreams.size() == 1,
          "document config upstreams should parse");
  require(parsed->runtime.upstream_session_mode ==
              mcp::gateway::UpstreamSessionMode::persistent,
          "document runtime session mode should parse");
  require(parsed->runtime.persistent_session_pool_size == 3,
          "document persistent session pool size should parse");
  require(parsed->runtime.prewarm_capabilities,
          "document prewarm flag should parse");

  const Json per_call = {
      {"runtime", Json{{"upstreamSessionMode", "per-call"}}},
      {"upstreams",
       Json::array({Json{{"id", "stdio"},
                         {"transport", "stdio"},
                         {"command", "fixture"}}})},
  };
  parsed = mcp::gateway::gateway_config_document_from_json(per_call);
  require(parsed.has_value(), "per-call spelling should parse");
  require(parsed->runtime.upstream_session_mode ==
              mcp::gateway::UpstreamSessionMode::per_call,
          "per-call spelling should select per-call mode");

  const Json defaults = {
      {"upstreams",
       Json::array({Json{{"id", "stdio"},
                         {"transport", "stdio"},
                         {"command", "fixture"}}})},
  };
  parsed = mcp::gateway::gateway_config_document_from_json(defaults);
  require(parsed.has_value(), "runtime defaults should parse");
  require(parsed->runtime.upstream_session_mode ==
              mcp::gateway::UpstreamSessionMode::per_call,
          "default runtime session mode should be per-call");
  require(parsed->runtime.persistent_session_pool_size == 1,
          "default persistent session pool size should be one");
  require(!parsed->runtime.prewarm_capabilities,
          "default runtime prewarm should be false");
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

  const Json zero_stdio_timeout = Json{
      {"upstreams",
       Json::array({Json{{"id", "stdio"},
                         {"transport", "stdio"},
                         {"command", "fixture"},
                         {"timeoutMs", 0}}})}};
  parsed = mcp::gateway::gateway_config_from_json(zero_stdio_timeout);
  require(!parsed.has_value(), "zero stdio timeout should fail validation");
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

  require_config_document_error(
      Json{{"runtime", Json::array()},
           {"upstreams",
            Json::array({Json{{"id", "stdio"},
                              {"transport", "stdio"},
                              {"command", "fixture"}}})}},
      "$.runtime", "non-object runtime config should fail");

  require_config_document_error(
      Json{{"runtime", Json{{"upstreamSessionMode", "pooled"}}},
           {"upstreams",
            Json::array({Json{{"id", "stdio"},
                              {"transport", "stdio"},
                              {"command", "fixture"}}})}},
      "$.runtime.upstreamSessionMode",
      "unknown runtime session mode should fail");

  require_config_document_error(
      Json{{"runtime", Json{{"upstreamSessionMode", 42}}},
           {"upstreams",
            Json::array({Json{{"id", "stdio"},
                              {"transport", "stdio"},
                              {"command", "fixture"}}})}},
      "$.runtime.upstreamSessionMode",
      "non-string runtime session mode should fail");

  require_config_document_error(
      Json{{"runtime", Json{{"prewarmCapabilities", "yes"}}},
           {"upstreams",
            Json::array({Json{{"id", "stdio"},
                              {"transport", "stdio"},
                              {"command", "fixture"}}})}},
      "$.runtime.prewarmCapabilities",
      "non-boolean runtime prewarm should fail");

  require_config_document_error(
      Json{{"runtime", Json{{"persistentSessionPoolSize", 0}}},
           {"upstreams",
            Json::array({Json{{"id", "stdio"},
                              {"transport", "stdio"},
                              {"command", "fixture"}}})}},
      "$.runtime.persistentSessionPoolSize",
      "zero persistent session pool size should fail");

  require_config_document_error(
      Json{{"runtime", Json{{"persistentSessionPoolSize", -1}}},
           {"upstreams",
            Json::array({Json{{"id", "stdio"},
                              {"transport", "stdio"},
                              {"command", "fixture"}}})}},
      "$.runtime.persistentSessionPoolSize",
      "negative persistent session pool size should fail");

  require_config_document_error(
      Json{{"runtime", Json{{"persistentSessionPoolSize", "2"}}},
           {"upstreams",
            Json::array({Json{{"id", "stdio"},
                              {"transport", "stdio"},
                              {"command", "fixture"}}})}},
      "$.runtime.persistentSessionPoolSize",
      "non-integer persistent session pool size should fail");
}

void test_reject_endpoint_fields() {
  const Json upstream = Json{{"id", "stdio"},
                             {"transport", "stdio"},
                             {"command", "fixture"}};
  require_config_error(
      Json{{"host", "127.0.0.1"},
           {"upstreams", Json::array({upstream})}},
      "$.host", "config host endpoint field should fail");
  require_config_error(
      Json{{"port", 3000}, {"upstreams", Json::array({upstream})}},
      "$.port", "config port endpoint field should fail");
  require_config_error(
      Json{{"path", "/mcp"}, {"upstreams", Json::array({upstream})}},
      "$.path", "config path endpoint field should fail");
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

  auto document = mcp::gateway::load_gateway_config_document_file(
      CXXMCP_GATEWAY_CONFIG_IO_FIXTURE);
  require(document.has_value(), "config fixture document should load");
  require(document->runtime.upstream_session_mode ==
              mcp::gateway::UpstreamSessionMode::persistent,
          "config fixture runtime session mode should load");
  require(document->runtime.persistent_session_pool_size == 2,
          "config fixture persistent session pool size should load");
  require(document->runtime.prewarm_capabilities,
          "config fixture runtime prewarm should load");

  const auto missing_path =
      std::string(CXXMCP_GATEWAY_CONFIG_IO_FIXTURE) + ".missing";
  auto missing = mcp::gateway::load_gateway_config_file(missing_path);
  require(!missing.has_value(), "missing config file should fail");
  require(missing.error().category == "gateway.config",
          "missing config file should use gateway.config category");
  require(missing.error().message == "failed to open gateway config file",
          "missing config file should report stable open failure");
  require(missing.error().detail == missing_path,
          "missing config file should preserve path detail");

  auto malformed = mcp::gateway::load_gateway_config_file(
      CXXMCP_GATEWAY_MALFORMED_CONFIG_IO_FIXTURE);
  require(!malformed.has_value(), "malformed config JSON should fail");
  require(malformed.error().category == "gateway.config",
          "malformed config JSON should use gateway.config category");
  require(malformed.error().message == "failed to parse gateway config JSON",
          "malformed config JSON should report stable parse failure");
  require(malformed.error().detail ==
              std::string(CXXMCP_GATEWAY_MALFORMED_CONFIG_IO_FIXTURE),
          "malformed config JSON should preserve path detail");
}

}  // namespace

int main() {
  try {
    test_parse_json_config();
    test_parse_json_config_document();
    test_reject_invalid_config();
    test_reject_optional_string_type_mismatches();
    test_reject_structured_field_type_mismatches();
    test_reject_endpoint_fields();
    test_parse_disabled_upstreams_without_connection_fields();
    test_load_config_file();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "gateway config_io test failed: " << ex.what() << "\n";
    return 1;
  }
}
