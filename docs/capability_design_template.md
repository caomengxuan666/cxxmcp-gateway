# Capability Design Template

Copy this template for any PR that proposes a new routed MCP capability family.
Do not enable runtime advertisement until every section has a concrete answer
and matching evidence is listed in `release_evidence.md`.

## Capability Summary

- Capability family:
- MCP methods in scope:
- Explicitly unsupported methods:
- Public API additions:
- Package component affected:

## Namespace And Routing

- Downstream name, id, URI, or reference shape:
- Upstream id extraction rule:
- Upstream value reconstruction rule:
- Collision handling:
- Unknown upstream behavior:
- Disabled upstream behavior:

## Advertisement

- `server_capabilities()` behavior before upstream discovery:
- `server_capabilities()` behavior after capability refresh:
- Hosted `initialize` snapshot behavior:
- Capability-negative upstream behavior:
- Config, CLI, GUI, or admin state influence:

## Upstream Discovery

- Required upstream capability fields:
- Behavior when an upstream omits the capability field:
- Behavior when only some enabled upstreams support the family:
- Whether refresh is required before execution:

## Catalog And Cache

- Aggregate list/catalog methods:
- Cacheable successful results:
- Cache invalidation triggers:
- Fail-fast or partial-result policy:
- Duplicate exposed-name or URI behavior:

## Execution And Errors

- Per-upstream request mapping:
- Timeout behavior:
- Transport failure mapping:
- Malformed response mapping:
- Upstream MCP error preservation:
- Gateway-owned route error shape:

## Notifications

- Notifications forwarded upstream:
- Notifications accepted as local no-ops:
- Notifications rejected:
- Cache or runtime-state effects:

## Lifecycle And Concurrency

- Behavior before downstream initialization:
- Behavior while runtime is `stopping`:
- Behavior after `stop()`:
- Behavior when downstream session closes:
- Active upstream work ownership:
- Same-upstream concurrency model:

## Cancellation And Progress

- Request ownership model:
- Cancellation propagation behavior:
- Progress forwarding behavior:
- Behavior when cancellation is unsupported:
- Required SDK hooks:

## Observability

- `upstream_states()` additions:
- Observer events:
- Error detail required for host diagnostics:
- Metrics a host can derive without a logging dependency:

## Required Evidence

- Core namespace and route tests:
- Process-stdio runtime integration tests:
- Streamable HTTP runtime integration tests:
- Multi-upstream aggregation tests:
- Duplicate exposed-name or URI tests:
- Unknown, disabled, unavailable, timeout, malformed response, and upstream MCP
  error tests:
- Capability advertisement tests before and after refresh:
- Raw JSON-RPC routed request tests:
- Unsupported request and notification tests:
- Active-call, `stopping`, post-stop, and downstream-close tests:
- Hosted endpoint tests:
- Package or example tests for new public API:

## Release Notes

- User-visible behavior:
- Compatibility risk:
- Performance or lifecycle risk:
- Documentation updates:
