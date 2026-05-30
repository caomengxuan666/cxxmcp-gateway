if(NOT DEFINED CXXMCP_GATEWAY_SOURCE_DIR)
    message(FATAL_ERROR "CXXMCP_GATEWAY_SOURCE_DIR is required")
endif()

set(scan_files "${CXXMCP_GATEWAY_SOURCE_DIR}/CMakeLists.txt")

file(GLOB_RECURSE gateway_sources
    LIST_DIRECTORIES false
    "${CXXMCP_GATEWAY_SOURCE_DIR}/gateway/*.cpp"
    "${CXXMCP_GATEWAY_SOURCE_DIR}/gateway/*.hpp")
list(APPEND scan_files ${gateway_sources})

if(EXISTS "${CXXMCP_GATEWAY_SOURCE_DIR}/cmake")
    file(GLOB_RECURSE cmake_sources
        LIST_DIRECTORIES false
        "${CXXMCP_GATEWAY_SOURCE_DIR}/cmake/*.cmake")
    list(APPEND scan_files ${cmake_sources})
endif()

set(forbidden_literals
    "CLI11"
    "spdlog"
    "tcb"
    "cxxmcp/app"
    "runtime/include"
    "tools/cli"
    "import-export"
    "import_export")

foreach(file_path IN LISTS scan_files)
    file(READ "${file_path}" contents)
    foreach(forbidden IN LISTS forbidden_literals)
        string(FIND "${contents}" "${forbidden}" match_index)
        if(NOT match_index EQUAL -1)
            message(FATAL_ERROR
                "forbidden legacy dependency '${forbidden}' found in ${file_path}")
        endif()
    endforeach()
endforeach()

set(forbidden_paths
    "app"
    "profile"
    "policy"
    "import-export"
    "import_export"
    "runtime/include"
    "tools/cli")

foreach(relative_path IN LISTS forbidden_paths)
    if(EXISTS "${CXXMCP_GATEWAY_SOURCE_DIR}/${relative_path}")
        message(FATAL_ERROR
            "forbidden legacy path still exists: ${relative_path}")
    endif()
endforeach()
