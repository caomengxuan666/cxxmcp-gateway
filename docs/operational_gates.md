# Operational Gates

This document records the minimum operational evidence required before a
gateway change is treated as release-ready. It is intentionally narrower than a
product release process: `cxxmcp-gateway` is still library-first, and the CLI is
only a reference runner.

## Release-Blocking Checks

Every pull request that changes gateway code, CMake packaging, public headers,
or tests must pass the GitHub Actions `ci` workflow.

The current release-blocking jobs are:

| Job | Purpose |
| --- | --- |
| `gateway-ubuntu-latest-static` | Linux static build, package smoke, and tests |
| `gateway-ubuntu-latest-shared` | Linux shared-library build and tests |
| `gateway-macos-latest-static` | macOS static build and tests |
| `gateway-macos-latest-shared` | macOS shared-library build and tests |
| `gateway-windows-latest-static` | Windows static build and tests |
| `gateway-windows-latest-shared` | Windows shared-library build and tests |

The workflow builds an HTTP-enabled C++23 `cxxmcp` SDK from
`caomengxuan666/cxxmcp` at `CXXMCP_REF`, installs it, configures the gateway
against that install tree, builds the gateway, and runs `ctest`. The CTest
suite includes a source hygiene guard for conflict markers and trailing
whitespace across tracked source, test, documentation, CMake, and workflow
text files.

## Local Gate

Before pushing gateway changes, run the same static and shared coverage locally
when possible:

```powershell
cmake -S . -B build-agent-check -DCMAKE_BUILD_TYPE=Debug -Dcxxmcp_DIR=C:\Users\cmx\repo\MCPServer.cpp\out\install\gateway-sdk-cxx23\lib\cmake\cxxmcp
cmake --build build-agent-check
ctest --test-dir build-agent-check --output-on-failure --timeout 180

cmake -S . -B build-agent-shared-check -DCMAKE_BUILD_TYPE=Debug -DBUILD_SHARED_LIBS=ON -DCXXMCP_GATEWAY_BUILD_TESTS=ON -DCXXMCP_GATEWAY_BUILD_CLI=ON -Dcxxmcp_DIR=C:\Users\cmx\repo\MCPServer.cpp\out\install\gateway-sdk-cxx23\lib\cmake\cxxmcp
cmake --build build-agent-shared-check
ctest --test-dir build-agent-shared-check --output-on-failure --timeout 180

git diff --check
```

On Windows, `git diff --check` may report LF/CRLF conversion warnings. Those
warnings are not whitespace errors, but actual trailing whitespace or conflict
marker reports are release-blocking.

## Supported Matrix

The current supported matrix is the CI matrix above:

- Linux, static and shared gateway libraries;
- macOS, static and shared gateway libraries;
- Windows, static and shared gateway libraries;
- C++23;
- HTTP-enabled `cxxmcp` SDK.

The public CMake package must continue to support both build-tree and
install-tree consumption. Component install behavior for `core`, `runtime`,
`config_io`, and `cli` is part of the release-blocking package smoke tests.

## SDK Compatibility

The gateway tracks the HTTP-enabled `cxxmcp` SDK branch configured by
`CXXMCP_REF` in CI. A release candidate must record the exact `cxxmcp` commit
used for validation. Until a versioned SDK compatibility policy is introduced,
changes should not claim compatibility beyond the CI-tested SDK revision.

## Performance Measurement

Performance is not yet a release blocker, but regressions must be measurable.
Use a Release build and the optional `cxxmcp-gateway-perf` tool as the baseline.
The tool is intentionally excluded from the default build and CI gate.

Build and run it explicitly against a `cxxmcp` SDK built with the same build
type and MSVC runtime settings:

```powershell
cmake -S . -B build-perf -G Ninja -DCMAKE_BUILD_TYPE=Release -DCXXMCP_GATEWAY_BUILD_PERF=ON -Dcxxmcp_DIR=C:\Users\cmx\repo\MCPServer.cpp\out\install\gateway-sdk-cxx23\lib\cmake\cxxmcp
cmake --build build-perf --target cxxmcp_gateway_perf
build-perf\cxxmcp-gateway-perf.exe --iterations 50
```

The tool prints CSV rows with transport, operation, iteration count, median
latency, and p95 latency in microseconds.

Measure at least:

- `tools/list` through one stdio upstream;
- `tools/list` through one Streamable HTTP upstream;
- `tools/call` through one stdio upstream;
- `tools/call` through one Streamable HTTP upstream.

Record the gateway commit, SDK commit, OS, compiler, build type, transport,
operation, iteration count, median latency, p95 latency, and notes about local
load. Do not compare Debug-build timings across releases.

The current recorded release-candidate performance evidence is in
[`release_baseline.md`](release_baseline.md).

## Release Checklist

Before cutting or tagging a release candidate, complete
[`release_checklist.md`](release_checklist.md). The checklist is intentionally
library-first: it validates package consumption, component installs, SDK
revision evidence, performance evidence, and public contracts without adding
GUI, daemon, policy, auth, or admin API gates.

## Non-Goals For This Gate

This gate does not add:

- release signing;
- installers;
- auto-update;
- policy or auth validation;
- GUI or daemon packaging;
- a logging framework dependency.

Those belong to later operational or product packaging work and must remain
outside the default library-first build path until explicitly designed.
