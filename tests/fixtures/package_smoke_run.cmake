if(NOT DEFINED CXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_DIR)
    message(FATAL_ERROR "CXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_DIR is required")
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
if(NOT DEFINED CXXMCP_GATEWAY_PACKAGE_SMOKE_PATH_DIRS)
    set(CXXMCP_GATEWAY_PACKAGE_SMOKE_PATH_DIRS "")
endif()
if(NOT DEFINED CXXMCP_GATEWAY_PACKAGE_SMOKE_CLI)
    set(CXXMCP_GATEWAY_PACKAGE_SMOKE_CLI "")
endif()
if(NOT DEFINED CXXMCP_GATEWAY_PACKAGE_SMOKE_EXPECTED_VERSION)
    set(CXXMCP_GATEWAY_PACKAGE_SMOKE_EXPECTED_VERSION "")
endif()
if(NOT DEFINED CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG)
    set(CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG "")
endif()

if(CMAKE_HOST_WIN32)
    set(executable_suffix ".exe")
else()
    set(executable_suffix "")
endif()

set(runtime_path "")
foreach(path_dir IN LISTS CXXMCP_GATEWAY_PACKAGE_SMOKE_PATH_DIRS)
    if(path_dir)
        if(runtime_path)
            if(CMAKE_HOST_WIN32)
                string(APPEND runtime_path ";${path_dir}")
            else()
                string(APPEND runtime_path ":${path_dir}")
            endif()
        else()
            set(runtime_path "${path_dir}")
        endif()
    endif()
endforeach()

if(CMAKE_HOST_WIN32)
    set(runtime_env "PATH=${runtime_path};$ENV{PATH}")
elseif(APPLE)
    set(runtime_env "DYLD_LIBRARY_PATH=${runtime_path}:$ENV{DYLD_LIBRARY_PATH}")
else()
    set(runtime_env "LD_LIBRARY_PATH=${runtime_path}:$ENV{LD_LIBRARY_PATH}")
endif()

function(run_package_smoke_executable target_name)
    set(executable_candidates
        "${CXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_DIR}/${target_name}${executable_suffix}")
    if(CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG)
        list(PREPEND executable_candidates
            "${CXXMCP_GATEWAY_PACKAGE_SMOKE_BUILD_DIR}/${CXXMCP_GATEWAY_PACKAGE_SMOKE_CONFIG}/${target_name}${executable_suffix}")
    endif()
    set(executable "")
    foreach(candidate IN LISTS executable_candidates)
        if(EXISTS "${candidate}")
            set(executable "${candidate}")
            break()
        endif()
    endforeach()
    if(NOT EXISTS "${executable}")
        message(FATAL_ERROR
            "package smoke executable is missing: ${executable_candidates}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "${runtime_env}" "${executable}"
        RESULT_VARIABLE run_result)
    if(NOT run_result EQUAL 0)
        message(FATAL_ERROR
            "package smoke executable failed: ${target_name}")
    endif()
endfunction()

run_package_smoke_executable(cxxmcp_gateway_package_core)

if(CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_RUNTIME)
    run_package_smoke_executable(cxxmcp_gateway_package_runtime)
endif()

if(CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CONFIG_IO)
    run_package_smoke_executable(cxxmcp_gateway_package_config_io)
endif()

if(CXXMCP_GATEWAY_PACKAGE_SMOKE_REQUIRE_CLI)
    if(NOT CXXMCP_GATEWAY_PACKAGE_SMOKE_CLI)
        message(FATAL_ERROR
            "CXXMCP_GATEWAY_PACKAGE_SMOKE_CLI is required when CLI is enabled")
    endif()
    if(NOT EXISTS "${CXXMCP_GATEWAY_PACKAGE_SMOKE_CLI}")
        message(FATAL_ERROR
            "package smoke CLI executable is missing: ${CXXMCP_GATEWAY_PACKAGE_SMOKE_CLI}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "${runtime_env}"
            "${CXXMCP_GATEWAY_PACKAGE_SMOKE_CLI}" --version
        RESULT_VARIABLE cli_result
        OUTPUT_VARIABLE cli_stdout
        ERROR_VARIABLE cli_stderr
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE)
    if(NOT cli_result EQUAL 0)
        message(FATAL_ERROR
            "package smoke CLI executable failed\n${cli_stderr}")
    endif()
    if(CXXMCP_GATEWAY_PACKAGE_SMOKE_EXPECTED_VERSION AND
       NOT cli_stdout STREQUAL CXXMCP_GATEWAY_PACKAGE_SMOKE_EXPECTED_VERSION)
        message(FATAL_ERROR
            "package smoke CLI version mismatch: expected "
            "${CXXMCP_GATEWAY_PACKAGE_SMOKE_EXPECTED_VERSION}, got "
            "${cli_stdout}")
    endif()
endif()
