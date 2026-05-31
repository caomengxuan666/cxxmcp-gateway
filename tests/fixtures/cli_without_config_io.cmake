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

set(cli_build_dir
    "${CXXMCP_GATEWAY_BINARY_DIR}/cli_without_config_io_${CXXMCP_GATEWAY_GENERATOR_ID}")
set(cli_output_dir "${cli_build_dir}/bin")
set(cli_config_args "")
if(DEFINED CXXMCP_GATEWAY_BUILD_TYPE AND NOT CXXMCP_GATEWAY_BUILD_TYPE STREQUAL "")
    list(APPEND cli_config_args
        "-DCMAKE_BUILD_TYPE=${CXXMCP_GATEWAY_BUILD_TYPE}")
endif()

file(REMOVE_RECURSE "${cli_build_dir}")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -G "${CXXMCP_GATEWAY_GENERATOR}"
        -S "${CXXMCP_GATEWAY_SOURCE_DIR}"
        -B "${cli_build_dir}"
        ${cli_config_args}
        "-Dcxxmcp_DIR=${cxxmcp_DIR}"
        "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${cli_output_dir}"
        -DCXXMCP_GATEWAY_BUILD_CONFIG_IO=OFF
        -DCXXMCP_GATEWAY_BUILD_RUNTIME=ON
        -DCXXMCP_GATEWAY_BUILD_CLI=ON
        -DCXXMCP_GATEWAY_BUILD_TESTS=OFF
        -DBUILD_TESTING=OFF
    RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "CLI-without-config_io configure failed")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${cli_build_dir}"
        --target cxxmcp_gateway_cli
    RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "CLI-without-config_io build failed")
endif()

if(WIN32)
    set(cli_path "${cli_output_dir}/cxxmcp-gateway.exe")
else()
    set(cli_path "${cli_output_dir}/cxxmcp-gateway")
endif()

execute_process(
    COMMAND "${cli_path}" serve
        --config "${CXXMCP_GATEWAY_SOURCE_DIR}/tests/fixtures/gateway_config.json"
        --upstream-stdio fixture=fixture-server
    RESULT_VARIABLE cli_result
    OUTPUT_VARIABLE cli_stdout
    ERROR_VARIABLE cli_stderr)
if(cli_result EQUAL 0)
    message(FATAL_ERROR
        "CLI accepted --config when config_io was disabled\n"
        "stdout: ${cli_stdout}\n"
        "stderr: ${cli_stderr}")
endif()
if(NOT cli_stderr MATCHES "--config requires cxxmcp_gateway_config_io")
    message(FATAL_ERROR
        "CLI without config_io did not report stable --config error\n"
        "stdout: ${cli_stdout}\n"
        "stderr: ${cli_stderr}")
endif()
