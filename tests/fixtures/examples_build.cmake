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

set(examples_build_dir
    "${CXXMCP_GATEWAY_BINARY_DIR}/examples_build_${CXXMCP_GATEWAY_GENERATOR_ID}")
set(examples_output_dir "${examples_build_dir}/bin")
set(examples_config_args "")
if(DEFINED CXXMCP_GATEWAY_BUILD_TYPE AND NOT CXXMCP_GATEWAY_BUILD_TYPE STREQUAL "")
    list(APPEND examples_config_args
        "-DCMAKE_BUILD_TYPE=${CXXMCP_GATEWAY_BUILD_TYPE}")
endif()

file(REMOVE_RECURSE "${examples_build_dir}")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -G "${CXXMCP_GATEWAY_GENERATOR}"
        -S "${CXXMCP_GATEWAY_SOURCE_DIR}"
        -B "${examples_build_dir}"
        ${examples_config_args}
        "-Dcxxmcp_DIR=${cxxmcp_DIR}"
        "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${examples_output_dir}"
        -DCXXMCP_GATEWAY_BUILD_EXAMPLES=ON
        -DCXXMCP_GATEWAY_BUILD_RUNTIME=ON
        -DCXXMCP_GATEWAY_BUILD_CLI=OFF
        -DCXXMCP_GATEWAY_BUILD_CONFIG_IO=OFF
        -DCXXMCP_GATEWAY_BUILD_TESTS=OFF
        -DBUILD_TESTING=OFF
    RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "examples configure failed")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${examples_build_dir}"
        --target cxxmcp_gateway_embedded_runtime_example
    RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "embedded runtime example build failed")
endif()

if(WIN32)
    set(example_suffix ".exe")
else()
    set(example_suffix "")
endif()
set(example_path
    "${examples_output_dir}/cxxmcp-gateway-embedded-runtime-example${example_suffix}")
if(NOT EXISTS "${example_path}")
    file(GLOB_RECURSE example_candidates
        "${examples_output_dir}/cxxmcp-gateway-embedded-runtime-example${example_suffix}")
    list(LENGTH example_candidates example_candidate_count)
    if(example_candidate_count EQUAL 0)
        message(FATAL_ERROR "embedded runtime example executable not found")
    endif()
    list(GET example_candidates 0 example_path)
endif()

execute_process(
    COMMAND "${example_path}" --help
    RESULT_VARIABLE help_result
    OUTPUT_VARIABLE help_stdout
    ERROR_VARIABLE help_stderr)
if(NOT help_result EQUAL 0)
    message(FATAL_ERROR
        "embedded runtime example help failed\n"
        "stdout: ${help_stdout}\n"
        "stderr: ${help_stderr}")
endif()
if(NOT help_stdout MATCHES "--persistent" OR
   NOT help_stdout MATCHES "--session-pool-size" OR
   NOT help_stdout MATCHES "--prewarm")
    message(FATAL_ERROR
        "embedded runtime example help did not advertise runtime options\n"
        "stdout: ${help_stdout}\n"
        "stderr: ${help_stderr}")
endif()
