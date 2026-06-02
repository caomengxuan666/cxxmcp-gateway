# gatewayd MVP

This repository contains an experimental `cxxmcp-gatewayd-mvp` binary only to
validate the local middleware shape. It is not installed, exported, or treated
as the main product surface.

The prototype starts:

- one or more MCP data-plane endpoints, one per profile;
- one separate admin MCP endpoint;
- existing `GatewayRuntime` instances behind each profile.

It deliberately does not add product scope to `cxxmcp-gateway` core/runtime.
The prototype consumes the runtime exactly like an external daemon would.

## Build

Build against an installed `cxxmcp` SDK with the same build type. The gateway
CI pins the SDK revision through `.github/workflows/ci.yml` `CXXMCP_REF`;
local experiments should either use that pinned revision or explicitly record a
compatibility bump.

```powershell
cmake -S . -B build-gatewayd-mvp -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -Dcxxmcp_DIR=C:\path\to\cxxmcp-install\lib\cmake\cxxmcp `
  -DCXXMCP_GATEWAY_BUILD_GATEWAYD_MVP=ON
cmake --build build-gatewayd-mvp --target cxxmcp_gatewayd_mvp
```

## Run

```powershell
build-gatewayd-mvp\cxxmcp-gatewayd-mvp.exe --config examples\gatewayd_mvp_config.json
```

The sample config uses disabled upstreams so the daemon shape can be started
without requiring external MCP servers. Replace them with real upstreams to
test end-to-end routing.

Data-plane endpoint:

```text
http://127.0.0.1:39931/mcp/default
```

Admin MCP endpoint:

```text
http://127.0.0.1:39932/admin
```

## Admin Tools

The admin endpoint is an MCP endpoint for this feasibility pass. It exposes:

- `gatewayd.health`: reports admin URL and profile MCP URLs;
- `gatewayd.upstreams`: reports configured upstreams and runtime state;
- `gatewayd.catalog.tools`: lists merged tools per profile.

This is intentionally smaller than the future daemon design. A real
`cxxmcp-gatewayd` repository can replace the admin MCP endpoint with REST,
SSE/events, a UI, or CLI integration without changing the gateway data-plane
library.

## Config Shape

```json
{
  "admin": {
    "host": "127.0.0.1",
    "port": 39932,
    "path": "/admin"
  },
  "profiles": [
    {
      "id": "default",
      "endpoint": {
        "host": "127.0.0.1",
        "port": 39931,
        "path": "/mcp/default"
      },
      "runtime": {
        "upstreamSessionMode": "persistent",
        "persistentSessionPoolSize": 1
      },
      "upstreams": [
        {
          "id": "workspace",
          "transport": "stdio",
          "command": "cxxmcp_workspace_server"
        }
      ]
    }
  ]
}
```

Each profile body reuses the existing gateway config document shape, with an
additional `id` and `endpoint` object owned by the daemon layer.
