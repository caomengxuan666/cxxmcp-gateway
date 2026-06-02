cmake_minimum_required(VERSION 3.23)

project(cxxmcp-gateway
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_EXTENSIONS OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

include(CTest)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

find_package(Threads REQUIRED)
find_package(cxxmcp CONFIG REQUIRED)

function(cxxmcp_gateway_require_cxx23 target_name)
    target_compile_features("${target_name}" PUBLIC cxx_std_23)
endfunction()

set(CXXMCP_GATEWAY_MSVC_RUNTIME_LIBRARY "" CACHE STRING
    "MSVC runtime library for cxxmcp-gateway compiled targets; empty keeps the toolchain default")
set_property(CACHE CXXMCP_GATEWAY_MSVC_RUNTIME_LIBRARY PROPERTY STRINGS
    ""
    "MultiThreaded"
    "MultiThreadedDebug"
    "MultiThreadedDLL"
    "MultiThreadedDebugDLL"
    "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

if(MSVC AND CXXMCP_GATEWAY_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "${CXXMCP_GATEWAY_MSVC_RUNTIME_LIBRARY}")
endif()

if(MSVC)
    add_compile_options(/bigobj)
elseif(MINGW)
    add_compile_options(-Wa,-mbig-obj)
endif()

option(CXXMCP_GATEWAY_BUILD_RUNTIME
    "Build the gateway runtime library" ON)

if(PROJECT_IS_TOP_LEVEL)
    set(CXXMCP_GATEWAY_DEFAULT_CLI ON)
    set(CXXMCP_GATEWAY_DEFAULT_CONFIG_IO ON)
else()
    set(CXXMCP_GATEWAY_DEFAULT_CLI OFF)
    set(CXXMCP_GATEWAY_DEFAULT_CONFIG_IO OFF)
endif()
if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
    set(CXXMCP_GATEWAY_DEFAULT_TESTS ON)
else()
    set(CXXMCP_GATEWAY_DEFAULT_TESTS OFF)
endif()

option(CXXMCP_GATEWAY_BUILD_CLI
    "Build the cxxmcp-gateway command-line executable"
    ${CXXMCP_GATEWAY_DEFAULT_CLI})
option(CXXMCP_GATEWAY_BUILD_CONFIG_IO
    "Build the optional gateway config file loading library"
    ${CXXMCP_GATEWAY_DEFAULT_CONFIG_IO})
option(CXXMCP_GATEWAY_BUILD_TESTS
    "Build gateway tests" ${CXXMCP_GATEWAY_DEFAULT_TESTS})
option(CXXMCP_GATEWAY_BUILD_EXAMPLES
    "Build optional gateway embedding examples" OFF)
option(CXXMCP_GATEWAY_BUILD_PERF
    "Build optional gateway performance measurement tools" OFF)
option(CXXMCP_GATEWAY_BUILD_GATEWAYD_MVP
    "Build an experimental gatewayd feasibility prototype" OFF)
set(CXXMCP_GATEWAY_CXXMCP_EXAMPLES_SOURCE_DIR
    ""
    CACHE PATH "Optional explicit local cxxmcp-examples source tree for the dev-tool gateway example; empty fetches from Git")
set(CXXMCP_GATEWAY_CXXMCP_EXAMPLES_GIT_REPOSITORY
    "https://github.com/caomengxuan666/cxxmcp-examples.git"
    CACHE STRING "Git repository for the optional cxxmcp-examples dev-tool gateway demo")
set(CXXMCP_GATEWAY_CXXMCP_EXAMPLES_GIT_TAG
    "main"
    CACHE STRING "Git tag, branch, or commit for the optional cxxmcp-examples dev-tool gateway demo")
option(CXXMCP_GATEWAY_BUILD_CXXMCP_EXAMPLES_DEV_TOOL_GATEWAY
    "Build an optional gateway example that superbuilds cxxmcp-examples upstream servers"
    OFF)

if(CXXMCP_GATEWAY_BUILD_CLI AND NOT CXXMCP_GATEWAY_BUILD_RUNTIME)
    message(FATAL_ERROR
        "CXXMCP_GATEWAY_BUILD_CLI requires CXXMCP_GATEWAY_BUILD_RUNTIME")
endif()
if(CXXMCP_GATEWAY_BUILD_EXAMPLES AND NOT CXXMCP_GATEWAY_BUILD_RUNTIME)
    message(FATAL_ERROR
        "CXXMCP_GATEWAY_BUILD_EXAMPLES requires CXXMCP_GATEWAY_BUILD_RUNTIME")
endif()
if(CXXMCP_GATEWAY_BUILD_PERF AND NOT CXXMCP_GATEWAY_BUILD_RUNTIME)
    message(FATAL_ERROR
        "CXXMCP_GATEWAY_BUILD_PERF requires CXXMCP_GATEWAY_BUILD_RUNTIME")
endif()
if(CXXMCP_GATEWAY_BUILD_CXXMCP_EXAMPLES_DEV_TOOL_GATEWAY AND
   NOT CXXMCP_GATEWAY_BUILD_RUNTIME)
    message(FATAL_ERROR
        "CXXMCP_GATEWAY_BUILD_CXXMCP_EXAMPLES_DEV_TOOL_GATEWAY requires CXXMCP_GATEWAY_BUILD_RUNTIME")
endif()
if(CXXMCP_GATEWAY_BUILD_GATEWAYD_MVP AND
   (NOT CXXMCP_GATEWAY_BUILD_RUNTIME OR NOT CXXMCP_GATEWAY_BUILD_CONFIG_IO))
    message(FATAL_ERROR
        "CXXMCP_GATEWAY_BUILD_GATEWAYD_MVP requires CXXMCP_GATEWAY_BUILD_RUNTIME and CXXMCP_GATEWAY_BUILD_CONFIG_IO")
endif()

add_library(cxxmcp_gateway_core
    gateway/src/catalog.cpp
    gateway/src/error.cpp
    gateway/src/router.cpp
)
set_target_properties(cxxmcp_gateway_core PROPERTIES EXPORT_NAME core)
set_target_properties(cxxmcp_gateway_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(cxxmcp_gateway_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/gateway/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(cxxmcp_gateway_core PUBLIC cxxmcp::sdk)
cxxmcp_gateway_require_cxx23(cxxmcp_gateway_core)
add_library(cxxmcp-gateway::core ALIAS cxxmcp_gateway_core)

if(CXXMCP_GATEWAY_BUILD_RUNTIME)
    add_library(cxxmcp_gateway_runtime
        gateway/src/runtime.cpp
    )
    set_target_properties(cxxmcp_gateway_runtime PROPERTIES
        EXPORT_NAME runtime
        POSITION_INDEPENDENT_CODE ON
    )
    target_include_directories(cxxmcp_gateway_runtime PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/gateway/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(cxxmcp_gateway_runtime
        PUBLIC
            cxxmcp_gateway_core
            cxxmcp::sdk
        PRIVATE
            Threads::Threads
    )
    cxxmcp_gateway_require_cxx23(cxxmcp_gateway_runtime)
    add_library(cxxmcp-gateway::runtime ALIAS cxxmcp_gateway_runtime)
endif()

if(CXXMCP_GATEWAY_BUILD_CONFIG_IO)
    add_library(cxxmcp_gateway_config_io
        gateway/src/config_io.cpp
    )
    set_target_properties(cxxmcp_gateway_config_io PROPERTIES
        EXPORT_NAME config_io
        POSITION_INDEPENDENT_CODE ON
    )
    target_include_directories(cxxmcp_gateway_config_io PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/gateway/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(cxxmcp_gateway_config_io PUBLIC cxxmcp_gateway_core)
    cxxmcp_gateway_require_cxx23(cxxmcp_gateway_config_io)
    add_library(cxxmcp-gateway::config_io ALIAS cxxmcp_gateway_config_io)
endif()

if(CXXMCP_GATEWAY_BUILD_CLI)
    add_executable(cxxmcp_gateway_cli
        gateway/cli/main.cpp
    )
    target_link_libraries(cxxmcp_gateway_cli PRIVATE cxxmcp_gateway_runtime)
    if(TARGET cxxmcp_gateway_config_io)
        target_link_libraries(cxxmcp_gateway_cli PRIVATE cxxmcp_gateway_config_io)
        target_compile_definitions(cxxmcp_gateway_cli PRIVATE
            CXXMCP_GATEWAY_HAS_CONFIG_IO=1)
    endif()
    target_compile_definitions(cxxmcp_gateway_cli PRIVATE
        CXXMCP_GATEWAY_VERSION="${PROJECT_VERSION}")
    set_target_properties(cxxmcp_gateway_cli PROPERTIES
        OUTPUT_NAME cxxmcp-gateway
        EXPORT_NAME cli)
    cxxmcp_gateway_require_cxx23(cxxmcp_gateway_cli)
    add_executable(cxxmcp-gateway::cli ALIAS cxxmcp_gateway_cli)
endif()

if(CXXMCP_GATEWAY_BUILD_PERF)
    add_executable(cxxmcp_gateway_perf_stdio_fixture
        tests/fixtures/stdio_upstream.cpp
    )
    target_link_libraries(cxxmcp_gateway_perf_stdio_fixture
        PRIVATE cxxmcp::sdk)
    cxxmcp_gateway_require_cxx23(cxxmcp_gateway_perf_stdio_fixture)

    add_executable(cxxmcp_gateway_perf
        tools/perf/gateway_perf.cpp
    )
    target_link_libraries(cxxmcp_gateway_perf PRIVATE cxxmcp_gateway_runtime)
    target_compile_definitions(cxxmcp_gateway_perf PRIVATE
        CXXMCP_GATEWAY_PERF_STDIO_FIXTURE="$<TARGET_FILE:cxxmcp_gateway_perf_stdio_fixture>"
    )
    add_dependencies(cxxmcp_gateway_perf cxxmcp_gateway_perf_stdio_fixture)
    set_target_properties(cxxmcp_gateway_perf PROPERTIES
        OUTPUT_NAME cxxmcp-gateway-perf)
    cxxmcp_gateway_require_cxx23(cxxmcp_gateway_perf)
endif()

if(CXXMCP_GATEWAY_BUILD_EXAMPLES)
    add_executable(cxxmcp_gateway_multi_upstream_namespace_example
        examples/multi_upstream_namespace.cpp
    )
    target_link_libraries(cxxmcp_gateway_multi_upstream_namespace_example
        PRIVATE cxxmcp_gateway_core)
    set_target_properties(cxxmcp_gateway_multi_upstream_namespace_example PROPERTIES
        OUTPUT_NAME cxxmcp-gateway-multi-upstream-namespace-example)
    cxxmcp_gateway_require_cxx23(cxxmcp_gateway_multi_upstream_namespace_example)

    add_executable(cxxmcp_gateway_embedded_runtime_example
        examples/embedded_runtime_host.cpp
    )
    target_link_libraries(cxxmcp_gateway_embedded_runtime_example
        PRIVATE cxxmcp_gateway_runtime)
    set_target_properties(cxxmcp_gateway_embedded_runtime_example PROPERTIES
        OUTPUT_NAME cxxmcp-gateway-embedded-runtime-example)
    cxxmcp_gateway_require_cxx23(cxxmcp_gateway_embedded_runtime_example)
endif()

if(CXXMCP_GATEWAY_BUILD_GATEWAYD_MVP)
    add_executable(cxxmcp_gatewayd_mvp
        examples/gatewayd_mvp.cpp
    )
    target_link_libraries(cxxmcp_gatewayd_mvp
        PRIVATE
            cxxmcp_gateway_runtime
            cxxmcp_gateway_config_io
            cxxmcp::sdk)
    set_target_properties(cxxmcp_gatewayd_mvp PROPERTIES
        OUTPUT_NAME cxxmcp-gatewayd-mvp)
    cxxmcp_gateway_require_cxx23(cxxmcp_gatewayd_mvp)
endif()

if(CXXMCP_GATEWAY_BUILD_CXXMCP_EXAMPLES_DEV_TOOL_GATEWAY)
    include(ExternalProject)

    set(CXXMCP_GATEWAY_CXXMCP_EXAMPLES_BUILD_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/cxxmcp_examples_dev_tool_build")
    set(CXXMCP_GATEWAY_CXXMCP_EXAMPLES_CONFIG_ARGS
        -DCXXMCP_EXAMPLES_USE_ADJACENT_SDK=OFF
        -Dcxxmcp_DIR=${cxxmcp_DIR})
    if(CMAKE_BUILD_TYPE)
        list(APPEND CXXMCP_GATEWAY_CXXMCP_EXAMPLES_CONFIG_ARGS
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})
    endif()

    if(CXXMCP_GATEWAY_CXXMCP_EXAMPLES_SOURCE_DIR AND
       EXISTS "${CXXMCP_GATEWAY_CXXMCP_EXAMPLES_SOURCE_DIR}/CMakeLists.txt")
        set(CXXMCP_GATEWAY_CXXMCP_EXAMPLES_SOURCE
            SOURCE_DIR "${CXXMCP_GATEWAY_CXXMCP_EXAMPLES_SOURCE_DIR}")
    else()
        set(CXXMCP_GATEWAY_CXXMCP_EXAMPLES_SOURCE
            GIT_REPOSITORY "${CXXMCP_GATEWAY_CXXMCP_EXAMPLES_GIT_REPOSITORY}"
            GIT_TAG "${CXXMCP_GATEWAY_CXXMCP_EXAMPLES_GIT_TAG}")
    endif()

    ExternalProject_Add(cxxmcp_gateway_cxxmcp_examples_upstreams
        ${CXXMCP_GATEWAY_CXXMCP_EXAMPLES_SOURCE}
        BINARY_DIR "${CXXMCP_GATEWAY_CXXMCP_EXAMPLES_BUILD_DIR}"
        CMAKE_ARGS ${CXXMCP_GATEWAY_CXXMCP_EXAMPLES_CONFIG_ARGS}
        BUILD_COMMAND "${CMAKE_COMMAND}" --build
            "${CXXMCP_GATEWAY_CXXMCP_EXAMPLES_BUILD_DIR}"
            --target
                cxxmcp_workspace_server
                cxxmcp_git_server
                cxxmcp_cmake_ctest_server
                cxxmcp_log_triage_server
        INSTALL_COMMAND "")

    set(CXXMCP_GATEWAY_EXAMPLE_SUFFIX "${CMAKE_EXECUTABLE_SUFFIX}")
    set(CXXMCP_GATEWAY_EXAMPLE_WORKSPACE_SERVER
        "${CXXMCP_GATEWAY_CXXMCP_EXAMPLES_BUILD_DIR}/cxxmcp_workspace_server${CXXMCP_GATEWAY_EXAMPLE_SUFFIX}")
    set(CXXMCP_GATEWAY_EXAMPLE_GIT_SERVER
        "${CXXMCP_GATEWAY_CXXMCP_EXAMPLES_BUILD_DIR}/cxxmcp_git_server${CXXMCP_GATEWAY_EXAMPLE_SUFFIX}")
    set(CXXMCP_GATEWAY_EXAMPLE_CMAKE_CTEST_SERVER
        "${CXXMCP_GATEWAY_CXXMCP_EXAMPLES_BUILD_DIR}/cxxmcp_cmake_ctest_server${CXXMCP_GATEWAY_EXAMPLE_SUFFIX}")
    set(CXXMCP_GATEWAY_EXAMPLE_LOG_TRIAGE_SERVER
        "${CXXMCP_GATEWAY_CXXMCP_EXAMPLES_BUILD_DIR}/cxxmcp_log_triage_server${CXXMCP_GATEWAY_EXAMPLE_SUFFIX}")

    add_executable(cxxmcp_gateway_cxxmcp_examples_dev_tool_gateway
        examples/cxxmcp_examples_dev_tool_gateway.cpp
    )
    target_link_libraries(cxxmcp_gateway_cxxmcp_examples_dev_tool_gateway
        PRIVATE cxxmcp_gateway_runtime)
    target_compile_definitions(cxxmcp_gateway_cxxmcp_examples_dev_tool_gateway
        PRIVATE
            CXXMCP_GATEWAY_EXAMPLE_WORKSPACE_SERVER="${CXXMCP_GATEWAY_EXAMPLE_WORKSPACE_SERVER}"
            CXXMCP_GATEWAY_EXAMPLE_GIT_SERVER="${CXXMCP_GATEWAY_EXAMPLE_GIT_SERVER}"
            CXXMCP_GATEWAY_EXAMPLE_CMAKE_CTEST_SERVER="${CXXMCP_GATEWAY_EXAMPLE_CMAKE_CTEST_SERVER}"
            CXXMCP_GATEWAY_EXAMPLE_LOG_TRIAGE_SERVER="${CXXMCP_GATEWAY_EXAMPLE_LOG_TRIAGE_SERVER}"
            CXXMCP_GATEWAY_EXAMPLE_DEFAULT_WORKSPACE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_dependencies(cxxmcp_gateway_cxxmcp_examples_dev_tool_gateway
        cxxmcp_gateway_cxxmcp_examples_upstreams)
    set_target_properties(cxxmcp_gateway_cxxmcp_examples_dev_tool_gateway PROPERTIES
        OUTPUT_NAME cxxmcp-gateway-cxxmcp-examples-dev-tool-gateway)
    cxxmcp_gateway_require_cxx23(cxxmcp_gateway_cxxmcp_examples_dev_tool_gateway)
endif()

if(CXXMCP_GATEWAY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS cxxmcp_gateway_core
    EXPORT cxxmcp-gatewayCoreTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT core
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT core
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT core
)

if(TARGET cxxmcp_gateway_runtime)
    install(TARGETS cxxmcp_gateway_runtime
        EXPORT cxxmcp-gatewayRuntimeTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
    )
endif()

if(TARGET cxxmcp_gateway_config_io)
    install(TARGETS cxxmcp_gateway_config_io
        EXPORT cxxmcp-gatewayConfigIoTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT config_io
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT config_io
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT config_io
    )
endif()

if(TARGET cxxmcp_gateway_cli)
    install(TARGETS cxxmcp_gateway_cli
        EXPORT cxxmcp-gatewayCliTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT cli
    )
endif()

install(FILES
    gateway/include/cxxmcp/gateway.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cxxmcp
    COMPONENT core)
install(FILES
    gateway/include/cxxmcp/gateway/catalog.hpp
    gateway/include/cxxmcp/gateway/config.hpp
    gateway/include/cxxmcp/gateway/error.hpp
    gateway/include/cxxmcp/gateway/router.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cxxmcp/gateway
    COMPONENT core)

if(TARGET cxxmcp_gateway_runtime)
    install(FILES
        gateway/include/cxxmcp/gateway/runtime.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cxxmcp/gateway
        COMPONENT runtime)
endif()

if(TARGET cxxmcp_gateway_config_io)
    install(FILES
        gateway/include/cxxmcp/gateway/config_io.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cxxmcp/gateway
        COMPONENT config_io)
endif()

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/cxxmcp-gatewayConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cxxmcp-gatewayConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cxxmcp-gatewayConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cxxmcp-gateway
)

export(TARGETS cxxmcp_gateway_core
    NAMESPACE cxxmcp-gateway::
    FILE "${CMAKE_CURRENT_BINARY_DIR}/cxxmcp-gatewayCoreTargets.cmake")
if(TARGET cxxmcp_gateway_runtime)
    export(TARGETS cxxmcp_gateway_runtime
        NAMESPACE cxxmcp-gateway::
        FILE "${CMAKE_CURRENT_BINARY_DIR}/cxxmcp-gatewayRuntimeTargets.cmake")
endif()
if(TARGET cxxmcp_gateway_config_io)
    export(TARGETS cxxmcp_gateway_config_io
        NAMESPACE cxxmcp-gateway::
        FILE "${CMAKE_CURRENT_BINARY_DIR}/cxxmcp-gatewayConfigIoTargets.cmake")
endif()
if(TARGET cxxmcp_gateway_cli)
    export(TARGETS cxxmcp_gateway_cli
        NAMESPACE cxxmcp-gateway::
        FILE "${CMAKE_CURRENT_BINARY_DIR}/cxxmcp-gatewayCliTargets.cmake")
endif()

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cxxmcp-gatewayConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cxxmcp-gatewayConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cxxmcp-gateway
    COMPONENT core
)

install(EXPORT cxxmcp-gatewayCoreTargets
    NAMESPACE cxxmcp-gateway::
    FILE cxxmcp-gatewayCoreTargets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cxxmcp-gateway
    COMPONENT core
)

if(TARGET cxxmcp_gateway_runtime)
    install(EXPORT cxxmcp-gatewayRuntimeTargets
        NAMESPACE cxxmcp-gateway::
        FILE cxxmcp-gatewayRuntimeTargets.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cxxmcp-gateway
        COMPONENT runtime
    )
endif()

if(TARGET cxxmcp_gateway_config_io)
    install(EXPORT cxxmcp-gatewayConfigIoTargets
        NAMESPACE cxxmcp-gateway::
        FILE cxxmcp-gatewayConfigIoTargets.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cxxmcp-gateway
        COMPONENT config_io
    )
endif()

if(TARGET cxxmcp_gateway_cli)
    install(EXPORT cxxmcp-gatewayCliTargets
        NAMESPACE cxxmcp-gateway::
        FILE cxxmcp-gatewayCliTargets.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cxxmcp-gateway
        COMPONENT cli
    )
endif()
