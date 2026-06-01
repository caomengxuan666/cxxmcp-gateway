// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/gateway/error.hpp"

#include <string>
#include <utility>

namespace mcp::gateway {

core::Error make_gateway_error(protocol::ErrorCode code, std::string message,
                               std::string detail) {
  return core::Error{static_cast<int>(code), std::move(message),
                     std::move(detail), "gateway"};
}

core::Error make_gateway_config_error(std::string message,
                                      std::string detail) {
  return core::Error{static_cast<int>(protocol::ErrorCode::InvalidParams),
                     std::move(message), std::move(detail),
                     "gateway.config"};
}

core::Error annotate_gateway_upstream_error(core::Error error,
                                            std::string_view upstream_id) {
  const auto looks_like_timeout =
      error.message.find("timed out") != std::string::npos ||
      error.message.find("timeout") != std::string::npos ||
      error.detail.find("timed out") != std::string::npos ||
      error.detail.find("timeout") != std::string::npos;
  if (error.category.rfind("gateway.", 0) != 0 && looks_like_timeout) {
    error.category = "timeout";
  }

  const auto prefix = "upstream '" + std::string(upstream_id) + "'";
  if (error.detail.empty()) {
    error.detail = prefix;
  } else {
    error.detail = prefix + ": " + error.detail;
  }
  if (error.category.empty()) {
    error.category = "gateway.upstream";
  } else if (error.category.rfind("gateway.", 0) != 0) {
    error.category = "gateway.upstream." + error.category;
  }
  return error;
}

}  // namespace mcp::gateway
