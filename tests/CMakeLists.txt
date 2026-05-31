add_executable(cxxmcp_gateway_router_tests
    router_tests.cpp
)
target_link_libraries(cxxmcp_gateway_router_tests PRIVATE cxxmcp_gateway_core)
cxxmcp_gateway_require_cxx23(cxxmcp_gateway_router_tests)

function(cxxmcp_gateway_prepend_target_dir_to_test test_name target_name)
    if(WIN32)
        set_tests_properties("${test_name}" PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:${target_name}>")
    endif()
endfunction()

add_test(NAME gateway_router COMMAND cxxmcp_gateway_router_tests)
cxxmcp_gateway_prepend_target_dir_to_test(gateway_router cxxmcp_gateway_core)

add_test(NAME gateway_source_dependency_guard
    COMMAND "${CMAKE_COMMAND}"
        -DCXXMCP_GATEWAY_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/source_dependency_guard.cmake")
add_test(NAME gateway_source_hygiene_guard_negative
    COMMAND "${CMAKE_COMMAND}"
        -DCXXMCP_GATEWAY_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DCXXMCP_GATEWAY_BINARY_DIR=${PROJECT_BINARY_DIR}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/source_hygiene_guard_negative.cmake")

if(TARGET cxxmcp_gateway_config_io)
    add_executable(cxxmcp_gateway_config_io_tests
        config_io_tests.cpp
    )
    target_link_libraries(cxxmcp_gateway_config_io_tests
        PRIVATE cxxmcp_gateway_config_io)
    target_compile_definitions(cxxmcp_gateway_config_io_tests PRIVATE
        CXXMCP_GATEWAY_CONFIG_IO_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/gateway_config.json"
        CXXMCP_GATEWAY_MALFORMED_CONFIG_IO_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/gateway_config_malformed.json"
    )
    cxxmcp_gateway_require_cxx23(cxxmcp_gateway_config_io_tests)

    add_test(NAME gateway_config_io COMMAND cxxmcp_gateway_config_io_tests)
    cxxmcp_gateway_prepend_target_dir_to_test(
        gateway_config_io cxxmcp_gateway_config_io)
endif()

if(TARGET cxxmcp_gateway_runtime)
    add_executable(cxxmcp_gateway_stdio_upstream_fixture
        fixtures/stdio_upstream.cpp
    )
    target_link_libraries(cxxmcp_gateway_stdio_upstream_fixture
        PRIVATE cxxmcp::sdk)
    cxxmcp_gateway_require_cxx23(cxxmcp_gateway_stdio_upstream_fixture)

    add_executable(cxxmcp_gateway_runtime_integration_tests
        runtime_integration_tests.cpp
    )
    target_link_libraries(cxxmcp_gateway_runtime_integration_tests
        PRIVATE cxxmcp_gateway_runtime)
    target_compile_definitions(cxxmcp_gateway_runtime_integration_tests PRIVATE
        CXXMCP_GATEWAY_STDIO_FIXTURE="$<TARGET_FILE:cxxmcp_gateway_stdio_upstream_fixture>"
    )
    add_dependencies(cxxmcp_gateway_runtime_integration_tests
        cxxmcp_gateway_stdio_upstream_fixture)
    cxxmcp_gateway_require_cxx23(cxxmcp_gateway_runtime_integration_tests)

    add_test(NAME gateway_runtime_integration
        COMMAND cxxmcp_gateway_runtime_integration_tests)
    cxxmcp_gateway_prepend_target_dir_to_test(
        gateway_runtime_integration cxxmcp_gateway_runtime)
endif()

if(TARGET cxxmcp_gateway_cli)
    add_test(NAME gateway_cli_no_args COMMAND cxxmcp_gateway_cli)
    cxxmcp_gateway_prepend_target_dir_to_test(
        gateway_cli_no_args cxxmcp_gateway_cli)
    set_tests_properties(gateway_cli_no_args PROPERTIES
        PASS_REGULAR_EXPRESSION "Usage:")

    add_test(NAME gateway_cli_help COMMAND cxxmcp_gateway_cli --help)
    cxxmcp_gateway_prepend_target_dir_to_test(
        gateway_cli_help cxxmcp_gateway_cli)
    set_tests_properties(gateway_cli_help PROPERTIES
        PASS_REGULAR_EXPRESSION "Usage:")

    add_test(NAME gateway_cli_version COMMAND cxxmcp_gateway_cli --version)
    cxxmcp_gateway_prepend_target_dir_to_test(
        gateway_cli_version cxxmcp_gateway_cli)
    set_tests_properties(gateway_cli_version PROPERTIES
        PASS_REGULAR_EXPRESSION "${PROJECT_VERSION}")

    add_test(NAME gateway_cli_invalid_args
        COMMAND "${CMAKE_COMMAND}"
            -DCXXMCP_GATEWAY_CLI=$<TARGET_FILE:cxxmcp_gateway_cli>
            -P "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cli_invalid_args.cmake")
    cxxmcp_gateway_prepend_target_dir_to_test(
        gateway_cli_invalid_args cxxmcp_gateway_cli)

    if(TARGET cxxmcp_gateway_config_io)
        add_test(NAME gateway_cli_config_missing
            COMMAND cxxmcp_gateway_cli serve
                --config "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/missing_config.json"
                --port 39999)
        cxxmcp_gateway_prepend_target_dir_to_test(
            gateway_cli_config_missing cxxmcp_gateway_cli)
        set_tests_properties(gateway_cli_config_missing PROPERTIES
            WILL_FAIL TRUE)

        add_test(NAME gateway_cli_config_and_flags_validate_together
            COMMAND "${CMAKE_COMMAND}"
                -DCXXMCP_GATEWAY_CLI=$<TARGET_FILE:cxxmcp_gateway_cli>
                -DCXXMCP_GATEWAY_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/gateway_config.json
                -P "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cli_duplicate_config_flag_upstream.cmake")
        cxxmcp_gateway_prepend_target_dir_to_test(
            gateway_cli_config_and_flags_validate_together cxxmcp_gateway_cli)
    endif()

    add_test(NAME gateway_cli_without_config_io_rejects_config
        COMMAND "${CMAKE_COMMAND}"
            -DCXXMCP_GATEWAY_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DCXXMCP_GATEWAY_BINARY_DIR=${PROJECT_BINARY_DIR}
            -Dcxxmcp_DIR=${cxxmcp_DIR}
            -DCXXMCP_GATEWAY_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCXXMCP_GATEWAY_GENERATOR=${CMAKE_GENERATOR}
            -DCXXMCP_GATEWAY_GENERATOR_ID=${CXXMCP_GATEWAY_GENERATOR_ID}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cli_without_config_io.cmake")
endif()

set(CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME OFF)
if(TARGET cxxmcp_gateway_runtime)
    set(CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME ON)
endif()
set(CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO OFF)
if(TARGET cxxmcp_gateway_config_io)
    set(CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO ON)
endif()
set(CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI OFF)
if(TARGET cxxmcp_gateway_cli)
    set(CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI ON)
endif()

string(MAKE_C_IDENTIFIER "${CMAKE_GENERATOR}" CXXMCP_GATEWAY_GENERATOR_ID)

set(CXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/package_smoke")
set(CXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_TREE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/package_smoke_build_tree_${CXXMCP_GATEWAY_GENERATOR_ID}")
set(CXXMCP_GATEWAY_PACKAGE_SMOKE_INSTALL_PREFIX
    "${CMAKE_CURRENT_BINARY_DIR}/package_smoke_install")
set(CXXMCP_GATEWAY_PACKAGE_SMOKE_INSTALL_BUILD_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/package_smoke_install_tree_${CXXMCP_GATEWAY_GENERATOR_ID}")
set(CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG_ARGS "")
if(CMAKE_BUILD_TYPE)
    list(APPEND CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})
endif()
if(CMAKE_CONFIGURATION_TYPES)
    set(CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG "$<CONFIG>")
elseif(CMAKE_BUILD_TYPE)
    set(CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG "${CMAKE_BUILD_TYPE}")
else()
    set(CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG "")
endif()
set(CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG_BUILD_ARGS "")
if(CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG)
    list(APPEND CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG_BUILD_ARGS
        --config ${CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG})
endif()
get_filename_component(CXXMCP_GATEWAY_CXXMCP_INSTALL_PREFIX
    "${cxxmcp_DIR}/../../.." ABSOLUTE)
set(CXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_TREE_PATH_DIRS
    "${PROJECT_BINARY_DIR};${PROJECT_BINARY_DIR}/${CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG};${CXXMCP_GATEWAY_CXXMCP_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR};${CXXMCP_GATEWAY_CXXMCP_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
set(CXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_TREE_CLI "")
if(TARGET cxxmcp_gateway_cli)
    set(CXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_TREE_CLI
        "$<TARGET_FILE:cxxmcp_gateway_cli>")
    list(APPEND CXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_TREE_PATH_DIRS
        "$<TARGET_FILE_DIR:cxxmcp_gateway_cli>")
endif()

add_test(NAME gateway_package_build_tree_configure
    COMMAND "${CMAKE_COMMAND}"
        -G "${CMAKE_GENERATOR}"
        -S "${CXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR}"
        -B "${CXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_TREE_DIR}"
        ${CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG_ARGS}
        -Dcxxmcp-gateway_DIR=${PROJECT_BINARY_DIR}
        -Dcxxmcp_DIR=${cxxmcp_DIR}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME=${CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO=${CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI=${CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI})
add_test(NAME gateway_package_build_tree_build
    COMMAND "${CMAKE_COMMAND}" --build
        "${CXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_TREE_DIR}"
        ${CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG_BUILD_ARGS})
set_tests_properties(gateway_package_build_tree_build PROPERTIES
    DEPENDS gateway_package_build_tree_configure)
add_test(NAME gateway_package_build_tree_run
    COMMAND "${CMAKE_COMMAND}"
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_DIR=${CXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_TREE_DIR}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME=${CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO=${CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI=${CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_EXPECTED_VERSION=${PROJECT_VERSION}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG=${CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG}
        "-DCXXMCP_GATEWAY_PACKAGE_SMOKE_PATH_DIRS=${CXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_TREE_PATH_DIRS}"
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_CLI=${CXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_TREE_CLI}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/package_smoke_run.cmake")
set_tests_properties(gateway_package_build_tree_run PROPERTIES
    DEPENDS gateway_package_build_tree_build)

add_test(NAME gateway_package_install_tree_install
    COMMAND "${CMAKE_COMMAND}" --install "${PROJECT_BINARY_DIR}"
        --prefix "${CXXMCP_GATEWAY_PACKAGE_SMOKE_INSTALL_PREFIX}"
        ${CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG_BUILD_ARGS})
add_test(NAME gateway_package_install_tree_configure
    COMMAND "${CMAKE_COMMAND}"
        -G "${CMAKE_GENERATOR}"
        -S "${CXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR}"
        -B "${CXXMCP_GATEWAY_PACKAGE_SMOKE_INSTALL_BUILD_DIR}"
        ${CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG_ARGS}
        -Dcxxmcp-gateway_DIR=${CXXMCP_GATEWAY_PACKAGE_SMOKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/cmake/cxxmcp-gateway
        -Dcxxmcp_DIR=${cxxmcp_DIR}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME=${CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO=${CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI=${CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI})
add_test(NAME gateway_package_install_tree_build
    COMMAND "${CMAKE_COMMAND}" --build
        "${CXXMCP_GATEWAY_PACKAGE_SMOKE_INSTALL_BUILD_DIR}"
        ${CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG_BUILD_ARGS})
set_tests_properties(gateway_package_install_tree_configure PROPERTIES
    DEPENDS gateway_package_install_tree_install)
set_tests_properties(gateway_package_install_tree_build PROPERTIES
    DEPENDS gateway_package_install_tree_configure)
if(WIN32)
    set(CXXMCP_GATEWAY_PACKAGE_SMOKE_CLI_SUFFIX ".exe")
else()
    set(CXXMCP_GATEWAY_PACKAGE_SMOKE_CLI_SUFFIX "")
endif()
add_test(NAME gateway_package_install_tree_run
    COMMAND "${CMAKE_COMMAND}"
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_DIR=${CXXMCP_GATEWAY_PACKAGE_SMOKE_INSTALL_BUILD_DIR}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME=${CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO=${CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI=${CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_EXPECTED_VERSION=${PROJECT_VERSION}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG=${CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG}
        "-DCXXMCP_GATEWAY_PACKAGE_SMOKE_PATH_DIRS=${CXXMCP_GATEWAY_PACKAGE_SMOKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR};${CXXMCP_GATEWAY_PACKAGE_SMOKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR};${CXXMCP_GATEWAY_CXXMCP_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR};${CXXMCP_GATEWAY_CXXMCP_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}"
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_CLI=${CXXMCP_GATEWAY_PACKAGE_SMOKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/cxxmcp-gateway${CXXMCP_GATEWAY_PACKAGE_SMOKE_CLI_SUFFIX}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/package_smoke_run.cmake")
set_tests_properties(gateway_package_install_tree_run PROPERTIES
    DEPENDS gateway_package_install_tree_build)

add_test(NAME gateway_package_core_only_runtime_component_fails
    COMMAND "${CMAKE_COMMAND}"
        -DCXXMCP_GATEWAY_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DCXXMCP_GATEWAY_BINARY_DIR=${PROJECT_BINARY_DIR}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR=${CXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR}
        -Dcxxmcp_DIR=${cxxmcp_DIR}
        -DCXXMCP_GATEWAY_GENERATOR=${CMAKE_GENERATOR}
        -DCXXMCP_GATEWAY_GENERATOR_ID=${CXXMCP_GATEWAY_GENERATOR_ID}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG=${CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/package_core_only_runtime_fail.cmake")

add_test(NAME gateway_package_config_io_component_fails
    COMMAND "${CMAKE_COMMAND}"
        -DCXXMCP_GATEWAY_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DCXXMCP_GATEWAY_BINARY_DIR=${PROJECT_BINARY_DIR}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR=${CXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR}
        -Dcxxmcp_DIR=${cxxmcp_DIR}
        -DCXXMCP_GATEWAY_GENERATOR=${CMAKE_GENERATOR}
        -DCXXMCP_GATEWAY_GENERATOR_ID=${CXXMCP_GATEWAY_GENERATOR_ID}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG=${CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/package_config_io_component_fail.cmake")

add_test(NAME gateway_package_cli_component_fails
    COMMAND "${CMAKE_COMMAND}"
        -DCXXMCP_GATEWAY_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DCXXMCP_GATEWAY_BINARY_DIR=${PROJECT_BINARY_DIR}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR=${CXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR}
        -Dcxxmcp_DIR=${cxxmcp_DIR}
        -DCXXMCP_GATEWAY_GENERATOR=${CMAKE_GENERATOR}
        -DCXXMCP_GATEWAY_GENERATOR_ID=${CXXMCP_GATEWAY_GENERATOR_ID}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG=${CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/package_cli_component_fail.cmake")

add_test(NAME gateway_package_component_install_smoke
    COMMAND "${CMAKE_COMMAND}"
        -DCXXMCP_GATEWAY_BINARY_DIR=${PROJECT_BINARY_DIR}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR=${CXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR}
        -Dcxxmcp_DIR=${cxxmcp_DIR}
        -DCXXMCP_GATEWAY_GENERATOR=${CMAKE_GENERATOR}
        -DCXXMCP_GATEWAY_GENERATOR_ID=${CXXMCP_GATEWAY_GENERATOR_ID}
        -DCXXMCP_GATEWAY_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
        -DCXXMCP_GATEWAY_INSTALL_BINDIR=${CMAKE_INSTALL_BINDIR}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG=${CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_EXPECTED_VERSION=${PROJECT_VERSION}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_RUN_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/package_smoke_run.cmake
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME=${CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO=${CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO}
        -DCXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI=${CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/package_component_install_smoke.cmake")

if(TARGET cxxmcp_gateway_runtime AND
   TARGET cxxmcp_gateway_config_io AND
   TARGET cxxmcp_gateway_cli)
    add_test(NAME gateway_package_cli_dependency_component_fails
        COMMAND "${CMAKE_COMMAND}"
            -DCXXMCP_GATEWAY_BINARY_DIR=${PROJECT_BINARY_DIR}
            -DCXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR=${CXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR}
            -Dcxxmcp_DIR=${cxxmcp_DIR}
            -DCXXMCP_GATEWAY_GENERATOR=${CMAKE_GENERATOR}
            -DCXXMCP_GATEWAY_GENERATOR_ID=${CXXMCP_GATEWAY_GENERATOR_ID}
            -DCXXMCP_GATEWAY_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
            -DCXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG=${CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/package_cli_dependency_component_fail.cmake")

    add_test(NAME gateway_package_full_build_core_component_fails
        COMMAND "${CMAKE_COMMAND}"
            -DCXXMCP_GATEWAY_BINARY_DIR=${PROJECT_BINARY_DIR}
            -DCXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR=${CXXMCP_GATEWAY_PACKAGE_SMOKE_SOURCE_DIR}
            -Dcxxmcp_DIR=${cxxmcp_DIR}
            -DCXXMCP_GATEWAY_GENERATOR=${CMAKE_GENERATOR}
            -DCXXMCP_GATEWAY_GENERATOR_ID=${CXXMCP_GATEWAY_GENERATOR_ID}
            -DCXXMCP_GATEWAY_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
            -DCXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG=${CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/package_full_build_core_component_fail.cmake")
endif()

add_test(NAME gateway_subproject_defaults
    COMMAND "${CMAKE_COMMAND}"
        -DCXXMCP_GATEWAY_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DCXXMCP_GATEWAY_BINARY_DIR=${PROJECT_BINARY_DIR}
        -Dcxxmcp_DIR=${cxxmcp_DIR}
        -DCXXMCP_GATEWAY_GENERATOR=${CMAKE_GENERATOR}
        -DCXXMCP_GATEWAY_GENERATOR_ID=${CXXMCP_GATEWAY_GENERATOR_ID}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/subproject_defaults.cmake")
