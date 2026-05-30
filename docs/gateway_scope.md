# Gateway Scope and Boundaries

`cxxmcp-gateway` is a MCP data-plane gateway built on top of the
`cxxmcp` SDK. It should not become a management platform, a second SDK, or a
large compatibility layer around old gateway code.

The first stable boundary is capability aggregation and request routing. Other
control-plane features should be added only after this data plane is validated
with real upstream servers and client traffic.

## Project Identity

`cxxmcp-gateway` is library-first. The main reusable artifacts are gateway
libraries that can be embedded by C++ applications. The executable is a
reference runner for local development, smoke tests, demos, and simple
sidecar-style deployments.

The executable must remain thin. If adding a CLI feature would force routing,
policy, profile, config file, or observability behavior into the core library,
the feature belongs in an optional layer or in the host application instead.

## Repository Policy

This repository is the library-first gateway implementation. Its stable
artifacts are gateway libraries, not the CLI or a future user interface.

The repository owns:

- `cxxmcp_gateway_core`;
- `cxxmcp_gateway_runtime`;
- future `cxxmcp_gateway_config_io`;
- the optional `cxxmcp-gateway` reference runner;
- tests, examples, and documentation needed to validate the libraries.

The dependency direction is:

```text
gui / cli / external app -> runtime -> core -> cxxmcp SDK
```

Nothing in core or runtime may depend on CLI, GUI, product workflow, installer,
profile store, or admin-console code.

## CLI Policy

The CLI stays in this repository while it remains a thin reference runner.

It may:

- parse command-line flags;
- construct or load a `GatewayConfig` through stable APIs;
- start `GatewayRuntime`;
- print concise startup/runtime errors;
- serve as a local development, smoke-test, demo, or simple sidecar binary.

It must not own:

- MCP protocol behavior;
- routing policy;
- profile storage;
- config migration;
- daemon management;
- authentication;
- authorization policy;
- audit behavior;
- admin APIs;
- GUI state;
- long-term product semantics.

As a CMake subproject, CLI should default to `OFF`. As a top-level project, CLI
may default to `ON` so developers get a runnable reference binary.

The CLI should be split into a separate repository or package if it gains an
independent release cadence, heavy dependencies, platform installers, shell
frameworks, daemon management, interactive onboarding, profile/config migration,
or starts shaping core/runtime APIs.

## GUI and Control-Plane Policy

A future GUI is not part of the MCP data plane. It should default to a separate
repository or package, such as `cxxmcp-gateway-ui`.

The GUI may interact with the gateway in two supported modes:

- embedded mode: a local desktop or developer tool links the public runtime
  library and owns the process lifecycle;
- daemon/admin mode: a gateway daemon runs the MCP data-plane endpoint, while
  the GUI calls a separate admin/control-plane API.

Daemon/admin mode is preferred for production, server, enterprise, remote, and
multi-user deployments because UI crashes, upgrades, or restarts should not
interrupt the MCP data plane.

The MCP endpoint and admin/control-plane endpoint must be separate. The MCP
endpoint handles MCP protocol traffic. Admin/control-plane APIs handle status,
health, config reload, audit, policy management, and UI backends.

GUI or control-plane code must not directly mutate internal runtime state. It
must use validated configuration, lifecycle, status, and admin APIs. GUI
requirements must not change default transparent routing behavior or advertised
MCP capabilities.

A small development-only debug UI may live under `examples/` or `tools/`, but it
must default to `OFF`, must not be installed as part of the default package,
must not affect public headers, and must not add public dependencies to core or
runtime.

GUI work must be split into a separate repository or package if it introduces a
desktop/web UI stack, asset pipeline, installer, signing, auto-update, user
profile editor, auth setup flow, logs/metrics dashboard, policy editor,
multi-user management, or independent release cadence.

## When to Use the Gateway

Use the `cxxmcp` SDK directly when you are building one MCP client, one MCP
server, or application-specific MCP behavior.

Use `cxxmcp-gateway` when you need one downstream MCP endpoint backed by
multiple upstream MCP servers, stable exposed capability names, centralized
routing decisions, and gateway-level error mapping.

Use the gateway library when embedding this behavior into a larger product that
already owns configuration, authentication, policy, deployment, and
observability. Use the CLI app only as a reference runner, local development
tool, smoke-test binary, or minimal sidecar process.

## SDK vs Gateway

The SDK owns protocol types, transport primitives, peers, services, and
JSON-RPC machinery.

The gateway owns reusable multi-server behavior:

- upstream discovery and validation;
- capability catalog aggregation;
- exposed namespace rules;
- routing decisions;
- gateway-level errors;
- hosted gateway runtime lifecycle.

If a requirement is only "implement MCP", use the SDK. If a requirement is
"make several MCP servers look like one coherent endpoint", use the gateway.

## Core Responsibilities

1. Protocol ingress

   The gateway exposes MCP server endpoints for downstream clients. The first
   supported hosted endpoint is Streamable HTTP.

   SDK-owned protocol lifecycle behavior, such as `initialize`, initialized
   notification handling, `ping`, and basic JSON-RPC framing, should stay in the
   SDK. The gateway should delegate to SDK primitives instead of reimplementing
   protocol machinery.

2. Upstream connectivity

   The gateway connects to multiple upstream MCP servers. The initial supported
   upstream transports are process stdio and Streamable HTTP.

   Connection pooling, session reuse, health checks, retry policy, timeout
   policy, and graceful reconnection belong in the runtime layer after the basic
   routing path is proven.

3. Capability aggregation

   The gateway aggregates upstream capabilities. The initial capability surface
   is tools:

   - `tools/list` is collected from enabled upstreams.
   - Exposed tool names use the stable `<upstream>.<tool>` format.
   - Gateway metadata preserves the upstream id and original upstream tool name.

   The pure catalog transformation lives in core: upstream tool catalogs are
   converted into gateway-exposed tool definitions by `merge_tool_catalogs()`.
   Runtime owns fetching upstream catalogs, but not the naming or metadata
   rules.

4. Request routing

   The gateway routes `tools/call` back to the selected upstream based on the
   exposed tool name.

   Tool arguments should remain transparent. The gateway should not rewrite
   schemas or mutate caller-provided arguments unless a future feature explicitly
   owns that behavior.

5. Error normalization

   The gateway owns gateway-level errors:

   - invalid exposed tool names;
   - missing or disabled upstreams;
   - missing upstream configuration;
   - upstream transport failure;
   - upstream timeout;
   - upstream protocol failure.

   Upstream MCP errors should be preserved where possible and wrapped only when
   the caller needs gateway context to understand the failure.

6. Runtime shell

   The runtime starts and stops hosted gateway endpoints, owns upstream
   connection/session lifecycle, and provides clean shutdown behavior.

7. Minimal CLI

   The CLI exists to build a `GatewayConfig` and run `GatewayRuntime`. It should
   stay thin: argument parsing, process exit codes, and user-facing error text.
   Business logic belongs in core or runtime.

## Explicit Non-Responsibilities

The gateway should not include these features in the core data-plane layer:

- full management console behavior;
- profile management;
- policy engine;
- onboarding flows;
- import/export workflows;
- client configuration generation;
- custom logging framework binding;
- custom CLI framework binding;
- protocol model duplication already covered by the SDK.

These features may become separate control-plane modules later, but they should
not be used to justify complexity in the data-plane core.

Control-plane features must not enter core to make the first app more
convenient. Core should only know the normalized `GatewayConfig`, capability
catalogs, routing decisions, namespace rules, and gateway errors.

Future optional modules may own config files, profiles, auth, policy, admin
APIs, audit sinks, deployment templates, or product workflows. Host
applications may also own those features directly. They must not change
transparent data-plane defaults silently.

## Layering

### `cxxmcp_gateway_core`

Owns pure gateway behavior:

- gateway configuration model;
- upstream id and exposed name resolution;
- config validation;
- tool catalog merge, exposed metadata, and route decisions;
- gateway-level error mapping.

It must not start processes, open network connections, block on upstream MCP
traffic, or depend on CLI concerns, config file formats, local profile storage,
or control-plane workflows.

### `cxxmcp_gateway_runtime`

Owns hosted gateway execution:

- Streamable HTTP server endpoint startup;
- SDK `Peer` and `Service` integration;
- upstream lifecycle;
- upstream tool discovery and tool call execution over upstream sessions;
- wait and shutdown behavior.

It can depend on SDK runtime primitives. It should not duplicate SDK transport or
protocol internals.

### `cxxmcp_gateway_cli`

Owns command-line process behavior:

- parse command-line flags;
- construct `GatewayConfig`;
- start the runtime;
- report startup and runtime errors.

The CLI should not contain routing logic, protocol logic, or long-lived
management state.

### Future Config IO

File-based configuration lives in the separate `cxxmcp_gateway_config_io`
layer. The initial format is JSON and loads into `GatewayConfig`; YAML, TOML,
or environment substitution are not supported until their rules are explicit.
The core remains independent from the chosen file format.

Library consumers must be able to construct `GatewayConfig` directly without
pulling in a file parser. Config IO is an optional adapter for runners and
deployments, not part of the routing contract.

## Supported Capability Surface

Current MVP:

- `tools/list`: aggregated from enabled upstreams.
- `tools/call`: routed by exposed tool name.

Not yet supported:

- resources;
- prompts;
- tasks;
- completion;
- mutation workflows;
- upstream capability-change notifications;
- progress and cancellation forwarding.

The gateway must not advertise a capability until it can route that capability.
Unsupported request methods should fail predictably instead of being partially
proxied.

Capability advertisement is runtime-owned. CLI flags, GUI settings, config
files, and control-plane state may request behavior, but they must not force
unsupported or unreachable capabilities into downstream MCP advertisement.

The current runtime advertises the tools capability only when at least one
upstream is enabled and the runtime can route `tools/list` and `tools/call`
requests. A gateway instance with no enabled upstreams does not advertise tools.
Resources, prompts, tasks, completion, progress, and cancellation remain
unadvertised until their routing behavior is implemented.

Embedded hosts can inspect the same runtime-owned advertisement decision through
`GatewayRuntime::server_capabilities()` before starting a hosted endpoint.

## Namespace Rules

The initial tool namespace is `<upstream>.<tool>`.

Upstream ids should be stable, case-sensitive ASCII identifiers. Phase 1 must
define and validate the exact grammar before the gateway is considered stable.
The current grammar is:

- non-empty;
- printable ASCII only;
- no `.` separator;
- no whitespace;
- no `/` or `\` path separator;
- at most 128 characters;
- unique within one `GatewayConfig`.

Tool names after the first `.` are preserved as upstream tool names. Future
capabilities must not blindly reuse the tool namespace. Resource URIs, prompt
names, subscriptions, and long-running task ids may need different namespace or
metadata rules.

## Session and Notification Semantics

The downstream MCP lifecycle is SDK-owned, but the gateway must document the
observable behavior:

- requests before downstream initialization;
- repeated downstream initialization;
- whether downstream sessions share an upstream catalog;
- whether Streamable HTTP session ids affect routing;
- what happens to active upstream calls when a downstream session closes.

MVP notification policy:

- initialized notification handling is delegated to the SDK;
- upstream `tools/list_changed` is not forwarded yet;
- resource change notifications are not forwarded yet;
- progress and cancellation are not forwarded yet;
- unsupported notifications must be ignored or rejected according to a documented
  rule before the gateway advertises the related capability.

## Upstream Lifecycle Model

The target ownership model is:

- runtime owns upstream connection/session lifecycle;
- core owns routing decisions and namespace rules;
- router APIs in core should not hide process or network side effects.

The target upstream state machine is:

```text
configured -> connecting -> initialized -> healthy
                                |
                                v
                             degraded
                                |
                                v
                         stopping -> stopped
```

The current per-request upstream session behavior is an MVP implementation
constraint, not the final model. Phase 1 must document this limitation, and
Phase 2 must decide whether upstream sessions are per-call, pooled, or
configurable.

The current Phase 2 decision is explicit per-call upstream sessions. Each
upstream list or call operation creates, initializes, uses, and stops its own
upstream SDK service. This keeps ownership simple while the data-plane behavior
is validated. Pooling or reuse can be added later behind the runtime boundary
when its latency and shutdown tradeoffs are tested.

The runtime exposes an upstream state snapshot for hosts and future admin APIs.
The current per-call implementation reports configured upstreams, marks an
upstream `connecting`/`initialized` during a call, records initialized upstream
capabilities, marks successful calls `healthy`, marks failed calls `degraded`
with the last gateway-normalized error, exposes the number of active in-flight
upstream calls, and marks upstreams `stopping`/`stopped` during runtime
shutdown. This is an observable lifecycle contract; it is not yet a pooled
connection manager.

`GatewayRuntime::stop()` is graceful for the current per-call model: it stops
the hosted downstream endpoint, rejects new upstream operations, waits for
already active upstream calls to finish, and then marks upstreams `stopped`. It
does not yet cancel or interrupt an active upstream call. In the current
lifecycle model, `stopped` is terminal for a runtime instance; hosts should
construct a new `GatewayRuntime` to restart the data plane.

For process-stdio upstreams, the per-call upstream SDK service is stopped at the
end of each upstream operation. Shutdown tests cover active stdio calls by
observing a fixture child process marker during the call and verifying that the
marker is removed after `GatewayRuntime::stop()` returns.

## Gateway Error Mapping

The stable error model is part of the gateway contract. Phase 1 must define a
protocol-level mapping for at least these cases:

| Case | Expected class |
| ---- | -------------- |
| Invalid exposed name | Invalid params |
| Unknown upstream | Tool not found or gateway routing error |
| Disabled upstream | Tool not found or gateway routing error |
| Missing upstream config | Invalid params or startup validation error |
| Upstream transport failure | Gateway upstream error |
| Upstream timeout | Gateway timeout error |
| Upstream protocol parse failure | Gateway upstream protocol error |
| Upstream MCP error | Preserve upstream code/message/data when possible |

Error data should preserve gateway context where possible: upstream id,
transport kind, exposed name, upstream method, upstream error code, upstream
message, and upstream data.

Current Phase 1 behavior preserves upstream SDK error code and message, prefixes
diagnostic detail with the upstream id, and maps SDK categories under
`gateway.upstream.*` such as `gateway.upstream.transport`,
`gateway.upstream.timeout`, or `gateway.upstream.tool`.

## `tools/list` Failure Policy

The gateway must choose and document one default:

- fail-fast: one enabled upstream failure fails the whole `tools/list`; or
- partial list: successful upstream tools are returned with diagnostics for
  failed upstreams.

The current implementation is fail-fast. If partial listing is added later, it
must be explicit and tested so clients can reason about incomplete catalogs.

## MVP Surface

The first validated gateway surface is:

- C++23;
- no bundled third-party CLI or logging libraries;
- no `tcb` dependency;
- Streamable HTTP hosted gateway endpoint;
- process stdio upstreams;
- Streamable HTTP upstreams;
- `tools/list` aggregation;
- `tools/call` routing;
- minimal CLI startup;
- installable CMake package.

Anything beyond this should be added only after tests demonstrate that the
existing surface is stable.

## Validation Matrix

The gateway should not be considered mature until these paths are covered:

1. Protocol lifecycle

   - downstream initialize;
   - initialized notification;
   - ping;
   - invalid JSON-RPC requests.
   - requests before initialization;
   - repeated initialization;
   - downstream session close with active upstream calls.

2. Tool aggregation

   - one upstream;
   - multiple upstreams;
   - duplicate upstream tool names;
   - disabled upstreams;
   - empty upstream ids;
   - metadata preservation.

3. Tool calls

   - successful call;
   - unknown exposed tool name;
   - unknown upstream;
   - disabled upstream;
   - invalid tool arguments;
   - upstream-returned MCP error.

4. Transport failure

   - stdio command not found;
   - stdio process exits early;
   - HTTP upstream unavailable;
   - upstream timeout;
   - malformed upstream response.

5. Concurrency

   - multiple downstream clients;
   - concurrent calls to one upstream;
   - concurrent calls to multiple upstreams;
   - cancellation behavior.

6. Shutdown

   - stop while idle;
   - stop with active upstream sessions;
   - stdio process cleanup;
   - HTTP service shutdown.

7. Packaging

   - build-tree consumption;
   - install-tree `find_package`;
   - static linking;
   - Windows, Linux, and macOS.

8. Future MCP capabilities

   Resources, prompts, tasks, and other MCP capabilities should be added
   incrementally after the tool path is validated.

## Design Rule

When a feature can be classified as data-plane routing, it belongs in the
gateway. When it is configuration management, user workflow, policy, or product
experience, it belongs outside the core and should wait until the gateway data
plane is proven.
