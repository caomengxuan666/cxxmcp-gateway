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

set(cli_disabled_build_dir
    "${CXXMCP_GATEWAY_BINARY_DIR}/package_smoke_cli_disabled_gateway_${CXXMCP_GATEWAY_GENERATOR_ID}")
set(cli_consumer_build_dir
    "${CXXMCP_GATEWAY_BINARY_DIR}/package_smoke_cli_disabled_required_${CXXMCP_GATEWAY_GENERATOR_ID}")

file(REMOVE_RECURSE
    "${cli_disabled_build_dir}"
    "${cli_consumer_build_dir}")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -G "${CXXMCP_GATEWAY_GENERATOR}"
        -S "${CXXMCP_GATEWAY_SOURCE_DIR}"
        -B "${cli_disabled_build_dir}"
        ${package_smoke_config_args}
        "-Dcxxmcp_DIR=${cxxmcp_DIR}"
        -DCXXMCP_GATEWAY_BUILD_CLI=OFF
        -DCXXMCP_GATEWAY_BUILD_TESTS=OFF
        -DBUILD_TESTING=OFF
    RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "cli-disabled gateway configure failed")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${cli_disabled_build_dir}"
        ${package_smoke_config_build_args}
    RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "cli-disabled gateway build failed")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -G "${CXXMCP_GATEWAY_GENERATOR}"
        -S "${CXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR}"
        -B "${cli_consumer_build_dir}"
        "-Dcxxmcp-gateway_DIR=${cli_disabled_build_dir}"
        "-Dcxxmcp_DIR=${cxxmcp_DIR}"
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI=ON
    RESULT_VARIABLE consumer_result
    OUTPUT_VARIABLE consumer_stdout
    ERROR_VARIABLE consumer_stderr)
if(consumer_result EQUAL 0)
    message(FATAL_ERROR
        "cli component unexpectedly resolved when disabled")
endif()
if(NOT consumer_stdout MATCHES "cli" AND
   NOT consumer_stderr MATCHES "cli")
    message(FATAL_ERROR
        "cli component failure did not mention cli component")
endif()
