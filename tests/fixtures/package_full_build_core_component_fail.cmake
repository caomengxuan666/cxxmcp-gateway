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

set(core_component_install_prefix
    "${CXXMCP_GATEWAY_BINARY_DIR}/package_smoke_full_build_core_component_install_${CXXMCP_GATEWAY_GENERATOR_ID}")

file(REMOVE_RECURSE "${core_component_install_prefix}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${CXXMCP_GATEWAY_BINARY_DIR}"
        --prefix "${core_component_install_prefix}"
        --component core
        ${package_smoke_config_build_args}
    RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "full-build core component install failed")
endif()

set(package_config_dir
    "${core_component_install_prefix}/${CXXMCP_GATEWAY_INSTALL_LIBDIR}/cmake/cxxmcp-gateway")

set(core_consumer_build_dir
    "${CXXMCP_GATEWAY_BINARY_DIR}/package_smoke_full_build_core_component_consumer_${CXXMCP_GATEWAY_GENERATOR_ID}")
file(REMOVE_RECURSE "${core_consumer_build_dir}")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -G "${CXXMCP_GATEWAY_GENERATOR}"
        -S "${CXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR}"
        -B "${core_consumer_build_dir}"
        ${package_smoke_config_args}
        "-Dcxxmcp-gateway_DIR=${package_config_dir}"
        "-Dcxxmcp_DIR=${cxxmcp_DIR}"
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME=OFF
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO=OFF
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI=OFF
    RESULT_VARIABLE core_consumer_configure_result)
if(NOT core_consumer_configure_result EQUAL 0)
    message(FATAL_ERROR "core-only component package smoke configure failed")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${core_consumer_build_dir}"
        ${package_smoke_config_build_args}
    RESULT_VARIABLE core_consumer_build_result)
if(NOT core_consumer_build_result EQUAL 0)
    message(FATAL_ERROR "core-only component package smoke build failed")
endif()

get_filename_component(CXXMCP_GATEWAY_CXXMCP_INSTALL_PREFIX
    "${cxxmcp_DIR}/../../.." ABSOLUTE)
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_DIR=${core_consumer_build_dir}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME=OFF
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO=OFF
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI=OFF
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG=${CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG}
        "-DCXXMCP_GATEWAY_PACKAGE_SMOKE_PATH_DIRS=${core_component_install_prefix}/${CXXMCP_GATEWAY_INSTALL_BINDIR};${core_component_install_prefix}/${CXXMCP_GATEWAY_INSTALL_LIBDIR};${CXXMCP_GATEWAY_CXXMCP_INSTALL_PREFIX}/${CXXMCP_GATEWAY_INSTALL_BINDIR};${CXXMCP_GATEWAY_CXXMCP_INSTALL_PREFIX}/${CXXMCP_GATEWAY_INSTALL_LIBDIR}"
        -P "${CXXMCP_GATEWAY_PACKAGE_SMOKE_RUN_SCRIPT}"
    RESULT_VARIABLE core_consumer_run_result)
if(NOT core_consumer_run_result EQUAL 0)
    message(FATAL_ERROR "core-only component package smoke run failed")
endif()

if(EXISTS "${package_config_dir}/cxxmcp-gatewayRuntimeTargets.cmake")
    message(FATAL_ERROR "core component installed runtime targets file")
endif()
if(EXISTS "${package_config_dir}/cxxmcp-gatewayConfigIoTargets.cmake")
    message(FATAL_ERROR "core component installed config_io targets file")
endif()
if(EXISTS "${package_config_dir}/cxxmcp-gatewayCliTargets.cmake")
    message(FATAL_ERROR "core component installed cli targets file")
endif()

if(EXISTS "${core_component_install_prefix}/include/cxxmcp/gateway/runtime.hpp")
    message(FATAL_ERROR "core component installed runtime.hpp")
endif()
if(EXISTS "${core_component_install_prefix}/include/cxxmcp/gateway/config_io.hpp")
    message(FATAL_ERROR "core component installed config_io.hpp")
endif()

function(expect_component_failure component cache_arg)
    set(consumer_build_dir
        "${CXXMCP_GATEWAY_BINARY_DIR}/package_smoke_full_build_core_component_${component}_required_${CXXMCP_GATEWAY_GENERATOR_ID}")
    file(REMOVE_RECURSE "${consumer_build_dir}")

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -G "${CXXMCP_GATEWAY_GENERATOR}"
            -S "${CXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR}"
            -B "${consumer_build_dir}"
            "-Dcxxmcp-gateway_DIR=${package_config_dir}"
            "-Dcxxmcp_DIR=${cxxmcp_DIR}"
            "${cache_arg}=ON"
        RESULT_VARIABLE consumer_result
        OUTPUT_VARIABLE consumer_stdout
        ERROR_VARIABLE consumer_stderr)
    if(consumer_result EQUAL 0)
        message(FATAL_ERROR
            "${component} component unexpectedly resolved from core-only component install")
    endif()
    if(NOT consumer_stdout MATCHES "${component}" AND
       NOT consumer_stderr MATCHES "${component}")
        message(FATAL_ERROR
            "${component} component failure did not mention ${component}")
    endif()
endfunction()

expect_component_failure(runtime
    -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME)
expect_component_failure(config_io
    -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO)
expect_component_failure(cli
    -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI)
