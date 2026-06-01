function(expect_cli_failure name expected_error)
    execute_process(
        COMMAND "${CXXMCP_GATEWAY_CLI}" ${ARGN}
        RESULT_VARIABLE cli_result
        OUTPUT_VARIABLE cli_stdout
        ERROR_VARIABLE cli_stderr)

    if(cli_result EQUAL 0)
        message(FATAL_ERROR
            "${name} unexpectedly succeeded\n"
            "stdout: ${cli_stdout}\n"
            "stderr: ${cli_stderr}")
    endif()

    if(NOT cli_stderr MATCHES "${expected_error}")
        message(FATAL_ERROR
            "${name} did not report expected error: ${expected_error}\n"
            "stdout: ${cli_stdout}\n"
            "stderr: ${cli_stderr}")
    endif()
endfunction()

execute_process(
    COMMAND "${CXXMCP_GATEWAY_CLI}" serve
        --config "${CXXMCP_GATEWAY_CONFIG}"
        --port 39999
    TIMEOUT 2
    RESULT_VARIABLE cli_result
    OUTPUT_VARIABLE cli_stdout
    ERROR_VARIABLE cli_stderr)

if(cli_result EQUAL 0)
    message(FATAL_ERROR
        "file_prewarm_false unexpectedly exited successfully\n"
        "stdout: ${cli_stdout}\n"
        "stderr: ${cli_stderr}")
endif()

if(cli_stderr MATCHES "failed to prewarm upstream capabilities")
    message(FATAL_ERROR
        "file_prewarm_false attempted prewarm without --prewarm\n"
        "stdout: ${cli_stdout}\n"
        "stderr: ${cli_stderr}")
endif()

expect_cli_failure(prewarm_flag_overrides_file_runtime_config
    "failed to prewarm upstream capabilities"
    serve --config "${CXXMCP_GATEWAY_CONFIG}" --port 39999 --prewarm)
