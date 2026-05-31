# Runtime API Contract

This document summarizes the public contract for hosts that embed
`cxxmcp-gateway` through `cxxmcp-gateway::runtime`.

The API is still pre-1.0, but the behavior below is part of the current
library-first release gate. Changes to these contracts should update tests and
the release checklist.

## Package and Headers

- Request the runtime component with
  `find_package(cxxmcp-gateway CONFIG REQUIRED COMPONENTS runtime)`.
- Link `cxxmcp-gateway::runtime`.
- Include `<cxxmcp/gateway/runtime.hpp>` for `GatewayRuntime`.
- Include `<cxxmcp/gateway/config.hpp>` for `GatewayConfig` and
  `validate_gateway_config()`.
- `<cxxmcp/gateway.hpp>` is intentionally core-only and does not imply that the
  runtime component is available.

## Construction

`GatewayRuntime` owns a validated snapshot of `GatewayConfig`.

- It is movable, not copyable.
- It does not start a hosted HTTP endpoint until `start_http()` is called.
- Disabled upstreams remain visible in `upstream_states()` but are not routed.
- Hosts should call `validate_gateway_config()` before construction or before
  exposing config errors to users. `start_http()` validates again before
  binding the endpoint.

## Session Modes

`GatewayRuntimeOptions::upstream_session_mode` controls upstream client
session lifecycle.

- `UpstreamSessionMode::per_call` is the default. Each routed upstream
  operation creates, initializes, uses, and stops an SDK client service.
- `UpstreamSessionMode::persistent` lazily keeps one initialized session per
  upstream. Same-upstream operations are serialized through that session.
- Persistent sessions are discarded after non-gateway-owned upstream failures
  and can reconnect on the next operation.
- `GatewayRuntime::stop()` closes retained persistent sessions.
- `clear_cached_catalogs()` does not close persistent sessions.

Persistent mode reduces repeated-call setup cost. It is not a connection pool,
does not multiplex multiple sessions per upstream, and should not be described
as a hard real-time or ultra-low-latency mode.

## Catalogs and Capabilities

The runtime exposes tools, resources, resource templates, prompts, and selected
completion routes.

- Successful aggregate `list_tools()`, `list_resources()`,
  `list_resource_templates()`, and `list_prompts()` results are cached.
- Cache misses fan out across eligible upstreams concurrently.
- The default catalog failure policy is fail-fast for the aggregate request:
  one upstream failure fails the whole list operation.
- `clear_cached_catalogs()` invalidates successful aggregate catalog caches.
- `refresh_upstream_capabilities()` initializes eligible upstreams, records
  their capabilities in `upstream_states()`, and clears aggregate catalog
  caches.
- `server_capabilities()` is side-effect-free. Before upstream discovery it
  advertises the configured MVP families. After all enabled upstream
  capabilities are known, it narrows advertisement to families supported by at
  least one initialized upstream.
- Hosted HTTP endpoints capture a capability snapshot at `start_http()`.
  Call `refresh_upstream_capabilities()` first when startup advertisement needs
  initialized upstream evidence.

The MVP does not advertise listChanged, resource subscriptions, tasks,
progress, cancellation forwarding, or logging control.

## Routing Names

- Tools use `<upstream>.<tool>`.
- Prompts use `<upstream>.<prompt>`.
- Resource and resource-template URIs are gateway-owned URIs created by
  `GatewayRouter::expose_resource_uri()` and
  `GatewayRouter::expose_resource_template_uri()`.
- Upstream ids must be non-empty and unique. They must not contain `.`,
  whitespace, or path separators.
- Tool and prompt names after the first `.` are preserved as upstream names.

## Lifecycle and Concurrency

`GatewayRuntime` supports concurrent data-plane calls.

- Different upstreams can run concurrently.
- Persistent same-upstream calls are serialized by the per-upstream session
  mutex.
- `stop()` is graceful. It marks the runtime stopping, rejects new data-plane
  work, waits for active upstream calls to drain, stops the hosted endpoint, and
  then marks upstreams stopped.
- Active calls are not cancelled by `stop()`. Transport timeouts are the
  current bound for slow upstream operations.
- `wait()` may overlap with `stop()`. It holds the hosted service alive while
  waiting.
- After `stop()`, side-effecting runtime APIs return runtime lifecycle errors.

The destructor calls `stop()`. Hosts should still call `stop()` explicitly when
they need to observe or handle shutdown errors.

## Observer Callbacks

Hosts can install `GatewayRuntimeObserver` in `GatewayRuntimeOptions`.

- Observer callbacks are synchronous.
- Observer exceptions are caught and ignored.
- Events include runtime stopping/stopped and upstream status changes.
- Observer callbacks may inspect runtime state, but should avoid long-running
  work and should not mutate runtime lifecycle. Queue work to a host-owned
  executor when heavy processing is needed.

Observer support exists so hosts can bridge status changes into their own
logging or metrics systems without adding a logging dependency to the gateway
libraries.

## Error Shape

Gateway errors use `mcp::core::Error`.

- Gateway-owned validation, core config validation, routing, and lifecycle
  errors use category `gateway`.
- Config file parsing helpers use category `gateway.config`.
- Upstream errors are annotated with the upstream id in `detail`.
- Upstream SDK or transport errors are normalized under
  `gateway.upstream.<category>` when they do not already use a gateway
  category.
- Timeout-like upstream errors are normalized under
  `gateway.upstream.timeout`.
- Upstream MCP errors preserve the upstream MCP error code and message.

Downstream JSON-RPC handling maps failed `Result` values into JSON-RPC error
responses using the same code, message, and detail.
