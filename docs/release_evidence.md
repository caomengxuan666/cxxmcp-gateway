# Release Evidence Map

This file maps the release checklist to concrete evidence in the repository.
It is intentionally an index, not a replacement for running the gates.

## Scope Evidence

| Checklist item | Evidence |
| --- | --- |
| Shipped capability surface | `docs/gateway_scope.md`, `docs/technical_roadmap.md` |
| Host integration path | `docs/getting_started.md` |
| Runtime public contract | `docs/api_contract.md` |
| SDK/API/ABI/platform boundaries | `docs/compatibility.md` |
| Future-only capabilities remain unadvertised | `docs/gateway_scope.md`, `tests/runtime_integration_tests.cpp` capability and raw-request coverage |
| Future capability extension gate | `docs/capability_extension_gate.md` |

## Validation Evidence

| Gate | Evidence |
| --- | --- |
| Linux/macOS/Windows static and shared release-blocking jobs | `.github/workflows/ci.yml`, `docs/operational_gates.md` |
| Local static build and CTest | `ctest --test-dir build-agent-check --output-on-failure --timeout 300` |
| Local shared build and CTest | `ctest --test-dir build-agent-shared-check --output-on-failure --timeout 300` |
| Source hygiene | `gateway_source_hygiene_guard_negative` |
| Forbidden legacy dependencies | `gateway_source_dependency_guard` |
| Build-tree package consumer | `gateway_package_build_tree_configure`, `gateway_package_build_tree_build`, `gateway_package_build_tree_run` |
| Install-tree package consumer | `gateway_package_install_tree_install`, `gateway_package_install_tree_configure`, `gateway_package_install_tree_build`, `gateway_package_install_tree_run` |
| Component install behavior | `gateway_package_component_install_smoke` |
| Missing component failures | `gateway_package_core_only_runtime_component_fails`, `gateway_package_config_io_component_fails`, `gateway_package_cli_component_fails`, `gateway_package_cli_dependency_component_fails`, `gateway_package_full_build_core_component_fails` |
| Subproject defaults | `gateway_subproject_defaults` |
| Optional embedded example | `gateway_examples_build` |

## Public Contract Evidence

| Contract | Evidence |
| --- | --- |
| `find_package(cxxmcp-gateway CONFIG REQUIRED)` exports core | `tests/fixtures/package_smoke/CMakeLists.txt` |
| Runtime component is explicit | `tests/fixtures/package_smoke/runtime_consumer.cpp`, missing-runtime component failure tests |
| Config IO component is explicit | `tests/fixtures/package_smoke/config_io_consumer.cpp`, `gateway_config_io`, missing-config-IO component failure tests |
| CLI component is optional | CLI smoke tests, CLI component failure tests |
| CLI/config runtime knobs | `gateway_cli_help`, `gateway_cli_invalid_args`, `gateway_config_io`, `tests/fixtures/package_smoke/config_io_consumer.cpp` |
| `BUILD_SHARED_LIBS` is honored | static/shared CI matrix and local static/shared gates |
| Umbrella header is core-only | package smoke core consumer and runtime component tests |
| Runtime observer has no logging dependency | `test_runtime_observer_reports_status_without_logger_dependency` |

## Runtime Evidence

| Runtime behavior | Evidence |
| --- | --- |
| Tools/resources/prompts/completion data plane | `gateway_runtime_integration` |
| Capability-aware advertisement | `gateway_runtime_integration` capability advertisement tests |
| Side-effect-free lifecycle inspection | `test_runtime_stop_waits_for_active_stdio_call`, `test_runtime_stop_timeout_bounds_active_http_call_wait` |
| Catalog caching and invalidation | `test_tools_list_uses_cached_catalog_until_cleared`, `test_clear_cached_catalogs_keeps_persistent_session` |
| Per-call default session behavior | repeated stdio call tests and stdio child-process cleanup assertions |
| Opt-in persistent sessions | persistent stdio and HTTP lifecycle tests |
| Persistent session pool concurrency | `test_persistent_stdio_session_pool_allows_same_upstream_concurrency`, `test_persistent_http_session_pool_handles_queued_calls`, `test_persistent_http_stop_rejects_queued_session_pool_call`, perf `tools/call:persistent_pool2_pair` |
| Persistent session pool slot observability | `test_persistent_stdio_session_pool_allows_same_upstream_concurrency`, `test_persistent_stop_rejects_queued_session_pool_call`, package runtime consumer |
| Persistent session pool wait timeout | `test_persistent_pool_acquire_timeout_rejects_queued_call`, `test_persistent_http_pool_acquire_timeout_rejects_queued_call`, config IO and CLI invalid-argument tests |
| Persistent session pool failure isolation | `test_persistent_stdio_pool_failure_isolates_failed_slot` |
| Persistent HTTP pool timeout recovery | `test_persistent_http_pool_timeout_recovers` |
| Graceful stop and concurrent wait/stop | runtime stop, active-call stop, raw routed request stopping checks for process-stdio and Streamable HTTP paths, wait/stop overlap tests, `test_runtime_stop_timeout_bounds_active_stdio_call_wait`, `test_persistent_pool_stop_waits_for_timed_out_stdio_call` |
| Stop drain timeout | `test_runtime_stop_timeout_bounds_active_stdio_call_wait`, `test_runtime_stop_timeout_bounds_active_http_call_wait`, config IO and CLI invalid-argument tests, package runtime/config consumers |
| Hosted multi-client routing | `test_hosted_gateway_multiple_downstream_clients`, `test_hosted_gateway_multiple_downstream_clients_to_http_upstreams` |
| Cancellation/progress notification no-ops | `test_cancellation_and_progress_notifications_are_local_noops`, stopping-state notification checks in `test_runtime_stop_waits_for_active_stdio_call` and `test_runtime_stop_timeout_bounds_active_http_call_wait`, `test_hosted_cancellation_notifications_do_not_cancel_upstream_call`, `test_hosted_cancellation_notifications_do_not_cancel_http_upstream_call` |
| Downstream close during active upstream calls | `test_downstream_close_during_active_upstream_call_clears_state`, `test_downstream_close_during_active_http_upstream_call_clears_state` |
| Unsupported methods and notifications | raw request routing tests, notification no-op tests, and `test_notification_lifecycle_after_stop` |
| Error mapping | router tests, runtime integration error assertions, and config IO tests |

## SDK and Performance Evidence

| Evidence | Location |
| --- | --- |
| Pinned CI SDK revision | `.github/workflows/ci.yml` `CXXMCP_REF` |
| Exact SDK and gateway revisions for perf | `docs/release_baseline.md` |
| Release perf command and CSV output | `docs/release_baseline.md` |
| Gateway and direct SDK perf tool source | `tools/perf/gateway_perf.cpp` |

## Local Verification Notes

The expected local verification sequence is:

```powershell
cmake --build build-agent-check
ctest --test-dir build-agent-check --output-on-failure --timeout 300

cmake --build build-agent-shared-check
ctest --test-dir build-agent-shared-check --output-on-failure --timeout 300

git diff --check
```

On Windows, LF/CRLF conversion warnings from `git diff --check` are expected
and are not whitespace failures. Actual trailing whitespace or conflict-marker
reports remain release-blocking.
