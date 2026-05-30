// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/gateway/config_io.hpp"

#include <fstream>
#include <iterator>
#include <cstdint>
#include <string>
#include <utility>

#include "cxxmcp/gateway/error.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::gateway {
namespace {

core::Result<std::string> require_string(const protocol::Json& json,
                                         std::string_view field,
                                         std::string_view path) {
  const auto key = std::string(field);
  if (!json.contains(key) || !json.at(key).is_string()) {
    return mcp::core::unexpected(make_gateway_config_error(
        "gateway config field must be a string",
        std::string(path) + "." + key));
  }
  return json.at(key).get<std::string>();
}

core::Result<std::string> optional_transport_string(
    const protocol::Json& json, std::string_view field,
    std::string_view path) {
  const auto key = std::string(field);
  if (!json.contains(key)) {
    return std::string{};
  }
  if (!json.at(key).is_string()) {
    return mcp::core::unexpected(make_gateway_config_error(
        "gateway config field must be a string",
        std::string(path) + "." + key));
  }
  return json.at(key).get<std::string>();
}

std::string optional_string(const protocol::Json& json,
                            std::string_view field) {
  const auto key = std::string(field);
  if (!json.contains(key) || !json.at(key).is_string()) {
    return {};
  }
  return json.at(key).get<std::string>();
}

core::Result<std::vector<std::string>> optional_string_array(
    const protocol::Json& json, std::string_view field,
    std::string_view path) {
  const auto key = std::string(field);
  std::vector<std::string> values;
  if (!json.contains(key)) {
    return values;
  }
  if (!json.at(key).is_array()) {
    return mcp::core::unexpected(make_gateway_config_error(
        "gateway config field must be an array",
        std::string(path) + "." + key));
  }
  for (const auto& item : json.at(key)) {
    if (!item.is_string()) {
      return mcp::core::unexpected(make_gateway_config_error(
          "gateway config array entries must be strings",
          std::string(path) + "." + key));
    }
    values.push_back(item.get<std::string>());
  }
  return values;
}

core::Result<std::unordered_map<std::string, std::string>> optional_string_map(
    const protocol::Json& json, std::string_view field,
    std::string_view path) {
  const auto key = std::string(field);
  std::unordered_map<std::string, std::string> values;
  if (!json.contains(key)) {
    return values;
  }
  if (!json.at(key).is_object()) {
    return mcp::core::unexpected(make_gateway_config_error(
        "gateway config field must be an object",
        std::string(path) + "." + key));
  }
  for (const auto& [map_key, map_value] : json.at(key).items()) {
    if (!map_value.is_string()) {
      return mcp::core::unexpected(make_gateway_config_error(
          "gateway config object values must be strings",
          std::string(path) + "." + key + "." + map_key));
    }
    values.emplace(map_key, map_value.get<std::string>());
  }
  return values;
}

core::Result<UpstreamServer> upstream_from_json(const protocol::Json& json,
                                                std::size_t index) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        make_gateway_config_error("gateway upstream entry must be an object",
                     "upstreams[" + std::to_string(index) + "]"));
  }

  const auto path = "upstreams[" + std::to_string(index) + "]";
  auto id = require_string(json, "id", path);
  if (!id) {
    return mcp::core::unexpected(id.error());
  }
  auto transport = require_string(json, "transport", path);
  if (!transport) {
    return mcp::core::unexpected(transport.error());
  }

  UpstreamServer upstream;
  upstream.id = std::move(*id);
  upstream.display_name = optional_string(json, "displayName");
  if (json.contains("enabled")) {
    if (!json.at("enabled").is_boolean()) {
      return mcp::core::unexpected(make_gateway_config_error(
          "gateway config field must be a boolean", path + ".enabled"));
    }
    upstream.enabled = json.at("enabled").get<bool>();
  }

  if (*transport == "stdio") {
    upstream.transport = UpstreamTransportKind::process_stdio;
    auto command = upstream.enabled ? require_string(json, "command", path)
                                    : optional_transport_string(
                                          json, "command", path);
    if (!command) {
      return mcp::core::unexpected(command.error());
    }
    upstream.process_stdio.command = std::move(*command);
    auto args = optional_string_array(json, "args", path);
    if (!args) {
      return mcp::core::unexpected(args.error());
    }
    upstream.process_stdio.args = std::move(*args);
    upstream.process_stdio.cwd = optional_string(json, "cwd");
    auto env = optional_string_map(json, "env", path);
    if (!env) {
      return mcp::core::unexpected(env.error());
    }
    upstream.process_stdio.env = std::move(*env);
  } else if (*transport == "http" || *transport == "streamable_http") {
    upstream.transport = UpstreamTransportKind::streamable_http;
    auto uri = upstream.enabled ? require_string(json, "uri", path)
                                : optional_transport_string(json, "uri", path);
    if (!uri) {
      return mcp::core::unexpected(uri.error());
    }
    upstream.streamable_http.uri = std::move(*uri);
    auto headers = optional_string_map(json, "headers", path);
    if (!headers) {
      return mcp::core::unexpected(headers.error());
    }
    upstream.streamable_http.headers = std::move(*headers);
    if (json.contains("timeoutMs")) {
      if (!json.at("timeoutMs").is_number_integer() ||
          json.at("timeoutMs").get<std::int64_t>() <= 0) {
        return mcp::core::unexpected(make_gateway_config_error(
            "gateway config field must be a positive integer",
            path + ".timeoutMs"));
      }
      upstream.streamable_http.timeout =
          std::chrono::milliseconds{json.at("timeoutMs").get<std::int64_t>()};
    }
  } else {
    return mcp::core::unexpected(make_gateway_config_error(
        "gateway upstream transport must be 'stdio' or 'http'",
        path + ".transport"));
  }

  return upstream;
}

}  // namespace

core::Result<GatewayConfig> gateway_config_from_json(
    const protocol::Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        make_gateway_config_error("gateway config root must be an object"));
  }

  GatewayConfig config;
  config.name = optional_string(json, "name");
  if (config.name.empty()) {
    config.name = "cxxmcp-gateway";
  }
  config.version = optional_string(json, "version");
  if (config.version.empty()) {
    config.version = "0.1.0";
  }

  if (!json.contains("upstreams") || !json.at("upstreams").is_array()) {
    return mcp::core::unexpected(make_gateway_config_error(
        "gateway config field must be an array", "upstreams"));
  }

  for (std::size_t i = 0; i < json.at("upstreams").size(); ++i) {
    auto upstream = upstream_from_json(json.at("upstreams").at(i), i);
    if (!upstream) {
      return mcp::core::unexpected(upstream.error());
    }
    config.upstreams.push_back(std::move(*upstream));
  }

  auto valid = validate_gateway_config(config);
  if (!valid) {
    return mcp::core::unexpected(valid.error());
  }
  return config;
}

core::Result<GatewayConfig> load_gateway_config_file(
    std::string_view path) {
  std::ifstream input(std::string(path), std::ios::binary);
  if (!input) {
    return mcp::core::unexpected(make_gateway_config_error(
        "failed to open gateway config file", std::string(path)));
  }
  const std::string text{std::istreambuf_iterator<char>(input),
                         std::istreambuf_iterator<char>()};
  auto json = protocol::Json::parse(text, nullptr, false);
  if (json.is_discarded()) {
    return mcp::core::unexpected(make_gateway_config_error(
        "failed to parse gateway config JSON", std::string(path)));
  }
  return gateway_config_from_json(json);
}

}  // namespace mcp::gateway
