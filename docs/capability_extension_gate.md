# Capability Extension Gate

This gate is required before adding or advertising any new routed MCP
capability family beyond the current tools, resources, prompts, and selected
completion surface.

It is intentionally a design gate, not an implementation plan. A future
capability PR should update this document or link to a capability-specific
design that answers every item below before runtime advertisement is enabled.
Use [`capability_design_template.md`](capability_design_template.md) as the
starting point for that capability-specific design.

## Required Decisions

- Capability family and methods: list every request and notification method in
  scope, and explicitly mark unsupported methods.
- Namespace: define how downstream names, ids, URIs, or references map to one
  upstream, and how collisions are avoided.
- Advertisement: define exactly when `server_capabilities()` and hosted
  `initialize` responses advertise the capability. Config, CLI, GUI, or admin
  state must not force advertisement without implemented routing.
- Upstream discovery: define whether initialized upstream capability records
  are required before advertisement or execution, and how capability-negative
  upstreams are skipped or rejected.
- Catalog/cache behavior: define whether successful aggregate results are
  cached, how caches are invalidated, and whether unsupported notification
  methods affect cache state.
- Routing behavior: define default failure policy for aggregate calls,
  per-upstream execution, timeout handling, and upstream MCP error mapping.
- Notification behavior: define forwarding, local no-op, or explicit rejection
  for every notification in the family.
- Lifecycle behavior: define behavior before downstream initialization, during
  `stopping`, after `stop()`, when a downstream session closes, and during
  active upstream work.
- Cancellation and ownership: define request ownership and cancellation rules
  before advertising subscriptions, progress, cancellation, or long-running
  task-style flows.
- Observability: define the runtime state, observer events, or diagnostics
  hosts can rely on.

## Required Evidence

- Core tests for namespace parsing, route decisions, and gateway-owned errors.
- Runtime integration tests for process-stdio upstreams.
- Runtime integration tests for Streamable HTTP upstreams.
- Multi-upstream aggregation and duplicate-name or duplicate-id behavior.
- Unknown, disabled, unavailable, timeout, malformed response, and upstream MCP
  error cases.
- Capability-aware advertisement tests before and after upstream capability
  refresh.
- Raw JSON-RPC handler tests for routed requests, unsupported requests, and
  lifecycle rejection.
- Notification tests for active calls, `stopping`, and post-stop behavior.
- Hosted endpoint tests when the capability is advertised downstream.
- Package or example coverage when new public API is added.

## Release Rule

A capability family remains future-only until all required decisions are
documented and all required evidence is mapped in `release_evidence.md`.
Partial implementation must stay unadvertised.
