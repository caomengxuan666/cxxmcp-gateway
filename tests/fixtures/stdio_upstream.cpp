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
#include "cxxmcp/protocol/completion.hpp"
#include "cxxmcp/protocol/prompt.hpp"
#include "cxxmcp/protocol/resource.hpp"
#include "cxxmcp/protocol/types.hpp"
#include "cxxmcp/protocol/tool.hpp"
#include "cxxmcp/run.hpp"
#include "cxxmcp/server/authoring.hpp"

namespace {

class MarkerFile final {
 public:
  MarkerFile(const char* path, const char* directory)
      : path_(path == nullptr ? "" : path) {
    const std::filesystem::path directory_path =
        directory == nullptr ? std::filesystem::path{} : directory;
    if (!directory_path.empty()) {
      std::error_code ignored;
      std::filesystem::create_directories(directory_path, ignored);
      for (int attempt = 0; attempt < 100; ++attempt) {
        const auto name =
            "started-" +
            std::to_string(std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count()) +
            "-" + std::to_string(attempt) + ".txt";
        const auto candidate = directory_path / name;
        if (std::filesystem::exists(candidate)) {
          continue;
        }
        path_ = candidate;
        break;
      }
    }
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

bool has_arg(int argc, char** argv, const std::string& name) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == name) {
      return true;
    }
  }
  return false;
}

std::string option_value(int argc, char** argv, const std::string& name) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == name) {
      return argv[i + 1];
    }
  }
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  using Json = mcp::protocol::Json;
  using ToolResult = mcp::protocol::ToolResult;

  MarkerFile marker(std::getenv("CXXMCP_GATEWAY_STDIO_MARKER_FILE"),
                    std::getenv("CXXMCP_GATEWAY_STDIO_MARKER_DIR"));

  const auto startup_delay = option_value(argc, argv, "--startup-delay-ms");
  if (!startup_delay.empty()) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds{std::stoi(startup_delay)});
  }

  if (has_arg(argc, argv, "--exit-immediately")) {
    return 0;
  }

  if (has_arg(argc, argv, "--malformed-response")) {
    std::cout << "{not-json}\n";
    std::cout.flush();
    std::string ignored;
    (void)std::getline(std::cin, ignored);
    return 0;
  }

  if (has_arg(argc, argv, "--tools-only")) {
    return mcp::ServerPeer::builder()
        .name("cxxmcp-gateway-tools-only-fixture")
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
        .run();
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
          "client-capabilities",
          [](const Json&, const mcp::server::ToolContext& context) {
            const auto client = context.client();
            return ToolResult::text(
                std::string{"roots="} +
                (client.supports_roots() ? "1" : "0") + ";sampling=" +
                (client.supports_sampling_tools() ? "1" : "0") +
                ";elicitation=" +
                (client.supports_elicitation() ? "1" : "0") + ";tasks=" +
                (client.supports_tasks() ? "1" : "0"));
          })
      .tool<Json, ToolResult>(
          "fail", [](const Json&) -> mcp::core::Result<ToolResult> {
            return mcp::core::unexpected(mcp::core::Error{
                static_cast<int>(mcp::protocol::ErrorCode::PermissionDenied),
                "fixture denied", "fixture detail", "fixture"});
          })
      .prompt(mcp::protocol::Prompt{
                  .title = "Fixture Summary",
                  .name = "summarize",
                  .description = "Summarize fixture text",
                  .arguments =
                      {
                          mcp::protocol::PromptArgument{
                              .name = "text",
                              .description = "Text to summarize",
                              .required = true,
                              .required_present = true,
                          },
                      },
                  .meta = Json{{"fixture", "stdio"}, {"preserve", true}},
              },
              [](const mcp::server::PromptContext& context) {
                mcp::protocol::PromptsGetResult result;
                result.description = "Summarize fixture text";
                result.messages.push_back(mcp::protocol::PromptMessage::text(
                    "user", "Summarize " +
                                context.arguments.value("text",
                                                        std::string{})));
                return result;
              })
      .prompt("fail-prompt",
              [](const mcp::server::PromptContext&)
                  -> mcp::core::Result<mcp::protocol::PromptsGetResult> {
                return mcp::core::unexpected(mcp::core::Error{
                    static_cast<int>(mcp::protocol::ErrorCode::PermissionDenied),
                    "prompt denied", "prompt detail", "fixture"});
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
      .resource_template(mcp::protocol::ResourceTemplate{
          .title = "Fixture File",
          .uri_template = "file:///fixture/{path}",
          .name = "fixture-file",
          .description = "Fixture file by path",
          .mime_type = "text/plain",
          .meta = Json{{"fixture", "stdio"}, {"preserve", true}},
      })
      .completion([](const mcp::protocol::CompleteParams& params,
                     const mcp::server::CompletionContext&) {
        mcp::protocol::CompleteResult result;
        if (params.ref.type == "ref/prompt" &&
            params.ref.name == "summarize" &&
            params.argument.name == "text") {
          result.completion.values = {
              params.argument.value + "-summary",
              params.argument.value + "-brief",
          };
          result.completion.total = 2;
          result.completion.has_more = false;
          return result;
        }
        if (params.ref.type == "ref/resource" &&
            params.ref.uri == "file:///fixture/{path}" &&
            params.argument.name == "path") {
          result.completion.values = {
              params.argument.value + "readme.txt",
              params.argument.value + "config.json",
          };
          result.completion.total = 2;
          result.completion.has_more = false;
          return result;
        }
        result.completion.values = {};
        result.completion.total = 0;
        result.completion.has_more = false;
        return result;
      })
      .run();
}
