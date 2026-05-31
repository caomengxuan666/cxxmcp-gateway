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
if(NOT DEFINED CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME)
    set(CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME OFF)
endif()
if(NOT DEFINED CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO)
    set(CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO OFF)
endif()
if(NOT DEFINED CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI)
    set(CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI OFF)
endif()

set(component_install_prefix
    "${CXXMCP_GATEWAY_BINARY_DIR}/package_smoke_component_install_${CXXMCP_GATEWAY_GENERATOR_ID}")
set(component_consumer_build_dir
    "${CXXMCP_GATEWAY_BINARY_DIR}/package_smoke_component_install_consumer_${CXXMCP_GATEWAY_GENERATOR_ID}")

file(REMOVE_RECURSE
    "${component_install_prefix}"
    "${component_consumer_build_dir}")

function(install_gateway_component component)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --install "${CXXMCP_GATEWAY_BINARY_DIR}"
            --prefix "${component_install_prefix}"
            --component "${component}"
        RESULT_VARIABLE install_result)
    if(NOT install_result EQUAL 0)
        message(FATAL_ERROR "${component} component install failed")
    endif()
endfunction()

install_gateway_component(core)
if(CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME)
    install_gateway_component(runtime)
endif()
if(CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO)
    install_gateway_component(config_io)
endif()
if(CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI)
    install_gateway_component(cli)
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -G "${CXXMCP_GATEWAY_GENERATOR}"
        -S "${CXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR}"
        -B "${component_consumer_build_dir}"
        "-Dcxxmcp-gateway_DIR=${component_install_prefix}/${CXXMCP_GATEWAY_INSTALL_LIBDIR}/cmake/cxxmcp-gateway"
        "-Dcxxmcp_DIR=${cxxmcp_DIR}"
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME=${CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO=${CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI=${CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI}
    RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "component install package smoke configure failed")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${component_consumer_build_dir}"
    RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "component install package smoke build failed")
endif()
