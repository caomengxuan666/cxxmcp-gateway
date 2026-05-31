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
See [Gateway Technical Roadmap](docs/technical_roadmap.md) for the phased
implementation path.
See [Operational Gates](docs/operational_gates.md) for release-blocking checks,
the supported CI matrix, and performance measurement expectations.

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

## Build

Build against an installed HTTP-enabled `cxxmcp` SDK:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:\path\to\cxxmcp\install
cmake --build build
ctest --test-dir build --output-on-failure
```

The CMake package exports `cxxmcp-gateway::core` plus, when enabled,
`cxxmcp-gateway::runtime` and `cxxmcp-gateway::config_io`. The CLI, tests, and
optional config IO layer default to `ON` only for top-level builds and default
to `OFF` when this repository is embedded as a subproject.
Subproject default behavior is covered by the test suite: embedding projects
get core/runtime by default without CLI, config IO, or gateway test targets.

Library targets honor `BUILD_SHARED_LIBS`. Static builds keep position
independent code enabled so the libraries can be linked into host shared
objects, and Windows shared builds use automatic symbol export while the public
ABI is still stabilizing.

Optional performance tooling is available with
`-DCXXMCP_GATEWAY_BUILD_PERF=ON`. It is excluded from the default build and
prints CSV latency summaries for stdio/HTTP `tools/list` and `tools/call`.

## CLI

Use the CLI as a thin reference runner, not as the primary architecture.

```powershell
cxxmcp-gateway serve --port 39931 --upstream-http local=http://127.0.0.1:3000/mcp
```

Use `--upstream-stdio <id=command>` for process-stdio upstreams.
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
      "args": ["--root", "."]
    }
  ]
}
```
