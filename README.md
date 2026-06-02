# cxxmcp-gateway

`cxxmcp-gateway` is a library-first C++23 MCP gateway for the
[`cxxmcp`](https://github.com/caomengxuan666/cxxmcp) SDK.

Its primary artifact is a library-quality reference gateway: a reusable C++
data-plane library that aggregates MCP capabilities from multiple upstream
servers and routes downstream MCP requests back to the selected upstream. It is
not a second MCP SDK, an enterprise management product, or a cross-language
runtime. The command-line app is a runnable reference runner for local
development, smoke tests, demos, and simple sidecar-style deployments; it
should not grow into the main product surface or a management console.

The `cxxmcp` SDK owns protocol models, JSON-RPC machinery, peers, services,
transports, and general MCP client/server behavior. This gateway owns only the
multi-upstream composition semantics on top: exposed namespaces, catalog
aggregation, route decisions, gateway-level errors, capability advertisement
policy, and hosted runtime wiring.

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
See [cxxmcp Integration Boundary](docs/cxxmcp_integration_boundary.md) for the
SDK-versus-gateway responsibility line and cross-language binding policy.
See [Positioning Guardrails](docs/positioning_guardrails.md) for the project
shape, reference boundaries, and feature decision rules that prevent product or
SDK drift.
See [Convergence Plan](docs/convergence_plan.md) for the implementation
sequence that turns those boundaries into tests, examples, and runtime
hardening work.
See [cxxmcp-examples Dev-Tool Gateway Demo](docs/cxxmcp_examples_gateway_demo.md)
for the optional superbuild that compiles independent `cxxmcp-examples`
servers and exposes them through one gateway endpoint.
See [Local Middleware Service Design](docs/local_middleware_service_design.md)
for the future Redis-like local daemon direction and why it belongs outside
this data-plane library.
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
`GatewayRuntimeOptions` or `make_gateway_runtime_options()`. Observer
callbacks receive runtime lifecycle and upstream status events and do not
require a logging framework dependency.
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
and are not installed as package components. The examples include a core-only
multi-upstream namespace sample and a hosted runtime sample; together they show
the intended split between pure gateway composition semantics and SDK-backed
runtime execution.

An optional cross-repository demo is available with
`-DCXXMCP_GATEWAY_BUILD_CXXMCP_EXAMPLES_DEV_TOOL_GATEWAY=ON`. It fetches the
independent `cxxmcp-examples` repository as an external project by default,
builds selected stdio MCP servers, and hosts them through one gateway endpoint.
Maintainers can pass `CXXMCP_GATEWAY_CXXMCP_EXAMPLES_SOURCE_DIR` explicitly to
test a local checkout. This is a demo superbuild, not part of the default
package.

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
`GatewayRuntimeConfig` to `make_gateway_runtime_options()` mapping plus the
`GatewayRuntime::refresh_upstream_capabilities()` path. It reduces repeated-call
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
Library consumers can parse config from a `protocol::Json` value, JSON text, or
a JSON file through the `config_io` component.
Endpoint defaults are `--host 127.0.0.1`, `--port 3000`, and `--path /mcp`.
Disabled upstreams may omit transport connection fields such as `command` or
`uri`; enabled upstreams must provide the fields required by their transport.
Config values are parsed literally by default. Library consumers can opt into
`${NAME}` substitution for JSON string values by passing
`GatewayConfigLoadOptions::environment`; missing variables fail with a
`gateway.config` error. The reference CLI keeps the default literal behavior.

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
