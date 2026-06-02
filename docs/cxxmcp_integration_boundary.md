# cxxmcp Integration Boundary

`cxxmcp-gateway` is a library-quality reference gateway built on top of the
`cxxmcp` SDK. It is not a second MCP SDK, a general cross-language runtime, or
an enterprise gateway product.

The useful boundary is narrow:

```text
host app / reference CLI / future UI
  -> cxxmcp-gateway runtime
  -> cxxmcp-gateway core
  -> cxxmcp SDK
```

`cxxmcp` owns MCP protocol and transport primitives. `cxxmcp-gateway` owns the
multi-upstream composition semantics that are useful to hosts building one
downstream endpoint over several upstream MCP servers.

## What cxxmcp Owns

The SDK is the source of truth for MCP implementation details:

- protocol types and serialization;
- JSON-RPC request, response, and notification machinery;
- client and server peers;
- service lifecycle primitives;
- process stdio and Streamable HTTP transports;
- SDK-owned lifecycle and liveness behavior such as initialize and ping;
- typed helpers for MCP capability families.

When the SDK adds or hardens a general primitive such as cancellation,
backpressure, reconnect, transport authentication, or session pooling, the
gateway should consume that primitive instead of maintaining a parallel
implementation.

## What the Gateway Owns

The gateway owns reusable multi-server behavior:

- one downstream MCP endpoint backed by multiple upstream MCP servers;
- gateway configuration and upstream validation;
- stable exposed names such as `<upstream>.<tool>` and `<upstream>.<prompt>`;
- gateway-owned resource and resource-template URI namespaces;
- catalog aggregation with gateway metadata that preserves upstream details;
- route decisions for routed data-plane requests;
- gateway-level error normalization and upstream context;
- runtime state for configured upstreams, capability discovery, catalog caches,
  and shutdown bounds;
- a thin reference CLI that demonstrates and smoke-tests the library surface.

These are composition semantics. They should remain small enough for C++ hosts
to embed without also adopting a management platform.

## What Must Not Move Into Gateway Core

The gateway core and runtime must not grow into these responsibilities:

- general MCP protocol models or JSON-RPC framing;
- new transport implementations already owned by the SDK;
- single-client or single-server authoring APIs;
- profile stores, config migration, installers, or onboarding flows;
- GUI state, dashboards, or admin consoles;
- authentication workflows, authorization policy, audit stores, or tenant
  management;
- language bindings or general FFI surfaces;
- adaptive high-QPS proxy behavior unless the contract, scheduler, and SDK
  primitives are explicitly designed for it.

Optional control-plane features can exist later, but they belong in a separate
module, package, or host application. They must not change the transparent
data-plane defaults.

## Reference CLI Policy

The `cxxmcp-gateway` executable is a runnable reference CLI. It is allowed to
be useful for local development, smoke tests, demos, and simple sidecar-style
deployments.

It is not the main product surface. If CLI requirements start shaping routing
semantics, profile storage, daemon management, auth flows, admin APIs, or
installers, that work should move out of this repository or into an optional
consumer layer.

## Cross-Language Boundary

The current public API is a C++23 source API, not a stable binary ABI. It uses
C++ and SDK types that should not be frozen into a cross-language contract.

A C ABI can be valuable later, but only as a separate experimental component
with a narrow surface:

- opaque runtime handles;
- UTF-8 JSON strings for config, raw JSON-RPC, state, and errors;
- explicit allocation and free functions;
- documented blocking, callback, and shutdown semantics;
- no exposed C++ standard library or SDK protocol types.

Rust, Go, Python, or Node bindings should wrap that narrow C ABI, or use the
gateway as a process over its MCP endpoint. They should not bind directly to
`GatewayRuntime`.

## Positioning Rule

Use `cxxmcp-gateway` when the value is:

```text
multiple upstream MCP servers -> one routed downstream MCP endpoint
```

Use `cxxmcp` directly when the value is a single MCP client, a single MCP
server, a transport integration, or general protocol behavior.
