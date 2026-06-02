if(NOT DEFINED CXXMCP_GATEWAY_SOURCE_DIR)
    message(FATAL_ERROR "CXXMCP_GATEWAY_SOURCE_DIR is required")
endif()

set(scan_files
    "${CXXMCP_GATEWAY_SOURCE_DIR}/CMakeLists.txt"
    "${CXXMCP_GATEWAY_SOURCE_DIR}/README.md")
set(legacy_scan_files "${CXXMCP_GATEWAY_SOURCE_DIR}/CMakeLists.txt")
set(target_definition_scan_files "${CXXMCP_GATEWAY_SOURCE_DIR}/CMakeLists.txt")
set(gateway_implementation_scan_files "")
set(gateway_sdk_boundary_scan_files "")

file(GLOB_RECURSE gateway_sources
    LIST_DIRECTORIES false
    "${CXXMCP_GATEWAY_SOURCE_DIR}/gateway/*.cpp"
    "${CXXMCP_GATEWAY_SOURCE_DIR}/gateway/*.hpp")
list(APPEND scan_files ${gateway_sources})
list(APPEND legacy_scan_files ${gateway_sources})
list(APPEND gateway_implementation_scan_files ${gateway_sources})

set(gateway_sdk_boundary_scan_files
    "${CXXMCP_GATEWAY_SOURCE_DIR}/gateway/include/cxxmcp/gateway.hpp"
    "${CXXMCP_GATEWAY_SOURCE_DIR}/gateway/include/cxxmcp/gateway/catalog.hpp"
    "${CXXMCP_GATEWAY_SOURCE_DIR}/gateway/include/cxxmcp/gateway/config.hpp"
    "${CXXMCP_GATEWAY_SOURCE_DIR}/gateway/include/cxxmcp/gateway/config_io.hpp"
    "${CXXMCP_GATEWAY_SOURCE_DIR}/gateway/include/cxxmcp/gateway/error.hpp"
    "${CXXMCP_GATEWAY_SOURCE_DIR}/gateway/include/cxxmcp/gateway/router.hpp"
    "${CXXMCP_GATEWAY_SOURCE_DIR}/gateway/include/cxxmcp/gateway/runtime.hpp"
    "${CXXMCP_GATEWAY_SOURCE_DIR}/gateway/src/catalog.cpp"
    "${CXXMCP_GATEWAY_SOURCE_DIR}/gateway/src/config_io.cpp"
    "${CXXMCP_GATEWAY_SOURCE_DIR}/gateway/src/error.cpp"
    "${CXXMCP_GATEWAY_SOURCE_DIR}/gateway/src/router.cpp")

file(GLOB_RECURSE test_sources
    LIST_DIRECTORIES false
    "${CXXMCP_GATEWAY_SOURCE_DIR}/tests/*.cpp"
    "${CXXMCP_GATEWAY_SOURCE_DIR}/tests/*.cmake"
    "${CXXMCP_GATEWAY_SOURCE_DIR}/tests/*.json")
list(APPEND scan_files ${test_sources})

if(EXISTS "${CXXMCP_GATEWAY_SOURCE_DIR}/tools")
    file(GLOB_RECURSE tool_sources
        LIST_DIRECTORIES false
        "${CXXMCP_GATEWAY_SOURCE_DIR}/tools/*.cpp"
        "${CXXMCP_GATEWAY_SOURCE_DIR}/tools/*.hpp")
    list(APPEND scan_files ${tool_sources})
    list(APPEND legacy_scan_files ${tool_sources})
endif()

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
        "${CXXMCP_GATEWAY_SOURCE_DIR}/cmake/*.cmake"
        "${CXXMCP_GATEWAY_SOURCE_DIR}/cmake/*.cmake.in")
    list(APPEND scan_files ${cmake_sources})
    list(APPEND legacy_scan_files ${cmake_sources})
    list(APPEND target_definition_scan_files ${cmake_sources})
endif()

list(REMOVE_DUPLICATES scan_files)
list(REMOVE_DUPLICATES legacy_scan_files)
list(REMOVE_DUPLICATES target_definition_scan_files)
list(REMOVE_DUPLICATES gateway_implementation_scan_files)
list(REMOVE_DUPLICATES gateway_sdk_boundary_scan_files)

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

set(forbidden_drift_targets
    "cxxmcp_gateway_admin"
    "cxxmcp_gateway_auth"
    "cxxmcp_gateway_c_api"
    "cxxmcp_gateway_cabi"
    "cxxmcp_gateway_dashboard"
    "cxxmcp_gateway_ffi"
    "cxxmcp_gateway_gui"
    "cxxmcp_gateway_policy"
    "cxxmcp_gateway_profile"
    "cxxmcp_gateway_rbac"
    "cxxmcp_gateway_tenant"
    "cxxmcp_gateway_ui")

foreach(file_path IN LISTS target_definition_scan_files)
    file(READ "${file_path}" contents)
    foreach(forbidden IN LISTS forbidden_drift_targets)
        string(FIND "${contents}" "${forbidden}" match_index)
        if(NOT match_index EQUAL -1)
            message(FATAL_ERROR
                "forbidden product/control-plane/FFI target '${forbidden}' found in ${file_path}; add a separate design record before changing the gateway surface")
        endif()
    endforeach()
endforeach()

foreach(file_path IN LISTS gateway_implementation_scan_files)
    file(READ "${file_path}" contents)
    string(FIND "${contents}" "extern \"C\"" extern_c_index)
    if(NOT extern_c_index EQUAL -1)
        message(FATAL_ERROR
            "direct C ABI surface found in ${file_path}; C ABI work must be a separate experimental component with an explicit design record")
    endif()
endforeach()

set(forbidden_sdk_boundary_includes
    "cxxmcp/peer.hpp"
    "cxxmcp/service.hpp"
    "cxxmcp/client/"
    "cxxmcp/server/"
    "cxxmcp/transport/")

foreach(file_path IN LISTS gateway_sdk_boundary_scan_files)
    if(NOT EXISTS "${file_path}")
        continue()
    endif()
    file(READ "${file_path}" contents)
    foreach(forbidden IN LISTS forbidden_sdk_boundary_includes)
        string(FIND "${contents}" "${forbidden}" match_index)
        if(NOT match_index EQUAL -1)
            message(FATAL_ERROR
                "SDK runtime primitive '${forbidden}' included outside gateway runtime implementation in ${file_path}")
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
    "admin"
    "bindings"
    "c_api"
    "cabi"
    "control-plane"
    "control_plane"
    "ffi"
    "gui"
    "profile"
    "policy"
    "rbac"
    "rust"
    "tenant"
    "ui"
    "import-export"
    "import_export"
    "gateway/admin"
    "gateway/auth"
    "gateway/bindings"
    "gateway/c_api"
    "gateway/cabi"
    "gateway/control-plane"
    "gateway/control_plane"
    "gateway/ffi"
    "gateway/gui"
    "gateway/policy"
    "gateway/profile"
    "gateway/rbac"
    "gateway/tenant"
    "gateway/ui"
    "runtime/include"
    "tools/cli")

foreach(relative_path IN LISTS forbidden_paths)
    if(EXISTS "${CXXMCP_GATEWAY_SOURCE_DIR}/${relative_path}")
        message(FATAL_ERROR
            "forbidden legacy path still exists: ${relative_path}")
    endif()
endforeach()
