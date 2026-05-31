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

expect_cli_failure(invalid_session_mode "invalid --session-mode value"
    serve --session-mode pooled --upstream-stdio fixture=fixture-server)

expect_cli_failure(missing_session_mode
    "unknown or incomplete option: --session-mode"
    serve --session-mode)

expect_cli_failure(invalid_session_pool_size
    "invalid --session-pool-size value"
    serve --session-pool-size 0 --upstream-stdio fixture=fixture-server)

expect_cli_failure(nonnumeric_session_pool_size
    "invalid --session-pool-size value"
    serve --session-pool-size many --upstream-stdio fixture=fixture-server)

expect_cli_failure(missing_session_pool_size
    "unknown or incomplete option: --session-pool-size"
    serve --session-pool-size)

expect_cli_failure(invalid_session_acquire_timeout
    "invalid --session-acquire-timeout-ms value"
    serve --session-acquire-timeout-ms -1 --upstream-stdio fixture=fixture-server)

expect_cli_failure(nonnumeric_session_acquire_timeout
    "invalid --session-acquire-timeout-ms value"
    serve --session-acquire-timeout-ms many --upstream-stdio fixture=fixture-server)

expect_cli_failure(missing_session_acquire_timeout
    "unknown or incomplete option: --session-acquire-timeout-ms"
    serve --session-acquire-timeout-ms)

expect_cli_failure(invalid_active_call_drain_timeout
    "invalid --active-call-drain-timeout-ms value"
    serve --active-call-drain-timeout-ms -1 --upstream-stdio fixture=fixture-server)

expect_cli_failure(nonnumeric_active_call_drain_timeout
    "invalid --active-call-drain-timeout-ms value"
    serve --active-call-drain-timeout-ms many --upstream-stdio fixture=fixture-server)

expect_cli_failure(missing_active_call_drain_timeout
    "unknown or incomplete option: --active-call-drain-timeout-ms"
    serve --active-call-drain-timeout-ms)

expect_cli_failure(malformed_http_upstream
    "--upstream-http expects <id=url>"
    serve --upstream-http missing-assignment)

expect_cli_failure(malformed_stdio_upstream
    "--upstream-stdio expects <id=command>"
    serve --upstream-stdio missing-assignment)
