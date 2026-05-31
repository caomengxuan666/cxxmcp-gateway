if(NOT DEFINED CXXMCP_GATEWAY_SOURCE_DIR)
    message(FATAL_ERROR "CXXMCP_GATEWAY_SOURCE_DIR is required")
endif()
if(NOT DEFINED CXXMCP_GATEWAY_BINARY_DIR)
    message(FATAL_ERROR "CXXMCP_GATEWAY_BINARY_DIR is required")
endif()

set(guard_script
    "${CXXMCP_GATEWAY_SOURCE_DIR}/tests/fixtures/source_dependency_guard.cmake")
set(fixture_root "${CXXMCP_GATEWAY_BINARY_DIR}/source_hygiene_negative")

function(expect_guard_failure case_name file_name file_contents expected_error)
    set(case_dir "${fixture_root}/${case_name}")
    file(REMOVE_RECURSE "${case_dir}")
    file(MAKE_DIRECTORY "${case_dir}")
    file(WRITE "${case_dir}/CMakeLists.txt" "cmake_minimum_required(VERSION 3.24)\n")
    file(WRITE "${case_dir}/README.md" "clean\n")
    file(WRITE "${case_dir}/${file_name}" "${file_contents}")

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -DCXXMCP_GATEWAY_SOURCE_DIR=${case_dir}
            -P "${guard_script}"
        RESULT_VARIABLE guard_result
        OUTPUT_VARIABLE guard_stdout
        ERROR_VARIABLE guard_stderr)
    if(guard_result EQUAL 0)
        message(FATAL_ERROR
            "source hygiene guard accepted ${case_name}\n"
            "stdout: ${guard_stdout}\n"
            "stderr: ${guard_stderr}")
    endif()
    if(NOT guard_stderr MATCHES "${expected_error}")
        message(FATAL_ERROR
            "source hygiene guard did not report ${expected_error} for ${case_name}\n"
            "stdout: ${guard_stdout}\n"
            "stderr: ${guard_stderr}")
    endif()
endfunction()

expect_guard_failure(
    "trailing_whitespace"
    "README.md"
    "bad trailing space \n"
    "trailing whitespace")

expect_guard_failure(
    "conflict_marker"
    "README.md"
    "<<<<<<< ours\n"
    "conflict marker")
