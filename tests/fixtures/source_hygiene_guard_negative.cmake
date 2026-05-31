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

expect_guard_failure(
    "legacy_dependency"
    "CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.24)\n# CLI11\n"
    "forbidden legacy dependency")

set(cmake_template_case_dir "${fixture_root}/cmake_template")
file(REMOVE_RECURSE "${cmake_template_case_dir}")
file(MAKE_DIRECTORY "${cmake_template_case_dir}/cmake")
file(WRITE "${cmake_template_case_dir}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.24)\n")
file(WRITE "${cmake_template_case_dir}/README.md" "clean\n")
file(WRITE "${cmake_template_case_dir}/cmake/package.cmake.in"
    "# generated template with trailing whitespace \n")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -DCXXMCP_GATEWAY_SOURCE_DIR=${cmake_template_case_dir}
        -P "${guard_script}"
    RESULT_VARIABLE cmake_template_result
    OUTPUT_VARIABLE cmake_template_stdout
    ERROR_VARIABLE cmake_template_stderr)
if(cmake_template_result EQUAL 0)
    message(FATAL_ERROR
        "source hygiene guard accepted cmake_template\n"
        "stdout: ${cmake_template_stdout}\n"
        "stderr: ${cmake_template_stderr}")
endif()
if(NOT cmake_template_stderr MATCHES "trailing whitespace")
    message(FATAL_ERROR
        "source hygiene guard did not report cmake template whitespace\n"
        "stdout: ${cmake_template_stdout}\n"
        "stderr: ${cmake_template_stderr}")
endif()

set(legacy_path_case_dir "${fixture_root}/legacy_path")
file(REMOVE_RECURSE "${legacy_path_case_dir}")
file(MAKE_DIRECTORY "${legacy_path_case_dir}/runtime/include")
file(WRITE "${legacy_path_case_dir}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.24)\n")
file(WRITE "${legacy_path_case_dir}/README.md" "clean\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -DCXXMCP_GATEWAY_SOURCE_DIR=${legacy_path_case_dir}
        -P "${guard_script}"
    RESULT_VARIABLE legacy_path_result
    OUTPUT_VARIABLE legacy_path_stdout
    ERROR_VARIABLE legacy_path_stderr)
if(legacy_path_result EQUAL 0)
    message(FATAL_ERROR
        "source hygiene guard accepted legacy_path\n"
        "stdout: ${legacy_path_stdout}\n"
        "stderr: ${legacy_path_stderr}")
endif()
if(NOT legacy_path_stderr MATCHES "forbidden legacy path")
    message(FATAL_ERROR
        "source hygiene guard did not report forbidden legacy path\n"
        "stdout: ${legacy_path_stdout}\n"
        "stderr: ${legacy_path_stderr}")
endif()
