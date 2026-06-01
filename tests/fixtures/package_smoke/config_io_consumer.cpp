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
  return 0;
}
