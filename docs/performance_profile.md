# Performance Profile

This document explains the current latency and throughput shape of
`cxxmcp-gateway`. It is a usage guide and release-evidence index, not a
service-level objective.

The gateway is optimized first for reusable MCP aggregation and routing. It is
not currently positioned as an adaptive high-QPS proxy, a hard real-time
runtime, or a replacement for direct SDK calls when an application only needs a
single upstream MCP server.

## Where Time Is Spent

Gateway routing is mostly namespace parsing, catalog lookup, capability checks,
and SDK call dispatch. For cached catalog reads this path is intentionally
small. The release baseline records cached `tools/list` calls in the
single-digit to low-double-digit microsecond range on the measured Windows
release build.

`tools/call` latency is dominated by upstream session and transport behavior:

- default per-call sessions create, initialize, use, and stop an SDK client
  service for each upstream operation;
- persistent sessions remove most repeated setup cost by keeping initialized
  sessions in a bounded per-upstream pool;
- Streamable HTTP calls still include SDK, transport, and upstream server work
  even when the gateway keeps a persistent session.

The `direct_sdk_persistent` row in [`release_baseline.md`](release_baseline.md)
measures an initialized SDK client calling the same Streamable HTTP fixture
without `GatewayRuntime`. It exists to separate gateway overhead from
SDK/transport/server cost.

## Session Modes

`UpstreamSessionMode::per_call` is the default. It is conservative and easy to
reason about: each upstream operation owns a fresh SDK client service
lifecycle. This is appropriate when low call volume, isolation, and simple
shutdown behavior matter more than repeated-call latency.

`UpstreamSessionMode::persistent` keeps a bounded pool of initialized sessions
per upstream. It reduces repeated-call setup cost and can allow same-upstream
concurrency up to `persistent_session_pool_size`, subject to the selected
transport and upstream server implementation.

Persistent mode is still a fixed pool. It is not adaptive multiplexing, does not
resize itself under load, and does not turn the gateway into a high-QPS runtime
by itself.

## Queueing And Shutdown Bounds

Two runtime options bound gateway-owned waiting points:

- `persistent_session_acquire_timeout` limits how long a call waits for a busy
  persistent pool slot.
- `active_call_drain_timeout` limits how long `GatewayRuntime::stop()` waits
  for accepted active upstream calls to drain.

These bounds make failure modes observable to hosts, but they do not cancel
active upstream work. Active request cancellation and progress forwarding remain
future capability work and must pass the capability extension gate before they
become advertised behavior.

## Interpreting The Baseline

Use [`release_baseline.md`](release_baseline.md) to compare material runtime or
SDK changes against the current release-candidate evidence. Refresh it when:

- the pinned SDK revision changes;
- upstream session lifecycle behavior changes;
- routing or catalog caching changes materially;
- persistent pool behavior changes materially.

Do not treat the baseline as a cross-platform guarantee. It is local evidence
for the measured commit, build type, SDK revision, transport fixtures, and host
environment.

## Consumer Guidance

Use the gateway when a C++ host needs one MCP endpoint backed by multiple
upstream MCP servers and wants reusable namespace, capability, routing, and
error behavior.

Prefer direct SDK integration when:

- there is only one upstream server and no aggregation requirement;
- the application has strict latency or throughput targets that require custom
  request scheduling, cancellation, or transport tuning;
- the application needs adaptive pooling, backpressure, or end-to-end service
  objectives that are not part of the current gateway contract.

For repeated calls through the gateway, start with persistent sessions,
configure a small explicit pool, set pool acquire and shutdown drain timeouts,
and measure with the application's real upstream servers.
