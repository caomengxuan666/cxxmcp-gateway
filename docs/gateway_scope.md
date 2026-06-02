# Gateway Scope and Boundaries

`cxxmcp-gateway` is a MCP data-plane gateway built on top of the
`cxxmcp` SDK. It should not become a management platform, a second SDK, or a
large compatibility layer around old gateway code.

The first stable boundary is capability aggregation and request routing. Other
control-plane features should be added only after this data plane is validated
with upstream integration fixtures and hosted client traffic.

The project is a library-quality reference gateway, not a product shell around
the SDK. The SDK owns protocol, transport, peer, service, and general
client/server behavior. The gateway owns only the reusable multi-upstream
composition semantics that turn several upstream MCP servers into one routed
downstream endpoint. The detailed SDK-versus-gateway boundary is maintained in
[`cxxmcp_integration_boundary.md`](cxxmcp_integration_boundary.md).

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
- optional `cxxmcp_gateway_config_io`;
- the optional `cxxmcp-gateway` reference runner;
- tests, examples, and documentation needed to validate the libraries.

The dependency direction is:

```text
gui / cli / external app -> runtime -> core -> cxxmcp SDK
```

Nothing in core or runtime may depend on CLI, GUI, product workflow, installer,
profile store, or admin-console code.

Nothing in core or runtime should duplicate general MCP SDK primitives such as
protocol models, JSON-RPC framing, transport implementations, single-client or
single-server authoring APIs, or SDK-owned lifecycle and liveness behavior.
When `cxxmcp` provides a general primitive such as cancellation, reconnect,
backpressure, transport authentication, or session pooling, the gateway should
consume that primitive instead of growing a parallel implementation.

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
server, a transport integration, or application-specific MCP behavior.

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

   The gateway aggregates upstream capabilities for the current routed data
   plane:

   - tool catalogs use stable `<upstream>.<tool>` names;
   - resource and resource-template catalogs use gateway-owned resource URIs;
   - prompt catalogs use stable `<upstream>.<prompt>` names;
   - completion support is advertised only from initialized upstream
     capability evidence.

   The pure catalog transformations live in core. Runtime owns fetching
   upstream catalogs and capability records, but not the naming, URI, or
   metadata rules.

4. Request routing

   The gateway routes supported data-plane requests back to the selected
   upstream: `tools/call`, `resources/read`, `prompts/get`, and selected
   `completion/complete` requests.

   Tool and prompt arguments should remain transparent. The gateway should not
   rewrite schemas or mutate caller-provided arguments unless a future feature
   explicitly owns that behavior. Resource and completion references are
   rewritten only according to their documented gateway namespaces.

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

### Config IO

File-based configuration lives in the separate `cxxmcp_gateway_config_io`
layer. The initial format is JSON and loads into `GatewayConfig`; YAML, TOML,
and other file formats are not supported yet. Environment-variable
substitution is supported only as an explicit library-consumer opt-in with
documented replacement rules. Hosted endpoint fields are not part of the file
format yet, so root-level
`host`, `port`, and `path` fields are rejected rather than ignored. The core
remains independent from the chosen file format.

Library consumers must be able to construct `GatewayConfig` directly without
pulling in a file parser. Config IO is an optional adapter for runners and
deployments, not part of the routing contract.

Disabled upstream entries are allowed to omit transport connection fields such
as stdio `command` or Streamable HTTP `uri`. These entries remain visible in
the normalized `GatewayConfig` and runtime state, but they are not routable and
do not contribute to advertised capabilities. When an upstream is enabled, the
transport-specific connection fields are required.

## Supported Capability Surface

Current MVP:

- `tools/list`: aggregated from enabled, tool-capable upstreams.
- `tools/call`: routed by exposed tool name.
- `resources/list`: aggregated from enabled, resource-capable upstreams with
  gateway-owned resource URIs.
- `resources/read`: routed by gateway resource URI.
- `resources/templates/list`: aggregated from enabled, resource-capable
  upstreams with gateway-owned resource URI templates.
- `prompts/list`: aggregated from enabled, prompt-capable upstreams.
- `prompts/get`: routed by exposed prompt name.
- `completion/complete`: routed for gateway prompt names and gateway resource
  template URIs when the target upstream supports completion.

Not yet supported:

- resource subscriptions and change notifications;
- prompt list-change notifications;
- tasks;
- logging capability and `logging/setLevel`;
- mutation workflows;
- upstream capability-change notifications;
- progress and cancellation forwarding.

The gateway must not advertise a capability until it can route that capability.
Unsupported request methods should fail predictably instead of being partially
proxied.
The current runtime leaves SDK-owned lifecycle and liveness requests such as
`initialize`, `ping`, and `server/discover` to the SDK. Other unsupported MCP
request methods are rejected with JSON-RPC `MethodNotFound`.
When the gateway initializes an upstream server as an MCP client, it advertises
no optional client capabilities by default. Roots, sampling, elicitation, and
task client capabilities must remain disabled until the gateway has explicit
forwarding and ownership rules for those flows.

Capability advertisement is runtime-owned. CLI flags, GUI settings, config
files, and control-plane state may request behavior, but they must not force
unsupported or unreachable capabilities into downstream MCP advertisement.

The current runtime advertises the tools capability only when at least one
upstream is enabled and the runtime can route `tools/list` and `tools/call`
requests. A gateway instance with no enabled upstreams does not advertise tools.
Resources and prompts follow the same runtime-owned rule for their implemented
list/read, templates/list, or list/get data-plane methods. Completion is
advertised only after initialized upstream capability records prove that at
least one enabled upstream supports `completion/complete`; it is never forced by
config alone. Prompt/resource list-change notifications, resource
subscriptions, tasks, progress, and cancellation remain unadvertised until their
routing behavior is implemented.

Embedded hosts can inspect the same runtime-owned advertisement decision through
`GatewayRuntime::server_capabilities()` before starting a hosted endpoint.
The method itself is side-effect-free: it must not start upstream processes or
network sessions. Before any enabled upstream has been initialized, the runtime
advertises the implemented MVP request families from the validated config. Once
all enabled upstreams have initialized capability records in runtime state, the
same method narrows `tools`, `resources`, and `prompts` advertisement and adds
`completion` advertisement from the families actually advertised by those
upstreams. Unsupported families such as tasks remain unadvertised.
When a complete capability cache is available, catalog listing methods skip
enabled upstreams that explicitly do not support that catalog family instead
of failing an advertised aggregate because of a capability-negative upstream.
Routed calls to a capability-negative upstream are rejected by the gateway
without opening a new upstream session.

Catalog listing methods cache successful aggregate results inside the runtime.
Repeated `tools/list`, `resources/list`, `resources/templates/list`, and
`prompts/list` calls return the cached aggregate until the host calls
`GatewayRuntime::clear_cached_catalogs()`, refreshes upstream capabilities, or
recreates the runtime. The gateway does not advertise or forward list-changed
notifications in the MVP, so cache invalidation is an explicit host decision.

Hosts that need narrowed advertisement before starting a hosted endpoint can
call `GatewayRuntime::refresh_upstream_capabilities()`. That API is explicitly
side-effecting: it initializes enabled upstreams, records their advertised
capabilities in runtime state, clears cached aggregate catalogs, and does not
fetch catalogs or route data-plane requests.

Hosted HTTP endpoints use the capability snapshot captured by
`GatewayRuntime::start_http()`. Capability discovery after the hosted endpoint
has started updates `GatewayRuntime::server_capabilities()`, but does not
change the `initialize` responses served by that already-running endpoint.

## Namespace Rules

The tool namespace is `<upstream>.<tool>`.

Upstream ids are stable, case-sensitive ASCII identifiers. The current
validated grammar is:

- non-empty;
- printable ASCII only;
- no `.` separator;
- no whitespace;
- no `/` or `\` path separator;
- at most 128 characters;
- unique within one `GatewayConfig`.

Tool names after the first `.` are preserved as upstream tool names.

Concrete resources use a separate gateway URI namespace instead of the tool
name grammar. The resource URI format is:

```text
cxxmcp-gateway-resource://<percent-encoded-upstream-id>/<percent-encoded-upstream-resource-uri>
```

The encoded upstream id is decoded and validated with the same upstream id
grammar as tools. The encoded upstream resource URI must decode to a non-empty
URI and is preserved in `_meta.gateway.upstreamResourceUri` when resource
catalogs are merged. Resource `name`, `title`, description, annotations, MIME
type, size, and other metadata are preserved; routing decisions use the gateway
resource URI, not the display name.

`resources/list` aggregates enabled upstream resource catalogs with the same
fail-fast policy as `tools/list`. `resources/read` routes by gateway resource
URI, forwards the decoded upstream URI, and rewrites returned content URIs into
the gateway resource URI namespace. Missing or disabled upstreams map to
`ResourceNotFound`.

`resources/templates/list` aggregates enabled upstream resource template
catalogs with the same fail-fast policy. Exposed resource template URIs use the
same gateway resource URI namespace while preserving URI template variables such
as `{path}`, so a client-expanded URI can still be routed through
`resources/read`.

Resource subscriptions, `resources/list_changed`, and `resources/updated`
forwarding are not part of this namespace contract yet. They require separate
ownership and notification rules before those resource sub-capabilities can be
advertised.

Prompt names use an explicit prompt namespace of `<upstream>.<prompt>`, with
the same upstream id grammar as tools. Prompt names after the first `.` are
preserved as upstream prompt names and `_meta.gateway.upstreamPromptName` when
prompt catalogs are merged. `prompts/list` aggregates enabled upstream prompt
catalogs with the same fail-fast policy as `tools/list`. `prompts/get` routes by
exposed prompt name and forwards arguments unchanged. Missing or disabled
gateway upstreams map to `InvalidParams` because the SDK protocol surface has no
standard prompt-not-found error code. `prompts/list_changed` forwarding is not
part of this contract yet.

Completion references reuse the existing namespaces. Prompt completions use
gateway prompt names such as `<upstream>.<prompt>` and are rewritten to the
upstream prompt name before forwarding. Resource completions use gateway
resource template URIs such as
`cxxmcp-gateway-resource://<upstream>/<template>` and are rewritten to the
upstream URI template before forwarding. Completion has no list-change
notification behavior.

Future subscriptions and long-running task ids may need their own namespace or
metadata rules. They must be defined separately before those capability
families are advertised, and must pass the
[`capability_extension_gate.md`](capability_extension_gate.md) checklist before
they enter the supported surface.

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
- unsupported non-SDK-owned notifications are ignored successfully by
  `GatewayRuntime`; they are not forwarded upstream and do not imply capability
  support.

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

The default Phase 2 decision is explicit per-call upstream sessions. Each
upstream list or call operation creates, initializes, uses, and stops its own
upstream SDK service. This keeps ownership simple while the data-plane behavior
is validated.

Hosts that need lower repeated-call latency can opt into persistent upstream
sessions through `GatewayRuntimeOptions`. The current persistent mode lazily
keeps a bounded initialized session pool per upstream, reuses initialized
capabilities, and closes retained sessions during `GatewayRuntime::stop()`.
The default pool size is one, which preserves serialized same-upstream
behavior. Larger explicit pool sizes allow same-upstream calls to use separate
initialized sessions up to the configured bound. It is not an adaptive
multiplexing layer and does not change the default per-call behavior.
Both Streamable HTTP and process-stdio upstreams support a configured
per-operation timeout; timeout failures are normalized as gateway upstream
timeout errors.

Aggregate catalog listing fans out eligible upstream list operations
concurrently and then applies the same whole-request failure policy: if any
enabled upstream returns a gateway-normalized error, the aggregate list fails.
This reduces multi-upstream catalog latency without changing the per-call
session model for an individual upstream operation.

The runtime exposes an upstream state snapshot for hosts and future admin APIs.
The current implementation reports configured upstreams, marks an
upstream `connecting`/`initialized` during a call, records initialized upstream
capabilities, marks successful calls `healthy`, marks failed calls `degraded`
with the last gateway-normalized error, exposes the number of active in-flight
gateway-accepted upstream operations, and marks upstreams `stopping`/`stopped`
during runtime shutdown. For persistent pools, `active_calls` includes calls
waiting for a pool slot; those waiters are woken during shutdown and return a
runtime stopping error instead of starting a new upstream operation. This is an
observable lifecycle contract; persistent mode uses a fixed per-upstream pool,
not an adaptive pooled connection manager.

`GatewayRuntime::stop()` is graceful for the current lifecycle model: it stops
the hosted downstream endpoint, rejects new upstream operations, waits for
already active upstream calls to finish, closes any persistent upstream
sessions, and then marks upstreams `stopped`. It does not yet cancel or
interrupt an active upstream call. Hosts that cannot tolerate unbounded
shutdown waits can set `active_call_drain_timeout`; when that timeout expires,
`stop()` returns a gateway-owned lifecycle error and the runtime remains
`stopping`, still rejecting new routed work. A later `stop()` call can complete
after active upstream calls drain. In the current lifecycle model, `stopped` is
terminal for a runtime instance; hosts should construct a new `GatewayRuntime`
to restart the data plane after it reaches `stopped`.

The raw JSON-RPC entry point follows the same lifecycle boundary for
gateway-routed methods. After `stop()`, routed requests such as `tools/list`,
`resources/list`, `prompts/list`, and `completion/complete` return a
gateway-owned `InvalidRequest` stopped error. SDK-owned lifecycle and liveness
requests, including `initialize`, `ping`, and `server/discover`, continue to
return no gateway response so the embedding SDK session layer retains
ownership.

For process-stdio upstreams, the per-call upstream SDK service is stopped at the
end of each upstream operation. Shutdown tests cover active stdio calls by
observing both a fixture child process marker and a slow-handler entry marker
during the call, then verifying that the child process marker is removed after
`GatewayRuntime::stop()` returns.

## Gateway Error Mapping

The stable error model is part of the gateway contract. The current
protocol-level mapping covers these cases:

| Case | Expected class |
| ---- | -------------- |
| Invalid exposed name | Invalid params |
| Unknown upstream | Tool not found or gateway routing error |
| Disabled upstream | Tool not found or gateway routing error |
| Missing upstream config | Invalid params or startup validation error |
| Upstream transport failure | Gateway upstream error |
| Upstream timeout | Gateway timeout error |
| Upstream protocol parse failure | Gateway upstream protocol error |
| Upstream MCP error | Preserve upstream code/message/detail text when possible |

Error details should preserve gateway context where possible: upstream id,
transport kind, exposed name, upstream method, upstream error code, upstream
message, and upstream detail text.

Current Phase 1 behavior preserves upstream SDK error code and message, prefixes
diagnostic detail text with the upstream id, and maps SDK categories under
`gateway.upstream.*` such as `gateway.upstream.transport`,
`gateway.upstream.timeout`, or `gateway.upstream.tool`.

## `tools/list` Failure Policy

The gateway documents one default aggregate catalog failure policy:

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
- `resources/list`, `resources/templates/list`, and `resources/read` routing
  through gateway-owned resource URIs;
- `prompts/list` and `prompts/get` routing with gateway prompt names;
- `completion/complete` routing when initialized upstream capabilities prove
  support;
- minimal CLI startup;
- installable CMake package.

Subscriptions, task APIs, progress, cancellation forwarding, policy, auth,
audit, and admin/control-plane APIs remain outside this surface until their
ownership and routing contracts are specified and tested.

## Validation Matrix

The current maturity gate is scoped to the library-first MVP surface above.
The concrete evidence index is maintained in
[`release_evidence.md`](release_evidence.md); the broader verified baseline is
summarized in [`technical_roadmap.md`](technical_roadmap.md).

1. Protocol lifecycle

   Covered for downstream initialize/initialized notification, ping,
   malformed and invalid JSON-RPC requests, requests before initialization,
   repeated initialization, unsupported methods and notifications, hosted
   endpoint startup/shutdown, wait-before-start rejection, and downstream
   session close while process-stdio or Streamable HTTP upstream calls are
   active.

2. Tool aggregation

   Covered for one upstream, multiple upstreams, duplicate exposed tool names,
   disabled upstreams, invalid and duplicate upstream ids, fail-fast catalog
   listing, cached catalog invalidation, capability-negative upstreams, and
   metadata preservation.

3. Tool calls

   Covered for successful routing, unknown exposed names, unknown upstreams,
   disabled upstreams, invalid arguments, upstream-returned MCP errors,
   timeout normalization, transport failure mapping, stopped/stopping runtime
   rejection, and raw JSON-RPC routed requests.

4. Transport failure

   Covered for process stdio command-not-found and early-exit paths,
   Streamable HTTP upstream unavailability, upstream timeouts, malformed stdio
   and HTTP upstream responses, per-call cleanup, persistent-session failure
   invalidation, failure isolation, reconnect, and pool timeout recovery.

5. Concurrency

   Covered for multiple hosted downstream clients, concurrent calls to one
   upstream, concurrent calls to multiple upstreams, concurrent aggregate
   catalog fan-out, default same-upstream serialization, configured
   same-upstream persistent pool concurrency, queued persistent pool calls,
   pool acquire timeout, and observable active/busy runtime state.

6. Shutdown

   Covered for idle shutdown, active-call shutdown with observable `stopping`
   state, active-call drain timeout, rejection of new routed work while
   stopping or stopped, raw JSON-RPC stopped/stopping errors, stdio child
   cleanup, persistent session cleanup, HTTP service shutdown, overlapping
   wait/stop, and observer lifecycle reentry.

7. Packaging

   Covered for build-tree consumption, install-tree `find_package`, versioned
   package discovery, component discovery and missing-component failures,
   static and shared library builds, component installs, subproject defaults,
   optional example builds, and the GitHub Actions Linux/macOS/Windows static
   and shared matrix.

8. Additional MCP capabilities

   Routed tools, resources, resource templates, prompts, and selected
   completion flows are part of the current MVP. Capability advertisement is
   owned by the runtime and is narrowed by initialized upstream capability
   records when available. Resource subscriptions, task APIs, progress
   forwarding, cancellation forwarding, and other MCP capabilities remain
   unadvertised until their namespace, advertisement, notification, ownership,
   and integration-test contracts are specified through the
   [`capability_extension_gate.md`](capability_extension_gate.md) checklist.

Active upstream cancellation is intentionally not part of this MVP. The current
contract is local cancellation/progress notification no-ops, downstream-close
state cleanup, and host-configurable wait bounds for queued pool calls and
shutdown drain.

## Design Rule

When a feature can be classified as data-plane routing, it belongs in the
gateway. When it is configuration management, user workflow, policy, or product
experience, it belongs outside the core and should wait until the gateway data
plane is proven.
