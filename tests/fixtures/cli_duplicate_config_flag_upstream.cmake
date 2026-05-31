execute_process(
    COMMAND "${CXXMCP_GATEWAY_CLI}" serve
        --config "${CXXMCP_GATEWAY_CONFIG}"
        --port 39999
        --upstream-stdio fixture=other-server
    RESULT_VARIABLE cli_result
    OUTPUT_VARIABLE cli_stdout
    ERROR_VARIABLE cli_stderr)

if(cli_result EQUAL 0)
    message(FATAL_ERROR
        "CLI accepted duplicate upstream id from merged config and flags")
endif()

if(NOT cli_stdout MATCHES "duplicate upstream id" AND
   NOT cli_stderr MATCHES "duplicate upstream id")
    message(FATAL_ERROR
        "CLI duplicate upstream failure did not report duplicate upstream id\n"
        "stdout: ${cli_stdout}\n"
        "stderr: ${cli_stderr}")
endif()
