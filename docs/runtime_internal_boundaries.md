# Runtime Internal Boundaries

Status: planning document. Public API changes are not implied.

`gateway/src/runtime.cpp` currently owns hosted endpoint setup, upstream peer
construction, per-call sessions, persistent session slots, catalog fan-out,
catalog caching, raw JSON-RPC dispatch, upstream state, observers, and shutdown
coordination. That is acceptable for the current MVP, but it is the main place
where future work could drift into a second SDK or an unbounded product runtime.

This document defines the first internal split candidates. The goal is to
reduce coupling without changing the public `GatewayRuntime` API.

## Split Rules

- Do not introduce new public headers until there is a consumer need.
- Keep internal components under the runtime implementation boundary.
- Do not move SDK protocol or transport ownership out of `cxxmcp`.
- Do not add product control-plane behavior while splitting code.
- Preserve existing tests before changing behavior.

## Candidate 1: Upstream Session Execution

Current responsibility:

- build upstream `ClientPeer` values;
- start SDK services;
- initialize and notify upstream sessions;
- run per-call operations;
- manage persistent session slots;
- discard failed sessions;
- report upstream state transitions.

Target internal component:

```text
UpstreamSessionExecutor
  input: UpstreamServer, session mode/options, operation callback
  output: Result<T>, upstream state events
```

Why it belongs in runtime:

- it has process/network side effects;
- it depends on SDK peer/service/transport primitives;
- it is not pure routing behavior.

Do not split this into core.

## Candidate 2: Catalog Aggregation

Current responsibility:

- select enabled and capability-eligible upstreams;
- fan out list operations;
- collect failures;
- merge successful catalogs with core helpers;
- store successful aggregate caches.

Target internal component:

```text
CatalogAggregator
  input: upstream list, capability snapshot, list operation
  output: merged catalog or gateway error
```

Why it should be split:

- the same pattern exists for tools, resources, resource templates, and prompts;
- partial result policy will otherwise duplicate logic in four places;
- cache behavior needs a clear boundary before it grows more modes.

## Candidate 3: Runtime Request Adapter

Current responsibility:

- map raw JSON-RPC methods into typed runtime calls;
- serialize typed results back to JSON-RPC responses;
- return `std::nullopt` for SDK-owned lifecycle and liveness methods;
- reject unsupported gateway-owned methods.

Target internal component:

```text
GatewayRequestAdapter
  input: JsonRpcRequest / JsonRpcNotification
  output: optional JsonRpcResponse or Unit
```

Why it should be split:

- it is protocol-adapter glue, not session lifecycle;
- it is the main boundary for future routed method families;
- it can keep unsupported methods explicit without touching session code.

## Candidate 4: Runtime State And Observer Dispatch

Current responsibility:

- track upstream status, active calls, capabilities, persistent pool counters,
  and last error;
- dispatch observer callbacks synchronously;
- expose snapshots through `upstream_states()`.

Target internal component:

```text
RuntimeStateStore
  input: state transition commands
  output: snapshots and observer events
```

Why it should be split:

- it is shared by session execution, catalog aggregation, shutdown, and host
  diagnostics;
- observer threading semantics need one documented implementation point.

## First Safe Refactor

The first code refactor is mechanical and behavior-preserving:

1. Extract catalog cache storage helpers or a small `CatalogCache` owner.
2. Keep tests passing after the extraction.
3. Extract catalog fan-out only after cache ownership is isolated.
4. Leave persistent sessions in place until catalog code is no longer mixed
   with raw JSON-RPC dispatch.

This sequence keeps the data-plane behavior stable while making room for
partial catalog design work and lifecycle hardening.

Current status:

- catalog cache ownership has started moving into an internal
  `RuntimeCatalogCache` helper;
- catalog fan-out, persistent sessions, raw JSON-RPC dispatch, and runtime
  state remain in `GatewayRuntime::Impl`;
- no public API or behavior change is implied by this extraction.
