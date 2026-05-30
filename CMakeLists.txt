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

if(CXXMCP_GATEWAY_BUILD_CLI AND NOT CXXMCP_GATEWAY_BUILD_RUNTIME)
    message(FATAL_ERROR
        "CXXMCP_GATEWAY_BUILD_CLI requires CXXMCP_GATEWAY_BUILD_RUNTIME")
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

if(CXXMCP_GATEWAY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

set(CXXMCP_GATEWAY_INSTALL_TARGETS cxxmcp_gateway_core)
if(TARGET cxxmcp_gateway_runtime)
    list(APPEND CXXMCP_GATEWAY_INSTALL_TARGETS cxxmcp_gateway_runtime)
endif()
if(TARGET cxxmcp_gateway_config_io)
    list(APPEND CXXMCP_GATEWAY_INSTALL_TARGETS cxxmcp_gateway_config_io)
endif()
if(TARGET cxxmcp_gateway_cli)
    list(APPEND CXXMCP_GATEWAY_INSTALL_TARGETS cxxmcp_gateway_cli)
endif()

install(TARGETS cxxmcp_gateway_core
    EXPORT cxxmcp-gatewayTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT core
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT core
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT core
)

if(TARGET cxxmcp_gateway_runtime)
    install(TARGETS cxxmcp_gateway_runtime
        EXPORT cxxmcp-gatewayTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
    )
endif()

if(TARGET cxxmcp_gateway_config_io)
    install(TARGETS cxxmcp_gateway_config_io
        EXPORT cxxmcp-gatewayTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT config_io
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT config_io
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT config_io
    )
endif()

if(TARGET cxxmcp_gateway_cli)
    install(TARGETS cxxmcp_gateway_cli
        EXPORT cxxmcp-gatewayTargets
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

set(CXXMCP_GATEWAY_CONFIG_HAS_RUNTIME FALSE)
if(TARGET cxxmcp_gateway_runtime)
    set(CXXMCP_GATEWAY_CONFIG_HAS_RUNTIME TRUE)
endif()
set(CXXMCP_GATEWAY_CONFIG_HAS_CLI FALSE)
if(TARGET cxxmcp_gateway_cli)
    set(CXXMCP_GATEWAY_CONFIG_HAS_CLI TRUE)
endif()
set(CXXMCP_GATEWAY_CONFIG_HAS_CONFIG_IO FALSE)
if(TARGET cxxmcp_gateway_config_io)
    set(CXXMCP_GATEWAY_CONFIG_HAS_CONFIG_IO TRUE)
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

export(TARGETS ${CXXMCP_GATEWAY_INSTALL_TARGETS}
    NAMESPACE cxxmcp-gateway::
    FILE "${CMAKE_CURRENT_BINARY_DIR}/cxxmcp-gatewayTargets.cmake")

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cxxmcp-gatewayConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cxxmcp-gatewayConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cxxmcp-gateway
    COMPONENT core
)

install(EXPORT cxxmcp-gatewayTargets
    NAMESPACE cxxmcp-gateway::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cxxmcp-gateway
    COMPONENT core
)
