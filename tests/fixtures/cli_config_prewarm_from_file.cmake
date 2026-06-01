execute_process(
    COMMAND "${CXXMCP_GATEWAY_CLI}" serve
        --config "${CXXMCP_GATEWAY_CONFIG}"
        --port 39999
    RESULT_VARIABLE cli_result
    OUTPUT_VARIABLE cli_stdout
    ERROR_VARIABLE cli_stderr)

if(cli_result EQUAL 0)
    message(FATAL_ERROR
        "CLI unexpectedly succeeded with prewarm-only config\n"
        "stdout: ${cli_stdout}\n"
        "stderr: ${cli_stderr}")
endif()

if(NOT cli_stderr MATCHES "failed to prewarm upstream capabilities")
    message(FATAL_ERROR
        "CLI did not apply prewarmCapabilities from config file\n"
        "stdout: ${cli_stdout}\n"
        "stderr: ${cli_stderr}")
endif()
