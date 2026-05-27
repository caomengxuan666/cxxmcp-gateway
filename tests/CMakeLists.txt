function(cxxmcp_gateway_mark_release_blocking test_name)
    if(TEST "${test_name}")
        set(labels release-blocking ${ARGN})
        set_tests_properties("${test_name}" PROPERTIES LABELS "${labels}")
    endif()
endfunction()

if(TARGET mcp_app AND TARGET mcp_gateway)
    add_executable(mcp_app_tests
        app/test_app.cpp
    )

    target_link_libraries(mcp_app_tests PRIVATE mcp_app mcp_gateway)

    add_test(NAME app COMMAND mcp_app_tests)
endif()

if(TARGET mcp_app)
    add_executable(mcp_server_management_tests
        app/test_server_management.cpp
    )

    target_link_libraries(mcp_server_management_tests PRIVATE mcp_app)

    add_test(NAME server_management COMMAND mcp_server_management_tests)

    add_executable(mcp_client_discovery_tests
        app/test_client_discovery.cpp
    )

    target_link_libraries(mcp_client_discovery_tests PRIVATE mcp_app)

    add_test(NAME client_discovery COMMAND mcp_client_discovery_tests)

    add_executable(mcp_tool_management_tests
        app/test_tool_management.cpp
    )

    target_link_libraries(mcp_tool_management_tests PRIVATE mcp_app)

    add_test(NAME tool_management COMMAND mcp_tool_management_tests)

    add_executable(mcp_task_management_tests
        app/test_task_management.cpp
    )

    target_link_libraries(mcp_task_management_tests PRIVATE mcp_app)

    add_test(NAME task_management COMMAND mcp_task_management_tests)
endif()

if(TARGET mcp_gateway)
    add_executable(mcp_gateway_tests
        app/test_gateway.cpp
    )

    target_link_libraries(mcp_gateway_tests PRIVATE mcp_gateway)

    add_test(NAME gateway COMMAND mcp_gateway_tests)
endif()

if(TARGET mcp_cli AND TARGET mcp_cli_app)
    add_executable(mcp_cli_tests
        cli/test_cli.cpp
    )

    target_link_libraries(mcp_cli_tests PRIVATE mcp_cli)
    target_compile_definitions(mcp_cli_tests PRIVATE
        MCP_TEST_CLI_EXE="$<TARGET_FILE:mcp_cli_app>"
    )
    add_dependencies(mcp_cli_tests mcp_cli_app)

    add_test(NAME cli COMMAND mcp_cli_tests)
endif()

get_property(cxxmcp_gateway_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(cxxmcp_gateway_test_target IN LISTS cxxmcp_gateway_test_targets)
    cxxmcp_gateway_require_cxx20("${cxxmcp_gateway_test_target}")
endforeach()
