# Local Middleware Service Design

Status: design direction. Not implemented in this repository.

The long-term product-shaped opportunity is a local MCP middleware service:

```text
local apps / IDEs / agents
  -> one local MCP endpoint
  -> managed upstream MCP servers
```

The analogy is Redis-like in deployment shape, not in protocol semantics: a
small local service that applications can rely on, configure, observe, restart,
and treat as infrastructure. It should be boring, explicit, and easy to run.

## Repository Boundary

This repository remains the data-plane library:

```text
cxxmcp-gateway
  core/runtime/config_io/reference CLI
```

The middleware service should live in a separate repository or package, such
as:

```text
cxxmcp-gatewayd
cxxmcp-local-gateway
```

That service consumes `cxxmcp-gateway::runtime`. It does not move daemon,
admin, UI, installer, profile, policy, audit, or service-management code into
`cxxmcp-gateway` core/runtime.

## Scope

The service is still MCP-focused. It should not become a generic protocol
gateway or arbitrary plugin platform.

Allowed product identity:

- local MCP middleware;
- managed local MCP gateway daemon;
- one stable local MCP endpoint over multiple upstream MCP servers;
- operational shell around `cxxmcp-gateway`.

Avoided identities:

- generic API gateway;
- arbitrary JSON-RPC gateway;
- service mesh;
- non-MCP plugin runtime;
- replacement for the `cxxmcp` SDK.

## Endpoint Split

The service should expose separate endpoints:

```text
MCP data-plane endpoint:
  http://127.0.0.1:39931/mcp

Admin/control-plane endpoint:
  http://127.0.0.1:39932/admin
```

The MCP endpoint handles only MCP traffic. The admin endpoint handles local
management. They must not be multiplexed into one MCP route.

## Minimum Admin Surface

Initial admin APIs should be small and local-only:

```text
GET  /admin/health
GET  /admin/upstreams
GET  /admin/upstreams/{id}
POST /admin/upstreams/{id}/enable
POST /admin/upstreams/{id}/disable
POST /admin/reload
GET  /admin/capabilities
GET  /admin/catalog/tools
GET  /admin/catalog/resources
GET  /admin/catalog/prompts
GET  /admin/events
```

The first version should avoid remote multi-user semantics. Bind to loopback by
default. Treat non-loopback binding as an explicit, documented security
decision.

## Config Shape

The daemon owns file paths, reload policy, and runtime process lifecycle:

```json
{
  "gateway": {
    "host": "127.0.0.1",
    "port": 39931,
    "path": "/mcp"
  },
  "admin": {
    "host": "127.0.0.1",
    "port": 39932,
    "path": "/admin"
  },
  "runtime": {
    "upstreamSessionMode": "persistent",
    "persistentSessionPoolSize": 2,
    "persistentSessionAcquireTimeoutMs": 100,
    "activeCallDrainTimeoutMs": 5000,
    "prewarmCapabilities": true
  },
  "upstreams": []
}
```

`cxxmcp-gateway` may provide reusable parsing for gateway data-plane config,
but the daemon owns admin endpoint config, profile locations, reload semantics,
and service lifecycle.

## Operational Responsibilities

The middleware service may own:

- config file discovery;
- process start/stop as a local service;
- hot reload of validated upstream config;
- status and event APIs;
- logs and local diagnostics;
- explicit local security defaults;
- optional install/startup scripts;
- packaging for the daemon.

The data-plane library should only add hooks needed by this daemon when those
hooks are generally useful to C++ hosts.

## Library Hooks The Daemon Might Need

Future daemon work may justify small additions to `cxxmcp-gateway`:

- side-effect-free runtime status snapshots;
- structured catalog diagnostics;
- explicit reload helper that constructs a new runtime safely;
- observer event fields with stable schemas;
- documented shutdown timeout behavior;
- optional admin-friendly serialization helpers.

These should remain library hooks, not daemon logic.

## Non-Goals For The Daemon MVP

- remote enterprise management;
- multi-tenant auth and RBAC;
- cloud control plane;
- GUI dashboard;
- credential vault;
- audit warehouse;
- arbitrary non-MCP plugin execution;
- generic HTTP or JSON-RPC proxying.

These can be separate products later, but they should not define the MVP.

## Decision

Build the current repository as the reusable MCP data-plane kernel. If the
Redis-like local middleware idea moves forward, start it as a separate daemon
repository that depends on this package.

This keeps the gateway valuable to embedders while leaving room for a real
local service product.
