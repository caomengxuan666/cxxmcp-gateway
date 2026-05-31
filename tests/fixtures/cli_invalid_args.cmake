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

expect_cli_failure(invalid_port "invalid --port value"
    serve --port not-a-port --upstream-stdio fixture=fixture-server)

expect_cli_failure(malformed_http_upstream
    "--upstream-http expects <id=url>"
    serve --upstream-http missing-assignment)

expect_cli_failure(malformed_stdio_upstream
    "--upstream-stdio expects <id=command>"
    serve --upstream-stdio missing-assignment)
