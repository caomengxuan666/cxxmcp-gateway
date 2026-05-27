cmake_minimum_required(VERSION 3.23)

project(cxxmcp-gateway
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_EXTENSIONS OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

include(CTest)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

find_package(Threads REQUIRED)

function(cxxmcp_gateway_require_cxx20 target_name)
    target_compile_features("${target_name}" PUBLIC cxx_std_20)
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

option(CXXMCP_GATEWAY_BUILD_APP
    "Build the runtime application service library" ON)
option(CXXMCP_GATEWAY_BUILD_GATEWAY
    "Build the gateway service library" ON)
option(CXXMCP_GATEWAY_BUILD_CLI
    "Build the command-line application" ON)
option(CXXMCP_GATEWAY_BUILD_TESTS
    "Build tests for enabled layers" ${BUILD_TESTING})

find_package(cxxmcp CONFIG REQUIRED)

if(CXXMCP_GATEWAY_BUILD_APP OR CXXMCP_GATEWAY_BUILD_GATEWAY OR CXXMCP_GATEWAY_BUILD_CLI)
    add_subdirectory(third_party/spdlog EXCLUDE_FROM_ALL)
endif()

if(CXXMCP_GATEWAY_BUILD_APP)
    add_library(mcp_app STATIC
        runtime/src/client_discovery.cpp
        runtime/src/exposure_management.cpp
        runtime/src/file_stores.cpp
        runtime/src/serialization.cpp
        runtime/src/task_management.cpp
        runtime/src/upstream_client.cpp
        runtime/src/server_management.cpp
        runtime/src/services.cpp
        runtime/src/tool_management.cpp
    )
    set_target_properties(mcp_app PROPERTIES EXPORT_NAME runtime)
    target_include_directories(mcp_app PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/runtime/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(mcp_app PUBLIC cxxmcp::sdk)
    cxxmcp_gateway_require_cxx20(mcp_app)
endif()

if(CXXMCP_GATEWAY_BUILD_GATEWAY)
    add_library(mcp_gateway STATIC
        runtime/src/client_config.cpp
        runtime/src/gateway.cpp
        runtime/src/gateway_public.cpp
        runtime/src/gateway_onboarding.cpp
        runtime/src/gateway_runtime.cpp
    )
    set_target_properties(mcp_gateway PROPERTIES EXPORT_NAME gateway)
    target_include_directories(mcp_gateway PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/runtime/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(mcp_gateway PUBLIC cxxmcp::sdk mcp_app)
    cxxmcp_gateway_require_cxx20(mcp_gateway)
endif()

if(CXXMCP_GATEWAY_BUILD_CLI)
    set(CLI11_BUILD_DOCS OFF CACHE BOOL "Disable CLI11 docs" FORCE)
    set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "Disable CLI11 examples" FORCE)
    set(CLI11_BUILD_EXAMPLES_JSON OFF CACHE BOOL "Disable CLI11 json examples" FORCE)
    set(CLI11_BUILD_TESTS OFF CACHE BOOL "Disable CLI11 tests" FORCE)
    set(CLI11_INSTALL OFF CACHE BOOL "Disable CLI11 install rules" FORCE)
    set(CLI11_SINGLE_FILE OFF CACHE BOOL "Disable CLI11 single-file build" FORCE)
    add_subdirectory(third_party/CLI11 EXCLUDE_FROM_ALL)

    add_library(mcp_cli STATIC
        tools/cli/src/commands.cpp
    )
    set_target_properties(mcp_cli PROPERTIES EXPORT_NAME cli)
    target_include_directories(mcp_cli PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tools/cli/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_include_directories(mcp_cli PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/CLI11/include
    )
    target_link_libraries(mcp_cli PUBLIC cxxmcp::sdk mcp_app mcp_gateway)
    cxxmcp_gateway_require_cxx20(mcp_cli)

    add_executable(mcp_cli_app
        tools/cli/src/main.cpp
    )
    target_link_libraries(mcp_cli_app PRIVATE mcp_cli)
    target_compile_definitions(mcp_cli_app PRIVATE MCP_PROJECT_VERSION="${PROJECT_VERSION}")
    set_target_properties(mcp_cli_app PROPERTIES OUTPUT_NAME cxxmcp-gateway)
endif()

if(CXXMCP_GATEWAY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

set(CXXMCP_GATEWAY_INSTALL_TARGETS "")
if(TARGET mcp_app)
    list(APPEND CXXMCP_GATEWAY_INSTALL_TARGETS mcp_app)
endif()
if(TARGET mcp_gateway)
    list(APPEND CXXMCP_GATEWAY_INSTALL_TARGETS mcp_gateway)
endif()
if(TARGET mcp_cli)
    list(APPEND CXXMCP_GATEWAY_INSTALL_TARGETS mcp_cli)
endif()

install(TARGETS ${CXXMCP_GATEWAY_INSTALL_TARGETS}
    EXPORT cxxmcp-gatewayTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY runtime/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY tools/cli/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

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

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cxxmcp-gatewayConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cxxmcp-gatewayConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cxxmcp-gateway
)

install(EXPORT cxxmcp-gatewayTargets
    NAMESPACE cxxmcp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cxxmcp-gateway
)
