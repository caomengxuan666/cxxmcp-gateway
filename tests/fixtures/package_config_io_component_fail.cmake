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

set(config_io_disabled_build_dir
    "${CXXMCP_GATEWAY_BINARY_DIR}/package_smoke_config_io_disabled_gateway_${CXXMCP_GATEWAY_GENERATOR_ID}")
set(config_io_consumer_build_dir
    "${CXXMCP_GATEWAY_BINARY_DIR}/package_smoke_config_io_disabled_required_${CXXMCP_GATEWAY_GENERATOR_ID}")

file(REMOVE_RECURSE
    "${config_io_disabled_build_dir}"
    "${config_io_consumer_build_dir}")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -G "${CXXMCP_GATEWAY_GENERATOR}"
        -S "${CXXMCP_GATEWAY_SOURCE_DIR}"
        -B "${config_io_disabled_build_dir}"
        "-Dcxxmcp_DIR=${cxxmcp_DIR}"
        -DCXXMCP_GATEWAY_BUILD_CONFIG_IO=OFF
        -DCXXMCP_GATEWAY_BUILD_RUNTIME=OFF
        -DCXXMCP_GATEWAY_BUILD_CLI=OFF
        -DCXXMCP_GATEWAY_BUILD_TESTS=OFF
        -DBUILD_TESTING=OFF
    RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "config_io-disabled gateway configure failed")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${config_io_disabled_build_dir}"
    RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "config_io-disabled gateway build failed")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -G "${CXXMCP_GATEWAY_GENERATOR}"
        -S "${CXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR}"
        -B "${config_io_consumer_build_dir}"
        "-Dcxxmcp-gateway_DIR=${config_io_disabled_build_dir}"
        "-Dcxxmcp_DIR=${cxxmcp_DIR}"
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO=ON
    RESULT_VARIABLE consumer_result
    OUTPUT_VARIABLE consumer_stdout
    ERROR_VARIABLE consumer_stderr)
if(consumer_result EQUAL 0)
    message(FATAL_ERROR
        "config_io component unexpectedly resolved when disabled")
endif()
if(NOT consumer_stdout MATCHES "config_io" AND
   NOT consumer_stderr MATCHES "config_io")
    message(FATAL_ERROR
        "config_io component failure did not mention config_io component")
endif()
