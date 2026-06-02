# Partial Catalog Results Design Record

Status: design only. Not implemented in the current runtime.

The current gateway catalog policy is fail-fast: if any enabled upstream fails
during `tools/list`, `resources/list`, `resources/templates/list`, or
`prompts/list`, the aggregate request fails. This is simple and already tested,
but it is not ideal for long-running gateways where one degraded upstream
should not hide every healthy upstream.

This record defines the boundary that must be resolved before partial catalog
results are implemented.

## Problem

A multi-upstream gateway has different availability expectations than a single
SDK client:

- a healthy upstream should usually remain visible when another upstream is
  temporarily down;
- downstream clients need to understand whether a catalog is complete;
- route decisions for already-known exposed names must remain predictable;
- cached partial results must not silently become indistinguishable from full
  results.

The gateway cannot simply drop failed upstreams without exposing diagnostics.
That would make downstream clients believe the aggregate catalog is complete.

## Current Behavior

The current behavior remains:

- list operations fan out to eligible enabled upstreams;
- the first gateway-normalized upstream error fails the aggregate list;
- successful full aggregate results may be cached;
- unsupported or capability-negative upstreams are skipped only when runtime
  capability records prove they do not support that catalog family.

This behavior is the release-candidate contract until this design is accepted
and implemented.

## Proposed Shape

Partial catalog mode should be explicit. It should not replace fail-fast as a
silent behavior change.

Potential API shape:

- keep current typed APIs fail-fast by default;
- add an opt-in runtime option such as `CatalogFailurePolicy::partial`;
- include diagnostics in gateway-owned metadata when the protocol result type
  permits metadata;
- expose host-side diagnostics through runtime observer events and upstream
  state snapshots.

Potential JSON metadata shape:

```json
{
  "_meta": {
    "gateway": {
      "partial": true,
      "failedUpstreams": [
        {
          "id": "git",
          "category": "gateway.upstream.transport",
          "message": "upstream transport failed",
          "detail": "upstream 'git': connection refused"
        }
      ]
    }
  }
}
```

The exact metadata shape must match the underlying `cxxmcp` protocol support.
If a protocol result does not permit result-level metadata, the gateway must
not invent a non-standard response shape for typed APIs.

## Required Decisions

Before implementation, decide:

- whether partial mode is global or per catalog family;
- whether typed APIs return partial diagnostics directly or only expose them
  through observer/state APIs;
- whether cached partial catalogs are allowed;
- how hosts invalidate partial caches;
- whether a partial catalog can satisfy capability advertisement expectations;
- what JSON-RPC error or metadata shape downstream clients see;
- how a failed upstream recovers from degraded to healthy after a later
  successful catalog or routed call.

## Required Tests

Implementation must add coverage for:

- one healthy upstream and one failing upstream in partial mode;
- all upstreams failing in partial mode;
- fail-fast behavior remaining the default;
- cache behavior for partial results;
- observer and `upstream_states()` diagnostics;
- tools, resources, resource templates, and prompts separately;
- hosted JSON-RPC responses and typed runtime APIs;
- capability-negative upstreams versus transport-failing upstreams.

## Non-Goals

This design does not add:

- automatic health checks;
- retry policy;
- circuit breakers;
- background catalog refresh;
- admin APIs;
- product dashboards.

Those may be useful later, but they are control-plane or operational features
and should not be smuggled into the data-plane catalog policy.
