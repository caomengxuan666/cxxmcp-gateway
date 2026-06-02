# Release Baseline

This file records reproducible release-candidate evidence for the current
library-first routing MVP. It complements the release-blocking gates in
[`operational_gates.md`](operational_gates.md).

## 2026-06-01 Refreshed Routing MVP Baseline

Gateway code base measured:

```text
e56556eb6c422b83b4db839f8d1d8eec8893ddba plus the current gatewayd MVP
working-tree changes
```

SDK source revision:

```text
caomengxuan666/cxxmcp
a9da92e291c552cd401060e0f848f60467d1f38f
```

The SDK was built from a clean clone with submodules initialized, not from the
dirty adjacent `MCPServer.cpp` working tree.

Environment:

| Field | Value |
| --- | --- |
| OS | Microsoft Windows NT 10.0.26200.0 |
| Compiler | Clang 22.1.1, target `x86_64-pc-windows-msvc` |
| CMake | 4.2.3-msvc3 |
| Build type | Release |
| Gateway perf target | `cxxmcp_gateway_perf` |
| Iterations | 50 |

SDK configure command:

```powershell
cmake -S C:\Users\cmx\repo\cxxmcp-sdk-perf-src -B C:\Users\cmx\repo\cxxmcp-sdk-perf-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=C:\Users\cmx\repo\cxxmcp-sdk-perf-install -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCXXMCP_SDK_CXX_STANDARD=23 -DCXXMCP_BUILD_SDK=ON -DCXXMCP_BUILD_CLIENT=ON -DCXXMCP_BUILD_SERVER=ON -DCXXMCP_BUILD_EXAMPLES=OFF -DCXXMCP_BUILD_TESTS=OFF -DCXXMCP_BUILD_BENCHMARKS=OFF -DCXXMCP_ENABLE_HTTP=ON
cmake --build C:\Users\cmx\repo\cxxmcp-sdk-perf-build --parallel
cmake --install C:\Users\cmx\repo\cxxmcp-sdk-perf-build
```

Gateway perf configure and run:

```powershell
cmake -S . -B build-gateway-latest-sdk-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCXXMCP_GATEWAY_BUILD_PERF=ON -DCXXMCP_GATEWAY_BUILD_GATEWAYD_MVP=ON -Dcxxmcp_DIR=C:\Users\cmx\repo\cxxmcp-sdk-perf-install\lib\cmake\cxxmcp
cmake --build build-gateway-latest-sdk-release --target cxxmcp_gateway_perf
build-gateway-latest-sdk-release\cxxmcp-gateway-perf.exe --iterations 50 --http-port 39976
```

Performance result:

```csv
transport,operation,iterations,median_us,p95_us
stdio,tools/list:cold,50,28115,39922
stdio,tools/list:cached,50,20,30
stdio,tools/call:per_call,50,26130,30856
stdio,tools/call:persistent,50,383,532
http,tools/list:cold,50,43723,61929
http,tools/list:cached,50,7,7
http,tools/call:per_call,50,47117,63079
http,tools/call:persistent,50,15307,16478
http,tools/call:direct_sdk_persistent,50,15287,16223
http,tools/call:persistent_pool2_pair,50,218152,233829
```

Notes:

- These numbers are a local Windows release-candidate baseline, not a
  cross-platform performance contract.
- Debug-build timings are not comparable to this baseline.
- The measured gateway code base includes uncommitted gatewayd MVP working-tree
  changes. Record the final commit before using this as release evidence.
- The initial Release perf build against the adjacent SDK install failed because
  that install used a Debug MSVC runtime. The baseline above uses a clean
  Release SDK install at the exact SDK commit listed above.
- This refreshed baseline supersedes earlier local measurements after
  concurrent catalog fan-out, aggregate catalog caching, process-stdio upstream
  timeouts, opt-in persistent upstream sessions, expanded performance
  measurement, lifecycle hardening, persistent lifecycle coverage, and
  example-gate work landed in the PR branch.
- The `persistent_pool2_pair` row measures two concurrent HTTP slow tool calls
  through a persistent pool of size two. It is a regression baseline for the
  pool path, not a latency guarantee; the current Streamable HTTP fixture path
  still dominates end-to-end timing.
- The `direct_sdk_persistent` row measures an initialized SDK `ClientPeer`
  calling the same Streamable HTTP fixture without going through
  `GatewayRuntime`. It is diagnostic evidence for separating gateway overhead
  from SDK/transport/server path cost, not a release performance contract.
