if(NOT DEFINED CXXMCP_GATEWAY_SOURCE_DIR)
    message(FATAL_ERROR "CXXMCP_GATEWAY_SOURCE_DIR is required")
endif()
if(NOT DEFINED CXXMCP_GATEWAY_BINARY_DIR)
    message(FATAL_ERROR "CXXMCP_GATEWAY_BINARY_DIR is required")
endif()
if(NOT DEFINED CXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR)
    message(FATAL_ERROR "CXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR is required")
endif()
if(NOT DEFINED cxxmcp_DIR)
    message(FATAL_ERROR "cxxmcp_DIR is required")
endif()
if(NOT DEFINED CXXMCP_GATEWAY_GENERATOR_ID)
    string(MAKE_C_IDENTIFIER "${CXXMCP_GATEWAY_GENERATOR}"
        CXXMCP_GATEWAY_GENERATOR_ID)
endif()
set(package_smoke_config_args "")
if(DEFINED CXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_TYPE AND
   CXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_TYPE)
    list(APPEND package_smoke_config_args
        -DCMAKE_BUILD_TYPE=${CXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_TYPE})
endif()
set(package_smoke_config_build_args "")
if(DEFINED CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG AND
   CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG)
    list(APPEND package_smoke_config_build_args
        --config "${CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG}")
endif()

set(core_only_build_dir
    "${CXXMCP_GATEWAY_BINARY_DIR}/package_smoke_core_only_gateway_${CXXMCP_GATEWAY_GENERATOR_ID}")
set(core_only_consumer_build_dir
    "${CXXMCP_GATEWAY_BINARY_DIR}/package_smoke_core_only_runtime_required_${CXXMCP_GATEWAY_GENERATOR_ID}")
set(core_only_install_prefix
    "${CXXMCP_GATEWAY_BINARY_DIR}/package_smoke_core_only_install_${CXXMCP_GATEWAY_GENERATOR_ID}")

file(REMOVE_RECURSE
    "${core_only_build_dir}"
    "${core_only_consumer_build_dir}"
    "${core_only_install_prefix}")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -G "${CXXMCP_GATEWAY_GENERATOR}"
        -S "${CXXMCP_GATEWAY_SOURCE_DIR}"
        -B "${core_only_build_dir}"
        ${package_smoke_config_args}
        "-Dcxxmcp_DIR=${cxxmcp_DIR}"
        -DCXXMCP_GATEWAY_BUILD_RUNTIME=OFF
        -DCXXMCP_GATEWAY_BUILD_CLI=OFF
        -DCXXMCP_GATEWAY_BUILD_TESTS=OFF
        -DBUILD_TESTING=OFF
    RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "core-only gateway configure failed")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${core_only_build_dir}"
        ${package_smoke_config_build_args}
    RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "core-only gateway build failed")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${core_only_build_dir}"
        --prefix "${core_only_install_prefix}"
        --component core
        ${package_smoke_config_build_args}
    RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "core-only gateway component install failed")
endif()

if(NOT EXISTS "${core_only_install_prefix}/include/cxxmcp/gateway.hpp")
    message(FATAL_ERROR "core component did not install gateway.hpp")
endif()
if(EXISTS "${core_only_install_prefix}/include/cxxmcp/gateway/runtime.hpp")
    message(FATAL_ERROR "core component installed runtime.hpp")
endif()
if(EXISTS "${core_only_install_prefix}/include/cxxmcp/gateway/config_io.hpp")
    message(FATAL_ERROR "core component installed config_io.hpp")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${CXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR}"
        -B "${core_only_consumer_build_dir}"
        "-Dcxxmcp-gateway_DIR=${core_only_build_dir}"
        "-Dcxxmcp_DIR=${cxxmcp_DIR}"
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME=ON
    RESULT_VARIABLE consumer_result
    OUTPUT_VARIABLE consumer_stdout
    ERROR_VARIABLE consumer_stderr)
if(consumer_result EQUAL 0)
    message(FATAL_ERROR
        "runtime component unexpectedly resolved against a core-only package")
endif()
if(NOT consumer_stdout MATCHES "runtime" AND
   NOT consumer_stderr MATCHES "runtime")
    message(FATAL_ERROR
        "runtime component failure did not mention runtime component")
endif()
