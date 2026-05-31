if(NOT DEFINED CXXMCP_GATEWAY_SOURCE_DIR)
    message(FATAL_ERROR "CXXMCP_GATEWAY_SOURCE_DIR is required")
endif()

set(scan_files
    "${CXXMCP_GATEWAY_SOURCE_DIR}/CMakeLists.txt"
    "${CXXMCP_GATEWAY_SOURCE_DIR}/README.md")
set(legacy_scan_files "${CXXMCP_GATEWAY_SOURCE_DIR}/CMakeLists.txt")

file(GLOB_RECURSE gateway_sources
    LIST_DIRECTORIES false
    "${CXXMCP_GATEWAY_SOURCE_DIR}/gateway/*.cpp"
    "${CXXMCP_GATEWAY_SOURCE_DIR}/gateway/*.hpp")
list(APPEND scan_files ${gateway_sources})
list(APPEND legacy_scan_files ${gateway_sources})

file(GLOB_RECURSE test_sources
    LIST_DIRECTORIES false
    "${CXXMCP_GATEWAY_SOURCE_DIR}/tests/*.cpp"
    "${CXXMCP_GATEWAY_SOURCE_DIR}/tests/*.cmake"
    "${CXXMCP_GATEWAY_SOURCE_DIR}/tests/*.json")
list(APPEND scan_files ${test_sources})

file(GLOB_RECURSE doc_sources
    LIST_DIRECTORIES false
    "${CXXMCP_GATEWAY_SOURCE_DIR}/docs/*.md")
list(APPEND scan_files ${doc_sources})

if(EXISTS "${CXXMCP_GATEWAY_SOURCE_DIR}/.github/workflows")
    file(GLOB_RECURSE workflow_sources
        LIST_DIRECTORIES false
        "${CXXMCP_GATEWAY_SOURCE_DIR}/.github/workflows/*.yml"
        "${CXXMCP_GATEWAY_SOURCE_DIR}/.github/workflows/*.yaml")
    list(APPEND scan_files ${workflow_sources})
endif()

if(EXISTS "${CXXMCP_GATEWAY_SOURCE_DIR}/cmake")
    file(GLOB_RECURSE cmake_sources
        LIST_DIRECTORIES false
        "${CXXMCP_GATEWAY_SOURCE_DIR}/cmake/*.cmake")
    list(APPEND scan_files ${cmake_sources})
    list(APPEND legacy_scan_files ${cmake_sources})
endif()

list(REMOVE_DUPLICATES scan_files)
list(REMOVE_DUPLICATES legacy_scan_files)

set(forbidden_literals
    "CLI11"
    "spdlog"
    "tcb"
    "cxxmcp/app"
    "runtime/include"
    "tools/cli"
    "import-export"
    "import_export")

foreach(file_path IN LISTS legacy_scan_files)
    file(READ "${file_path}" contents)
    foreach(forbidden IN LISTS forbidden_literals)
        string(FIND "${contents}" "${forbidden}" match_index)
        if(NOT match_index EQUAL -1)
            message(FATAL_ERROR
                "forbidden legacy dependency '${forbidden}' found in ${file_path}")
        endif()
    endforeach()
endforeach()

foreach(file_path IN LISTS scan_files)
    file(READ "${file_path}" contents)
    string(REPLACE "\r\n" "\n" contents "${contents}")
    string(REPLACE "\r" "\n" contents "${contents}")
    set(contents_with_final_newline "${contents}\n")
    if(contents_with_final_newline MATCHES
       "(^|\n)(<<<<<<<|=======|>>>>>>>)([ \t][^\n]*)?\n")
        message(FATAL_ERROR "conflict marker found in ${file_path}")
    endif()
    if(contents_with_final_newline MATCHES "[ \t]+\n")
        message(FATAL_ERROR "trailing whitespace found in ${file_path}")
    endif()
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
