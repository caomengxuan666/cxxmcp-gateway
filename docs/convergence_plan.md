# Convergence Plan

This plan turns the positioning decision into implementation work. The goal is
to keep `cxxmcp-gateway` converged on a small, reusable data-plane library
instead of drifting into a product, a second SDK, or a cross-language runtime.

## Target State

The supported shape is:

```text
cxxmcp SDK
  protocol / transport / peer / service / typed MCP helpers

cxxmcp-gateway core
  pure multi-upstream config, namespace, catalog, route, and error behavior

cxxmcp-gateway runtime
  hosted data-plane wiring over cxxmcp primitives

optional config_io
  JSON adapter into GatewayConfig and GatewayRuntimeConfig

optional cxxmcp-gateway CLI
  runnable reference runner and simple sidecar
```

The project is successful when a C++ host can embed the gateway to expose one
downstream MCP endpoint backed by multiple upstream servers, without also
adopting product workflow, control-plane state, or a second SDK surface.

## Implementation Priorities

### 1. Enforce The Boundary

Current status: started.

Required work:

- keep `docs/cxxmcp_integration_boundary.md` and
  `docs/positioning_guardrails.md` linked from README and release evidence;
- extend source hygiene tests so forbidden product, SDK-duplication, and FFI
  paths cannot appear silently;
- enforce that gateway core and public headers do not include SDK runtime
  primitives such as peer, service, server, client, or transport headers;
- keep the CMake component set limited to `core`, `runtime`, optional
  `config_io`, optional `cli`, optional examples, and optional perf tools.

Done when:

- `gateway_source_dependency_guard` fails for accidental product/control-plane
  paths;
- docs list every accepted route for work that does not belong in core/runtime.

### 2. Keep Gateway Logic Above cxxmcp

Current status: partially true.

Required work:

- preserve SDK ownership of protocol types, serialization, JSON-RPC, peer,
  service, and transport behavior;
- consume future SDK primitives for reconnect, cancellation, backpressure,
  transport auth, and pooling instead of cloning them;
- keep gateway-owned code focused on multi-upstream composition.

Done when:

- core has no process, network, or SDK service side effects;
- runtime delegates protocol and transport behavior to `cxxmcp`;
- every new runtime primitive documents why it belongs above the SDK.

### 3. Harden The Data Plane Before Expanding It

Current status: MVP implemented.

Required work:

- keep supported routed families limited to tools, resources, resource
  templates, prompts, and selected completion;
- avoid advertising unsupported families;
- improve lifecycle and shutdown behavior within the existing contract;
- add stronger tests before changing catalog failure policy, cancellation,
  progress, subscriptions, tasks, or list-change behavior.
- use `docs/partial_catalog_results_design.md` as the design gate before
  changing the current fail-fast catalog policy.

Done when:

- release evidence maps every advertised capability to tests;
- unsupported methods stay unadvertised and fail predictably;
- lifecycle state and shutdown bounds are documented and tested.

### 4. Improve Reference Use Without Productizing

Current status: one embedded example and a thin CLI.

Required work:

- keep the CLI a reference runner;
- add focused examples for common embedding and sidecar shapes;
- keep `examples/multi_upstream_namespace.cpp` as the core-only proof that
  gateway value starts with namespace and routing composition;
- avoid profile stores, installers, dashboards, auth workflows, policy stores,
  audit stores, and admin APIs in this repository.

Done when:

- examples demonstrate real gateway integration without creating product
  workflow state;
- CLI additions do not affect core/runtime routing semantics.

### 5. Defer Cross-Language Work

Current status: explicitly unsupported.

Required work before any C ABI:

- create a design record;
- define opaque handles, JSON/text boundaries, error ownership, memory
  ownership, callback threading, blocking behavior, and versioning;
- keep the component experimental and separate from the C++ source API.

Done when:

- no language binding depends on `GatewayRuntime` directly;
- any C ABI has its own tests and compatibility policy.

## Near-Term Task Queue

1. Add source guardrails for product/control-plane/FFI paths.
2. Add a small SDK-boundary check to the source hygiene test.
3. Keep the focused multi-upstream namespace example building as proof that
   gateway value starts with composition semantics, not transport or protocol
   reimplementation.
4. Split internal runtime implementation only when it reduces real coupling.
   `docs/runtime_internal_boundaries.md` records the first candidates:
   upstream sessions, catalog aggregation/cache, raw JSON-RPC dispatch, and
   runtime state.
5. Add explicit design records before implementing any capability from the
   "separate decision" list in `positioning_guardrails.md`.

## Non-Goals For This Line

- enterprise gateway product;
- management console or dashboard;
- auth, RBAC, tenant, audit, or policy product layer;
- stable binary ABI;
- Rust, Go, Python, or Node SDK;
- replacement for `cxxmcp` protocol, transport, or service APIs.

## Completion Check

Before calling a change aligned with this plan, verify:

- it composes multiple upstream MCP servers or supports that data-plane;
- it does not duplicate a general `cxxmcp` primitive;
- it keeps CLI/config/example code out of core;
- it has a test or release-evidence entry appropriate to its risk;
- it does not advertise an MCP capability before route behavior exists;
- it does not add product or FFI surface without a separate design record.
