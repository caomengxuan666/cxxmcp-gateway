# cxxmcp-gateway

`cxxmcp-gateway` is a library-first C++23 MCP gateway for the
[`cxxmcp`](https://github.com/caomengxuan666/cxxmcp) SDK.

Its primary artifact is a small C++ library that aggregates MCP capabilities
from multiple upstream servers and routes downstream MCP requests back to the
selected upstream. The command-line app is a reference runner for local
development, smoke tests, and simple sidecar-style deployments; it should not
grow into the main product surface or a management console.

The new design starts from a narrow data plane:

- upstream MCP servers over process stdio or Streamable HTTP;
- one hosted Streamable HTTP gateway endpoint;
- `tools/list` aggregation with `<upstream>.<tool>` names;
- `tools/call` routing back to the selected upstream;
- `resources/list`, `resources/read`, and `resources/templates/list` routing
  through gateway-owned resource URIs;
- `prompts/list` and `prompts/get` routing with `<upstream>.<prompt>` names;
- `completion/complete` routing for prompt names and resource template URIs
  when initialized upstream capabilities prove support.

Control-plane and management features will be added only after the gateway core
is validated.

See [Gateway Scope and Boundaries](docs/gateway_scope.md) for the current
responsibility split and validation plan.
See [Getting Started](docs/getting_started.md) for a minimal C++ host
integration path.
See [Runtime API Contract](docs/api_contract.md) for lifecycle, threading,
session, cache, capability, and error-shape semantics.
See [Compatibility Policy](docs/compatibility.md) for the current SDK,
platform, API, ABI, and feature-support boundaries.
See [Gateway Technical Roadmap](docs/technical_roadmap.md) for the phased
implementation path.
See [Operational Gates](docs/operational_gates.md) for release-blocking checks,
the supported CI matrix, and performance measurement expectations.
See [Performance Profile](docs/performance_profile.md) for latency,
throughput, session-mode, and baseline interpretation guidance.
See [Release Checklist](docs/release_checklist.md) for the release-candidate
validation checklist.
See [Release Evidence Map](docs/release_evidence.md) for the test and
documentation evidence behind each release gate.

## Use as a Library

Embed the gateway when your application already owns configuration,
authentication, policy, deployment, or observability, but needs reusable MCP
capability aggregation and routing.

```cpp
#include "cxxmcp/gateway/runtime.hpp"

mcp::gateway::GatewayConfig config;
config.upstreams.push_back(/* upstream config */);

mcp::gateway::GatewayRuntime runtime(std::move(config));
(void)runtime.start_http({.host = "127.0.0.1", .port = 39931, .path = "/mcp"});
(void)runtime.wait();
```

Hosts that need observability can install a `GatewayRuntimeObserver` through
`GatewayRuntimeOptions`. Observer callbacks receive runtime lifecycle and
upstream status events and do not require a logging framework dependency.
Repeated upstream calls use explicit per-call sessions by default. Hosts that
prefer lower repeated-call latency over the simplest lifecycle can opt into one
persistent session per upstream with
`GatewayRuntimeOptions::upstream_session_mode`.

## Build

Build against an installed HTTP-enabled `cxxmcp` SDK:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:\path\to\cxxmcp\install
cmake --build build
ctest --test-dir build --output-on-failure
```

The CMake package exports `cxxmcp-gateway::core` plus, when enabled,
`cxxmcp-gateway::runtime`, `cxxmcp-gateway::config_io`, and the optional
`cxxmcp-gateway::cli` executable component. The CLI, tests, and optional config
IO layer default to `ON` only for top-level builds and default to `OFF` when
this repository is embedded as a subproject.
Subproject default behavior is covered by the test suite: embedding projects
get core/runtime by default without CLI, config IO, or gateway test targets.
The `<cxxmcp/gateway.hpp>` umbrella header is core-only; runtime and config IO
consumers should include `<cxxmcp/gateway/runtime.hpp>` or
`<cxxmcp/gateway/config_io.hpp>` and request the matching package component.
Install components are additive: installing the optional `cli` executable also
requires installing the runtime component, and config IO when the CLI was built
with file-config support.

Library targets honor `BUILD_SHARED_LIBS`. Static builds keep position
independent code enabled so the libraries can be linked into host shared
objects, and Windows shared builds use automatic symbol export while the public
ABI is still stabilizing.

Optional performance tooling is available with
`-DCXXMCP_GATEWAY_BUILD_PERF=ON`. It is excluded from the default build and
prints CSV latency summaries for stdio/HTTP cold and cached `tools/list`,
per-call `tools/call`, opt-in persistent-session `tools/call`, and a direct
SDK HTTP comparison row.
These measurements are regression evidence, not a low-latency or high-QPS
service guarantee. See [Performance Profile](docs/performance_profile.md) for
how to choose between default per-call sessions, opt-in persistent sessions,
and direct SDK integration.

Optional embedding examples are available with
`-DCXXMCP_GATEWAY_BUILD_EXAMPLES=ON`. They are excluded from the default build
and are not installed as package components.

## CLI

Use the CLI as a thin reference runner, not as the primary architecture.

```powershell
cxxmcp-gateway serve --port 39931 --upstream-http local=http://127.0.0.1:3000/mcp
```

Use `--upstream-stdio <id=command>` for process-stdio upstreams.
Use `--session-mode persistent` to keep initialized upstream sessions and
`--session-pool-size <n>` to allow up to `<n>` concurrent initialized sessions
per upstream. `--session-acquire-timeout-ms <ms>` bounds how long a persistent
call waits for a busy pool slot; the default `0` keeps the existing unbounded
wait. `--active-call-drain-timeout-ms <ms>` bounds shutdown waiting for active
upstream calls; the default `0` keeps the existing unbounded wait. `--prewarm`
refreshes upstream capabilities before the hosted
endpoint starts and initializes the configured persistent pool. This is the
reference runner form of the library
`GatewayRuntimeOptions::upstream_session_mode`,
`GatewayRuntimeOptions::persistent_session_pool_size`, and
`GatewayRuntimeOptions::persistent_session_acquire_timeout`,
`GatewayRuntimeOptions::active_call_drain_timeout`, plus the
`GatewayRuntime::refresh_upstream_capabilities()` path; it reduces repeated-call
setup cost but remains a bounded per-upstream pool, not adaptive multiplexing.
Library hosts can also set `GatewayRuntimeOptions::active_call_drain_timeout`
to make shutdown return a lifecycle error after a bounded wait for active
upstream calls; the gateway still does not cancel active upstream work.
When `cxxmcp_gateway_config_io` is built, the reference runner also accepts
`--config <file>` for JSON gateway config and appends any command-line
upstreams to the loaded config. File config and command-line upstreams are
validated together before the HTTP endpoint is started; duplicate upstream ids
are rejected. The hosted endpoint remains CLI-owned through `--host`, `--port`,
and `--path`, and root-level `host`, `port`, or `path` fields in config files
are rejected instead of being silently ignored.
Endpoint defaults are `--host 127.0.0.1`, `--port 3000`, and `--path /mcp`.
Disabled upstreams may omit transport connection fields such as `command` or
`uri`; enabled upstreams must provide the fields required by their transport.
Config values are parsed literally; environment-variable substitution is not
performed by `cxxmcp_gateway_config_io`.

```json
{
  "name": "local-gateway",
  "version": "1.0.0",
  "runtime": {
    "upstreamSessionMode": "persistent",
    "persistentSessionPoolSize": 2,
    "prewarmCapabilities": true
  },
  "upstreams": [
    {
      "id": "local",
      "transport": "http",
      "uri": "http://127.0.0.1:3000/mcp",
      "timeoutMs": 30000
    },
    {
      "id": "fs",
      "transport": "stdio",
      "command": "filesystem-server",
      "args": ["--root", "."],
      "timeoutMs": 30000
    }
  ]
}
```
