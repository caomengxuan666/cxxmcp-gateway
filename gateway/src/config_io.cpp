// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/gateway/config_io.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>

#include "cxxmcp/gateway/error.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::gateway {
namespace {

bool valid_environment_name(std::string_view name) {
  if (name.empty()) {
    return false;
  }
  const auto first = static_cast<unsigned char>(name.front());
  if (std::isalpha(first) == 0 && name.front() != '_') {
    return false;
  }
  for (char ch : name.substr(1)) {
    const auto value = static_cast<unsigned char>(ch);
    if (std::isalnum(value) == 0 && ch != '_') {
      return false;
    }
  }
  return true;
}

core::Result<std::string> expand_environment_placeholders(
    std::string value, std::string_view path,
    const GatewayConfigLoadOptions& options) {
  if (!options.environment) {
    return value;
  }

  std::string expanded;
  expanded.reserve(value.size());
  std::size_t offset = 0;
  while (offset < value.size()) {
    const auto marker = value.find("${", offset);
    if (marker == std::string::npos) {
      expanded.append(value.substr(offset));
      break;
    }
    expanded.append(value.substr(offset, marker - offset));
    const auto close = value.find('}', marker + 2);
    if (close == std::string::npos) {
      return mcp::core::unexpected(make_gateway_config_error(
          "gateway config environment placeholder is not closed",
          std::string(path)));
    }
    const auto name =
        std::string_view(value).substr(marker + 2, close - marker - 2);
    if (!valid_environment_name(name)) {
      return mcp::core::unexpected(make_gateway_config_error(
          "gateway config environment placeholder name is invalid",
          std::string(path)));
    }
    auto replacement = options.environment(name);
    if (!replacement.has_value()) {
      return mcp::core::unexpected(make_gateway_config_error(
          "gateway config environment variable is not available",
          std::string(path)));
    }
    expanded.append(*replacement);
    offset = close + 1;
  }
  return expanded;
}

core::Result<std::string> require_string(
    const protocol::Json& json, std::string_view field, std::string_view path,
    const GatewayConfigLoadOptions& options) {
  const auto key = std::string(field);
  if (!json.contains(key) || !json.at(key).is_string()) {
    return mcp::core::unexpected(make_gateway_config_error(
        "gateway config field must be a string",
        std::string(path) + "." + key));
  }
  return expand_environment_placeholders(json.at(key).get<std::string>(),
                                         std::string(path) + "." + key,
                                         options);
}

core::Result<std::string> optional_transport_string(
    const protocol::Json& json, std::string_view field,
    std::string_view path, const GatewayConfigLoadOptions& options) {
  const auto key = std::string(field);
  if (!json.contains(key)) {
    return std::string{};
  }
  if (!json.at(key).is_string()) {
    return mcp::core::unexpected(make_gateway_config_error(
        "gateway config field must be a string",
        std::string(path) + "." + key));
  }
  return expand_environment_placeholders(json.at(key).get<std::string>(),
                                         std::string(path) + "." + key,
                                         options);
}

core::Result<std::string> optional_string(const protocol::Json& json,
                                          std::string_view field,
                                          std::string_view path,
                                          const GatewayConfigLoadOptions& options) {
  const auto key = std::string(field);
  if (!json.contains(key)) {
    return std::string{};
  }
  if (!json.at(key).is_string()) {
    return mcp::core::unexpected(make_gateway_config_error(
        "gateway config field must be a string",
        std::string(path) + "." + key));
  }
  return expand_environment_placeholders(json.at(key).get<std::string>(),
                                         std::string(path) + "." + key,
                                         options);
}

core::Result<core::Unit> reject_endpoint_fields(
    const protocol::Json& json) {
  const std::string fields[] = {"host", "port", "path"};
  for (const auto& field : fields) {
    if (json.contains(field)) {
      return mcp::core::unexpected(make_gateway_config_error(
          "gateway endpoint fields are not supported in config files",
          "$." + field));
    }
  }
  return core::Unit{};
}

core::Result<GatewayRuntimeConfig> runtime_config_from_json(
    const protocol::Json& json, const GatewayConfigLoadOptions& options) {
  GatewayRuntimeConfig runtime;
  if (!json.contains("runtime")) {
    return runtime;
  }
  const auto& runtime_json = json.at("runtime");
  if (!runtime_json.is_object()) {
    return mcp::core::unexpected(make_gateway_config_error(
        "gateway config field must be an object", "$.runtime"));
  }
  if (runtime_json.contains("upstreamSessionMode")) {
    if (!runtime_json.at("upstreamSessionMode").is_string()) {
      return mcp::core::unexpected(make_gateway_config_error(
          "gateway config field must be a string",
          "$.runtime.upstreamSessionMode"));
    }
    auto mode = expand_environment_placeholders(
        runtime_json.at("upstreamSessionMode").get<std::string>(),
        "$.runtime.upstreamSessionMode", options);
    if (!mode) {
      return mcp::core::unexpected(mode.error());
    }
    if (*mode == "per_call" || *mode == "per-call") {
      runtime.upstream_session_mode = UpstreamSessionMode::per_call;
    } else if (*mode == "persistent") {
      runtime.upstream_session_mode = UpstreamSessionMode::persistent;
    } else {
      return mcp::core::unexpected(make_gateway_config_error(
          "gateway runtime upstreamSessionMode must be 'per_call' or "
          "'persistent'",
          "$.runtime.upstreamSessionMode"));
    }
  }
  if (runtime_json.contains("persistentSessionPoolSize")) {
    if (!runtime_json.at("persistentSessionPoolSize").is_number_integer() ||
        runtime_json.at("persistentSessionPoolSize").get<std::int64_t>() <= 0) {
      return mcp::core::unexpected(make_gateway_config_error(
          "gateway config field must be a positive integer",
          "$.runtime.persistentSessionPoolSize"));
    }
    const auto value =
        runtime_json.at("persistentSessionPoolSize").get<std::int64_t>();
    if (static_cast<std::uint64_t>(value) >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max())) {
      return mcp::core::unexpected(make_gateway_config_error(
          "gateway config field is too large",
          "$.runtime.persistentSessionPoolSize"));
    }
    runtime.persistent_session_pool_size = static_cast<std::size_t>(value);
  }
  if (runtime_json.contains("persistentSessionAcquireTimeoutMs")) {
    if (!runtime_json.at("persistentSessionAcquireTimeoutMs")
             .is_number_integer() ||
        runtime_json.at("persistentSessionAcquireTimeoutMs")
                .get<std::int64_t>() < 0) {
      return mcp::core::unexpected(make_gateway_config_error(
          "gateway config field must be a non-negative integer",
          "$.runtime.persistentSessionAcquireTimeoutMs"));
    }
    runtime.persistent_session_acquire_timeout =
        std::chrono::milliseconds{
            runtime_json.at("persistentSessionAcquireTimeoutMs")
                .get<std::int64_t>()};
  }
  if (runtime_json.contains("activeCallDrainTimeoutMs")) {
    if (!runtime_json.at("activeCallDrainTimeoutMs").is_number_integer() ||
        runtime_json.at("activeCallDrainTimeoutMs").get<std::int64_t>() < 0) {
      return mcp::core::unexpected(make_gateway_config_error(
          "gateway config field must be a non-negative integer",
          "$.runtime.activeCallDrainTimeoutMs"));
    }
    runtime.active_call_drain_timeout =
        std::chrono::milliseconds{
            runtime_json.at("activeCallDrainTimeoutMs").get<std::int64_t>()};
  }
  if (runtime_json.contains("prewarmCapabilities")) {
    if (!runtime_json.at("prewarmCapabilities").is_boolean()) {
      return mcp::core::unexpected(make_gateway_config_error(
          "gateway config field must be a boolean",
          "$.runtime.prewarmCapabilities"));
    }
    runtime.prewarm_capabilities =
        runtime_json.at("prewarmCapabilities").get<bool>();
  }
  auto valid = validate_gateway_runtime_config(runtime);
  if (!valid) {
    return mcp::core::unexpected(
        make_gateway_config_error(valid.error().message, "$.runtime"));
  }
  return runtime;
}

core::Result<std::vector<std::string>> optional_string_array(
    const protocol::Json& json, std::string_view field,
    std::string_view path, const GatewayConfigLoadOptions& options) {
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
    auto value = expand_environment_placeholders(
        item.get<std::string>(), std::string(path) + "." + key, options);
    if (!value) {
      return mcp::core::unexpected(value.error());
    }
    values.push_back(std::move(*value));
  }
  return values;
}

core::Result<std::unordered_map<std::string, std::string>> optional_string_map(
    const protocol::Json& json, std::string_view field,
    std::string_view path, const GatewayConfigLoadOptions& options) {
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
    auto value = expand_environment_placeholders(
        map_value.get<std::string>(),
        std::string(path) + "." + key + "." + map_key, options);
    if (!value) {
      return mcp::core::unexpected(value.error());
    }
    values.emplace(map_key, std::move(*value));
  }
  return values;
}

core::Result<UpstreamServer> upstream_from_json(const protocol::Json& json,
                                                std::size_t index,
                                                const GatewayConfigLoadOptions& options) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        make_gateway_config_error("gateway upstream entry must be an object",
                     "upstreams[" + std::to_string(index) + "]"));
  }

  const auto path = "upstreams[" + std::to_string(index) + "]";
  auto id = require_string(json, "id", path, options);
  if (!id) {
    return mcp::core::unexpected(id.error());
  }
  auto transport = require_string(json, "transport", path, options);
  if (!transport) {
    return mcp::core::unexpected(transport.error());
  }

  UpstreamServer upstream;
  upstream.id = std::move(*id);
  auto display_name = optional_string(json, "displayName", path, options);
  if (!display_name) {
    return mcp::core::unexpected(display_name.error());
  }
  upstream.display_name = std::move(*display_name);
  if (json.contains("enabled")) {
    if (!json.at("enabled").is_boolean()) {
      return mcp::core::unexpected(make_gateway_config_error(
          "gateway config field must be a boolean", path + ".enabled"));
    }
    upstream.enabled = json.at("enabled").get<bool>();
  }

  if (*transport == "stdio") {
    upstream.transport = UpstreamTransportKind::process_stdio;
    auto command = upstream.enabled
                       ? require_string(json, "command", path, options)
                       : optional_transport_string(json, "command", path,
                                                   options);
    if (!command) {
      return mcp::core::unexpected(command.error());
    }
    upstream.process_stdio.command = std::move(*command);
    auto args = optional_string_array(json, "args", path, options);
    if (!args) {
      return mcp::core::unexpected(args.error());
    }
    upstream.process_stdio.args = std::move(*args);
    auto cwd = optional_string(json, "cwd", path, options);
    if (!cwd) {
      return mcp::core::unexpected(cwd.error());
    }
    upstream.process_stdio.cwd = std::move(*cwd);
    auto env = optional_string_map(json, "env", path, options);
    if (!env) {
      return mcp::core::unexpected(env.error());
    }
    upstream.process_stdio.env = std::move(*env);
    if (json.contains("timeoutMs")) {
      if (!json.at("timeoutMs").is_number_integer() ||
          json.at("timeoutMs").get<std::int64_t>() <= 0) {
        return mcp::core::unexpected(make_gateway_config_error(
            "gateway config field must be a positive integer",
            path + ".timeoutMs"));
      }
      upstream.process_stdio.timeout =
          std::chrono::milliseconds{json.at("timeoutMs").get<std::int64_t>()};
    }
  } else if (*transport == "http" || *transport == "streamable_http") {
    upstream.transport = UpstreamTransportKind::streamable_http;
    auto uri = upstream.enabled ? require_string(json, "uri", path, options)
                                : optional_transport_string(json, "uri", path,
                                                            options);
    if (!uri) {
      return mcp::core::unexpected(uri.error());
    }
    upstream.streamable_http.uri = std::move(*uri);
    auto headers = optional_string_map(json, "headers", path, options);
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
  return gateway_config_from_json(json, GatewayConfigLoadOptions{});
}

core::Result<GatewayConfig> gateway_config_from_json(
    const protocol::Json& json, const GatewayConfigLoadOptions& options) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        make_gateway_config_error("gateway config root must be an object"));
  }
  auto endpoint_fields = reject_endpoint_fields(json);
  if (!endpoint_fields) {
    return mcp::core::unexpected(endpoint_fields.error());
  }

  GatewayConfig config;
  auto name = optional_string(json, "name", "$", options);
  if (!name) {
    return mcp::core::unexpected(name.error());
  }
  config.name = std::move(*name);
  if (config.name.empty()) {
    config.name = "cxxmcp-gateway";
  }
  auto version = optional_string(json, "version", "$", options);
  if (!version) {
    return mcp::core::unexpected(version.error());
  }
  config.version = std::move(*version);
  if (config.version.empty()) {
    config.version = "0.1.0";
  }

  if (const auto policy = json.find("policy");
      policy != json.end()) {
    if (!policy->is_object()) {
      return mcp::core::unexpected(make_gateway_config_error(
          "gateway config field must be an object", "$.policy"));
    }
    auto allow_tools =
        optional_string_array(*policy, "allowTools", "$.policy", options);
    if (!allow_tools) {
      return mcp::core::unexpected(allow_tools.error());
    }
    auto deny_tools =
        optional_string_array(*policy, "denyTools", "$.policy", options);
    if (!deny_tools) {
      return mcp::core::unexpected(deny_tools.error());
    }
    config.tool_policy.allow_tools = std::move(*allow_tools);
    config.tool_policy.deny_tools = std::move(*deny_tools);
    auto allow_resources =
        optional_string_array(*policy, "allowResources", "$.policy", options);
    if (!allow_resources) {
      return mcp::core::unexpected(allow_resources.error());
    }
    auto deny_resources =
        optional_string_array(*policy, "denyResources", "$.policy", options);
    if (!deny_resources) {
      return mcp::core::unexpected(deny_resources.error());
    }
    config.resource_policy.allow_resources = std::move(*allow_resources);
    config.resource_policy.deny_resources = std::move(*deny_resources);
    auto allow_prompts =
        optional_string_array(*policy, "allowPrompts", "$.policy", options);
    if (!allow_prompts) {
      return mcp::core::unexpected(allow_prompts.error());
    }
    auto deny_prompts =
        optional_string_array(*policy, "denyPrompts", "$.policy", options);
    if (!deny_prompts) {
      return mcp::core::unexpected(deny_prompts.error());
    }
    config.prompt_policy.allow_prompts = std::move(*allow_prompts);
    config.prompt_policy.deny_prompts = std::move(*deny_prompts);
  }

  if (!json.contains("upstreams") || !json.at("upstreams").is_array()) {
    return mcp::core::unexpected(make_gateway_config_error(
        "gateway config field must be an array", "upstreams"));
  }

  for (std::size_t i = 0; i < json.at("upstreams").size(); ++i) {
    auto upstream =
        upstream_from_json(json.at("upstreams").at(i), i, options);
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

core::Result<GatewayConfigDocument> gateway_config_document_from_json(
    const protocol::Json& json) {
  return gateway_config_document_from_json(json, GatewayConfigLoadOptions{});
}

core::Result<GatewayConfigDocument> gateway_config_document_from_json(
    const protocol::Json& json, const GatewayConfigLoadOptions& options) {
  auto config = gateway_config_from_json(json, options);
  if (!config) {
    return mcp::core::unexpected(config.error());
  }
  auto runtime = runtime_config_from_json(json, options);
  if (!runtime) {
    return mcp::core::unexpected(runtime.error());
  }
  return GatewayConfigDocument{std::move(*config), *runtime};
}

static core::Result<protocol::Json> parse_gateway_config_json_text(
    std::string_view text, std::string detail) {
  auto json = protocol::Json::parse(text.begin(), text.end(), nullptr, false);
  if (json.is_discarded()) {
    return mcp::core::unexpected(make_gateway_config_error(
        "failed to parse gateway config JSON", std::move(detail)));
  }
  return json;
}

core::Result<GatewayConfig> gateway_config_from_json_text(
    std::string_view text) {
  return gateway_config_from_json_text(text, GatewayConfigLoadOptions{});
}

core::Result<GatewayConfig> gateway_config_from_json_text(
    std::string_view text, const GatewayConfigLoadOptions& options) {
  auto json = parse_gateway_config_json_text(text, "JSON text");
  if (!json) {
    return mcp::core::unexpected(json.error());
  }
  return gateway_config_from_json(*json, options);
}

core::Result<GatewayConfigDocument> gateway_config_document_from_json_text(
    std::string_view text) {
  return gateway_config_document_from_json_text(text,
                                                GatewayConfigLoadOptions{});
}

core::Result<GatewayConfigDocument> gateway_config_document_from_json_text(
    std::string_view text, const GatewayConfigLoadOptions& options) {
  auto json = parse_gateway_config_json_text(text, "JSON text");
  if (!json) {
    return mcp::core::unexpected(json.error());
  }
  return gateway_config_document_from_json(*json, options);
}

core::Result<protocol::Json> load_gateway_config_json(std::string_view path) {
  std::ifstream input(std::string(path), std::ios::binary);
  if (!input) {
    return mcp::core::unexpected(make_gateway_config_error(
        "failed to open gateway config file", std::string(path)));
  }
  const std::string text{std::istreambuf_iterator<char>(input),
                         std::istreambuf_iterator<char>()};
  return parse_gateway_config_json_text(text, std::string(path));
}

core::Result<GatewayConfig> load_gateway_config_file(
    std::string_view path) {
  return load_gateway_config_file(path, GatewayConfigLoadOptions{});
}

core::Result<GatewayConfig> load_gateway_config_file(
    std::string_view path, const GatewayConfigLoadOptions& options) {
  auto json = load_gateway_config_json(path);
  if (!json) {
    return mcp::core::unexpected(json.error());
  }
  return gateway_config_from_json(*json, options);
}

core::Result<GatewayConfigDocument> load_gateway_config_document_file(
    std::string_view path) {
  return load_gateway_config_document_file(path, GatewayConfigLoadOptions{});
}

core::Result<GatewayConfigDocument> load_gateway_config_document_file(
    std::string_view path, const GatewayConfigLoadOptions& options) {
  auto json = load_gateway_config_json(path);
  if (!json) {
    return mcp::core::unexpected(json.error());
  }
  return gateway_config_document_from_json(*json, options);
}

}  // namespace mcp::gateway
