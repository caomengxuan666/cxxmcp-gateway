# Release Baseline

This file records reproducible release-candidate evidence for the current
library-first routing MVP. It complements the release-blocking gates in
[`operational_gates.md`](operational_gates.md).

## 2026-05-31 Refreshed Routing MVP Baseline

Gateway code commit measured:

```text
c25a215fc5f933212c2fe9f2d28729667db25dbe
```

SDK source revision:

```text
caomengxuan666/cxxmcp master
8f89739665c9cf435607da932bca5b35a110fc4d
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
cmake -S . -B build-agent-perf-release-current -G Ninja -DCMAKE_BUILD_TYPE=Release -DCXXMCP_GATEWAY_BUILD_PERF=ON -Dcxxmcp_DIR=C:\Users\cmx\repo\cxxmcp-sdk-perf-install\lib\cmake\cxxmcp
cmake --build build-agent-perf-release-current --target cxxmcp_gateway_perf
build-agent-perf-release-current\cxxmcp-gateway-perf.exe --iterations 50 --http-port 39974
```

Performance result:

```csv
transport,operation,iterations,median_us,p95_us
stdio,tools/list,50,53217,136441
stdio,tools/call,50,54548,91541
http,tools/list,50,31295,61953
http,tools/call,50,31921,48682
```

Notes:

- These numbers are a local Windows release-candidate baseline, not a
  cross-platform performance contract.
- Debug-build timings are not comparable to this baseline.
- The measured gateway code commit is the code state immediately before this
  baseline document refresh.
- The initial Release perf build against the adjacent SDK install failed because
  that install used a Debug MSVC runtime. The baseline above uses a clean
  Release SDK install at the exact SDK commit listed above.
- This refreshed baseline supersedes the earlier local measurement at gateway
  commit `6317886e7965d9e1c651929f315ed1c1967a1bcb` after routing/runtime,
  capability-advertisement, config-IO, source-hygiene, lifecycle evidence, and
  package-smoke changes landed in the PR branch.
