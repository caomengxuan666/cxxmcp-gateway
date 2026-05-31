cmake_minimum_required(VERSION 3.23)
project(cxxmcp_gateway_package_smoke LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

find_package(cxxmcp-gateway CONFIG REQUIRED)
if(NOT TARGET cxxmcp-gateway::core)
    message(FATAL_ERROR
        "default package discovery did not export cxxmcp-gateway::core")
endif()

find_package(cxxmcp-gateway CONFIG REQUIRED COMPONENTS core)

add_executable(cxxmcp_gateway_package_core core_consumer.cpp)
target_link_libraries(cxxmcp_gateway_package_core
    PRIVATE cxxmcp-gateway::core)
target_compile_features(cxxmcp_gateway_package_core PRIVATE cxx_std_23)

if(CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME)
    find_package(cxxmcp-gateway CONFIG REQUIRED COMPONENTS runtime)
    add_executable(cxxmcp_gateway_package_runtime runtime_consumer.cpp)
    target_link_libraries(cxxmcp_gateway_package_runtime
        PRIVATE cxxmcp-gateway::runtime)
    target_compile_features(cxxmcp_gateway_package_runtime PRIVATE cxx_std_23)
endif()

if(CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO)
    find_package(cxxmcp-gateway CONFIG REQUIRED COMPONENTS config_io)
    add_executable(cxxmcp_gateway_package_config_io config_io_consumer.cpp)
    target_link_libraries(cxxmcp_gateway_package_config_io
        PRIVATE cxxmcp-gateway::config_io)
    target_compile_features(cxxmcp_gateway_package_config_io
        PRIVATE cxx_std_23)
endif()

if(CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI)
    find_package(cxxmcp-gateway CONFIG REQUIRED COMPONENTS cli)
    if(NOT TARGET cxxmcp-gateway::cli)
        message(FATAL_ERROR "cli component did not export cxxmcp-gateway::cli")
    endif()
endif()
