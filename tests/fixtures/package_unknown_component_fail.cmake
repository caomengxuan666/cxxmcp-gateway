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

set(unknown_component_source_dir
    "${CXXMCP_GATEWAY_BINARY_DIR}/package_smoke_unknown_component_src_${CXXMCP_GATEWAY_GENERATOR_ID}")
set(unknown_component_build_dir
    "${CXXMCP_GATEWAY_BINARY_DIR}/package_smoke_unknown_component_build_${CXXMCP_GATEWAY_GENERATOR_ID}")

file(REMOVE_RECURSE
    "${unknown_component_source_dir}"
    "${unknown_component_build_dir}")
file(MAKE_DIRECTORY "${unknown_component_source_dir}")
file(WRITE "${unknown_component_source_dir}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.23)
project(cxxmcp_gateway_unknown_component_smoke LANGUAGES CXX)

find_package(cxxmcp-gateway CONFIG REQUIRED
    COMPONENTS definitely_missing_component)
]=])

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -G "${CXXMCP_GATEWAY_GENERATOR}"
        -S "${unknown_component_source_dir}"
        -B "${unknown_component_build_dir}"
        "-Dcxxmcp-gateway_DIR=${CXXMCP_GATEWAY_BINARY_DIR}"
        "-Dcxxmcp_DIR=${cxxmcp_DIR}"
    RESULT_VARIABLE consumer_result
    OUTPUT_VARIABLE consumer_stdout
    ERROR_VARIABLE consumer_stderr)
if(consumer_result EQUAL 0)
    message(FATAL_ERROR
        "unknown component unexpectedly resolved")
endif()
if(NOT consumer_stdout MATCHES "definitely_missing_component" AND
   NOT consumer_stderr MATCHES "definitely_missing_component")
    message(FATAL_ERROR
        "unknown component failure did not mention requested component")
endif()
