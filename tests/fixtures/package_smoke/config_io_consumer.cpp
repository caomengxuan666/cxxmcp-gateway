#include <cxxmcp/gateway/config_io.hpp>

int main() {
  const mcp::protocol::Json json = {
      {"upstreams",
       mcp::protocol::Json::array(
           {mcp::protocol::Json{{"id", "local"},
                                {"transport", "stdio"},
                                {"command", "server"}}})}};
  auto parsed = mcp::gateway::gateway_config_from_json(json);
  return parsed.has_value() ? 0 : 1;
}
