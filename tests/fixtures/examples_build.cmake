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
