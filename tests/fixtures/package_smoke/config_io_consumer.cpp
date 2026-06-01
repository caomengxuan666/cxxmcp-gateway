#include <cxxmcp/gateway/config_io.hpp>

#include <filesystem>
#include <fstream>

int main() {
  const mcp::protocol::Json json = {
      {"upstreams",
       mcp::protocol::Json::array(
           {mcp::protocol::Json{{"id", "local"},
                                {"transport", "stdio"},
                                {"command", "server"}}})}};
  auto parsed = mcp::gateway::gateway_config_from_json(json);
  if (!parsed.has_value()) {
    return 1;
  }
  auto invalid_config =
      mcp::gateway::gateway_config_from_json(mcp::protocol::Json::object());
  if (invalid_config.has_value() ||
      invalid_config.error().category != "gateway.config") {
    return 1;
  }

  const mcp::protocol::Json document_json = {
      {"runtime",
       mcp::protocol::Json{{"upstreamSessionMode", "persistent"},
                           {"persistentSessionPoolSize", 2},
                           {"persistentSessionAcquireTimeoutMs", 150},
                           {"activeCallDrainTimeoutMs", 5000},
                           {"prewarmCapabilities", true}}},
      {"upstreams",
       mcp::protocol::Json::array(
           {mcp::protocol::Json{{"id", "local"},
                                {"transport", "stdio"},
                                {"command", "server"}}})}};
  auto document =
      mcp::gateway::gateway_config_document_from_json(document_json);
  if (!document.has_value()) {
    return 1;
  }
  auto invalid_document = mcp::gateway::gateway_config_document_from_json(
      mcp::protocol::Json{{"runtime", "bad"}});
  if (invalid_document.has_value() ||
      invalid_document.error().category != "gateway.config") {
    return 1;
  }
  if (document->runtime.upstream_session_mode !=
          mcp::gateway::UpstreamSessionMode::persistent ||
      document->runtime.persistent_session_pool_size != 2 ||
      document->runtime.persistent_session_acquire_timeout.count() != 150 ||
      document->runtime.active_call_drain_timeout.count() != 5000 ||
      !document->runtime.prewarm_capabilities) {
    return 1;
  }

  const auto config_path =
      std::filesystem::temp_directory_path() /
      "cxxmcp_gateway_package_config_io_consumer.json";
  {
    std::ofstream out(config_path, std::ios::binary);
    out << json.dump();
  }
  auto loaded_config =
      mcp::gateway::load_gateway_config_file(config_path.string());
  if (!loaded_config.has_value() ||
      loaded_config->upstreams.size() != 1 ||
      loaded_config->upstreams.front().id != "local") {
    std::filesystem::remove(config_path);
    return 1;
  }
  auto missing_config = mcp::gateway::load_gateway_config_file(
      (config_path.parent_path() / "cxxmcp_gateway_package_missing.json")
          .string());
  if (missing_config.has_value() ||
      missing_config.error().category != "gateway.config") {
    std::filesystem::remove(config_path);
    return 1;
  }
  {
    std::ofstream out(config_path, std::ios::binary | std::ios::trunc);
    out << document_json.dump();
  }
  auto loaded =
      mcp::gateway::load_gateway_config_document_file(config_path.string());
  std::filesystem::remove(config_path);
  if (!loaded.has_value() ||
      loaded->config.upstreams.size() != 1 ||
      loaded->config.upstreams.front().id != "local" ||
      loaded->runtime.upstream_session_mode !=
          mcp::gateway::UpstreamSessionMode::persistent ||
      loaded->runtime.active_call_drain_timeout.count() != 5000) {
    return 1;
  }
  auto missing_document = mcp::gateway::load_gateway_config_document_file(
      (config_path.parent_path() / "cxxmcp_gateway_package_missing.json")
          .string());
  if (missing_document.has_value() ||
      missing_document.error().category != "gateway.config") {
    return 1;
  }
  return 0;
}
