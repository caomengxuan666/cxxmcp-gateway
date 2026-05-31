#include <cxxmcp/gateway/config_io.hpp>

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
  return 0;
}
