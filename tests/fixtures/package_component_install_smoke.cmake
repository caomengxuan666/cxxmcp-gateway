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
if(NOT DEFINED CXXMCP_GATEWAY_INSTALL_BINDIR)
    set(CXXMCP_GATEWAY_INSTALL_BINDIR bin)
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
if(NOT DEFINED CXXMCP_GATEWAY_PACKAGE_SMOKE_EXPECTED_VERSION)
    set(CXXMCP_GATEWAY_PACKAGE_SMOKE_EXPECTED_VERSION "")
endif()
if(NOT DEFINED CXXMCP_GATEWAY_PACKAGE_SMOKE_RUN_SCRIPT)
    get_filename_component(CXXMCP_GATEWAY_PACKAGE_SMOKE_RUN_SCRIPT
        "${CXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR}/../package_smoke_run.cmake"
        ABSOLUTE)
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
            ${package_smoke_config_build_args}
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
        ${package_smoke_config_args}
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
        ${package_smoke_config_build_args}
    RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "component install package smoke build failed")
endif()

get_filename_component(CXXMCP_GATEWAY_CXXMCP_INSTALL_PREFIX
    "${cxxmcp_DIR}/../../.." ABSOLUTE)
if(CMAKE_HOST_WIN32)
    set(CXXMCP_GATEWAY_PACKAGE_SMOKE_CLI_SUFFIX ".exe")
else()
    set(CXXMCP_GATEWAY_PACKAGE_SMOKE_CLI_SUFFIX "")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_DIR=${component_consumer_build_dir}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME=${CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO=${CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI=${CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_EXPECTED_VERSION=${CXXMCP_GATEWAY_PACKAGE_SMOKE_EXPECTED_VERSION}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG=${CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG}
        "-DCXXMCP_GATEWAY_PACKAGE_SMOKE_PATH_DIRS=${component_install_prefix}/${CXXMCP_GATEWAY_INSTALL_BINDIR};${component_install_prefix}/${CXXMCP_GATEWAY_INSTALL_LIBDIR};${CXXMCP_GATEWAY_CXXMCP_INSTALL_PREFIX}/${CXXMCP_GATEWAY_INSTALL_BINDIR};${CXXMCP_GATEWAY_CXXMCP_INSTALL_PREFIX}/${CXXMCP_GATEWAY_INSTALL_LIBDIR}"
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_CLI=${component_install_prefix}/${CXXMCP_GATEWAY_INSTALL_BINDIR}/cxxmcp-gateway${CXXMCP_GATEWAY_PACKAGE_SMOKE_CLI_SUFFIX}
        -P "${CXXMCP_GATEWAY_PACKAGE_SMOKE_RUN_SCRIPT}"
    RESULT_VARIABLE run_result)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "component install package smoke run failed")
endif()
