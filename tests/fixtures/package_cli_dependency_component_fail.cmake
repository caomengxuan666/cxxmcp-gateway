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
if(NOT DEFINED CXXMCP_GATEWAY_INSTALL_LIBDIR)
    set(CXXMCP_GATEWAY_INSTALL_LIBDIR lib)
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

set(cli_dependency_install_prefix
    "${CXXMCP_GATEWAY_BINARY_DIR}/package_smoke_cli_dependency_install_${CXXMCP_GATEWAY_GENERATOR_ID}")
set(cli_dependency_consumer_dir
    "${CXXMCP_GATEWAY_BINARY_DIR}/package_smoke_cli_dependency_required_${CXXMCP_GATEWAY_GENERATOR_ID}")
file(REMOVE_RECURSE
    "${cli_dependency_install_prefix}"
    "${cli_dependency_consumer_dir}")

foreach(component IN ITEMS core cli)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --install "${CXXMCP_GATEWAY_BINARY_DIR}"
            --prefix "${cli_dependency_install_prefix}"
            --component "${component}"
            ${package_smoke_config_build_args}
        RESULT_VARIABLE install_result)
    if(NOT install_result EQUAL 0)
        message(FATAL_ERROR "${component} component install failed")
    endif()
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -G "${CXXMCP_GATEWAY_GENERATOR}"
        -S "${CXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR}"
        -B "${cli_dependency_consumer_dir}"
        ${package_smoke_config_args}
        "-Dcxxmcp-gateway_DIR=${cli_dependency_install_prefix}/${CXXMCP_GATEWAY_INSTALL_LIBDIR}/cmake/cxxmcp-gateway"
        "-Dcxxmcp_DIR=${cxxmcp_DIR}"
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI=ON
    RESULT_VARIABLE consumer_result
    OUTPUT_VARIABLE consumer_stdout
    ERROR_VARIABLE consumer_stderr)
if(consumer_result EQUAL 0)
    message(FATAL_ERROR
        "cli component unexpectedly resolved without runtime dependency")
endif()
if(NOT consumer_stdout MATCHES "cli" AND
   NOT consumer_stderr MATCHES "cli")
    message(FATAL_ERROR
        "cli dependency failure did not mention cli component")
endif()
