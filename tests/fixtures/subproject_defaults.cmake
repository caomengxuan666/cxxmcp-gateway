if(NOT DEFINED CXXMCP_GATEWAY_SOURCE_DIR)
    message(FATAL_ERROR "CXXMCP_GATEWAY_SOURCE_DIR is required")
endif()
if(NOT DEFINED CXXMCP_GATEWAY_BINARY_DIR)
    message(FATAL_ERROR "CXXMCP_GATEWAY_BINARY_DIR is required")
endif()
if(NOT DEFINED cxxmcp_DIR)
    message(FATAL_ERROR "cxxmcp_DIR is required")
endif()
if(NOT DEFINED CXXMCP_GATEWAY_GENERATOR_ID)
    string(MAKE_C_IDENTIFIER "${CXXMCP_GATEWAY_GENERATOR}"
        CXXMCP_GATEWAY_GENERATOR_ID)
endif()

set(subproject_source_dir
    "${CXXMCP_GATEWAY_BINARY_DIR}/subproject_defaults_src_${CXXMCP_GATEWAY_GENERATOR_ID}")
set(subproject_build_dir
    "${CXXMCP_GATEWAY_BINARY_DIR}/subproject_defaults_build_${CXXMCP_GATEWAY_GENERATOR_ID}")

file(REMOVE_RECURSE "${subproject_source_dir}" "${subproject_build_dir}")
file(MAKE_DIRECTORY "${subproject_source_dir}")

file(WRITE "${subproject_source_dir}/CMakeLists.txt"
"cmake_minimum_required(VERSION 3.23)
project(cxxmcp_gateway_subproject_defaults LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(\"${CXXMCP_GATEWAY_SOURCE_DIR}\" gateway)

if(NOT TARGET cxxmcp_gateway_core)
    message(FATAL_ERROR \"subproject should always build core by default\")
endif()
if(NOT TARGET cxxmcp_gateway_runtime)
    message(FATAL_ERROR \"subproject should build runtime by default\")
endif()
if(TARGET cxxmcp_gateway_cli)
    message(FATAL_ERROR \"subproject should not build CLI by default\")
endif()
if(TARGET cxxmcp_gateway_config_io)
    message(FATAL_ERROR \"subproject should not build config_io by default\")
endif()
if(TARGET cxxmcp_gateway_router_tests OR
   TARGET cxxmcp_gateway_runtime_integration_tests OR
   TARGET cxxmcp_gateway_config_io_tests)
    message(FATAL_ERROR \"subproject should not build tests by default\")
endif()

add_executable(cxxmcp_gateway_subproject_consumer main.cpp)
target_link_libraries(cxxmcp_gateway_subproject_consumer
    PRIVATE cxxmcp_gateway_core)
target_compile_features(cxxmcp_gateway_subproject_consumer PRIVATE cxx_std_23)
")

file(WRITE "${subproject_source_dir}/main.cpp"
"#include \"cxxmcp/gateway.hpp\"

int main() {
  mcp::gateway::GatewayConfig config;
  mcp::gateway::UpstreamServer upstream;
  upstream.id = \"local\";
  upstream.process_stdio.command = \"server\";
  config.upstreams.push_back(upstream);
  auto valid = mcp::gateway::validate_gateway_config(config);
  return valid.has_value() ? 0 : 1;
}
")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -G "${CXXMCP_GATEWAY_GENERATOR}"
        -S "${subproject_source_dir}"
        -B "${subproject_build_dir}"
        "-Dcxxmcp_DIR=${cxxmcp_DIR}"
    RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "subproject default configure failed")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${subproject_build_dir}"
    RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "subproject default build failed")
endif()
