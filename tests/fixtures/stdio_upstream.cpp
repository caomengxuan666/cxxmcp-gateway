// Copyright (c) 2025 [caomengxuan666]

#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/peer.hpp"
#include "cxxmcp/protocol/resource.hpp"
#include "cxxmcp/protocol/types.hpp"
#include "cxxmcp/protocol/tool.hpp"
#include "cxxmcp/run.hpp"
#include "cxxmcp/server/authoring.hpp"

namespace {

class MarkerFile final {
 public:
  explicit MarkerFile(const char* path) : path_(path == nullptr ? "" : path) {
    if (path_.empty()) {
      return;
    }
    std::ofstream marker(path_, std::ios::binary);
    marker << "started\n";
  }

  ~MarkerFile() {
    if (!path_.empty()) {
      std::error_code ignored;
      std::filesystem::remove(path_, ignored);
    }
  }

  MarkerFile(const MarkerFile&) = delete;
  MarkerFile& operator=(const MarkerFile&) = delete;

 private:
  std::filesystem::path path_;
};

}  // namespace

int main(int argc, char** argv) {
  using Json = mcp::protocol::Json;
  using ToolResult = mcp::protocol::ToolResult;

  MarkerFile marker(std::getenv("CXXMCP_GATEWAY_STDIO_MARKER_FILE"));

  if (argc > 1 && std::string(argv[1]) == "--exit-immediately") {
    return 0;
  }

  if (argc > 1 && std::string(argv[1]) == "--malformed-response") {
    std::cout << "{not-json}\n";
    std::cout.flush();
    std::string ignored;
    (void)std::getline(std::cin, ignored);
    return 0;
  }

  return mcp::ServerPeer::builder()
      .name("cxxmcp-gateway-stdio-fixture")
      .version("1.0.0")
      .stdio()
      .tool(mcp::server::tool<Json, ToolResult>("echo")
                .title("Echo")
                .description("Echoes the provided value")
                .input_schema(Json{{"type", "object"},
                                   {"properties",
                                    Json{{"value",
                                          Json{{"type", "string"},
                                               {"description",
                                                "Value to echo"}}}}},
                                   {"additionalProperties", true}})
                .meta(Json{{"fixture", "stdio"}, {"preserve", true}})
                .handler([](const Json& input) {
                  return ToolResult::text(
                      input.value("value", std::string{}));
                }))
      .tool<Json, ToolResult>("slow", [](const Json& input) {
        std::this_thread::sleep_for(std::chrono::milliseconds{
            input.value("sleepMs", 500)});
        return ToolResult::text("slow-done");
      })
      .tool<Json, ToolResult>(
          "fail", [](const Json&) -> mcp::core::Result<ToolResult> {
            return mcp::core::unexpected(mcp::core::Error{
                static_cast<int>(mcp::protocol::ErrorCode::PermissionDenied),
                "fixture denied", "fixture detail", "fixture"});
          })
      .resource(mcp::protocol::Resource{
                    .title = "Fixture Readme",
                    .uri = "file:///fixture/readme.txt",
                    .name = "fixture-readme",
                    .description = "Fixture readme resource",
                    .mime_type = "text/plain",
                    .meta = Json{{"fixture", "stdio"}, {"preserve", true}},
                },
                [](const mcp::server::ResourceContext& context)
                    -> mcp::core::Result<mcp::protocol::ResourcesReadResult> {
                  mcp::protocol::ResourcesReadResult result;
                  result.contents.push_back(mcp::protocol::ResourceContents{
                      .uri = context.uri,
                      .mime_type = "text/plain",
                      .text = "hello from stdio resource",
                  });
                  return result;
                })
      .resource("file:///fixture/fail.txt",
                [](const mcp::server::ResourceContext&)
                    -> mcp::core::Result<mcp::protocol::ResourcesReadResult> {
                  return mcp::core::unexpected(mcp::core::Error{
                      static_cast<int>(
                          mcp::protocol::ErrorCode::PermissionDenied),
                      "resource denied", "resource detail", "fixture"});
                })
      .run();
}
