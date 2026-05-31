# Gateway Technical Roadmap

This roadmap defines how `cxxmcp-gateway` should grow without drifting into a
management platform or a second SDK.

The gateway is useful only if its data plane is correct, observable, and easy to
embed. Every phase below must keep that priority.

The project is library-first. `cxxmcp_gateway_core` and
`cxxmcp_gateway_runtime` are the primary artifacts. The `cxxmcp-gateway`
executable is a reference runner for development, smoke tests, and simple
sidecar-style deployments.

Repository direction:

```text
cxxmcp-gateway
  cxxmcp_gateway_core
  cxxmcp_gateway_runtime
  optional cxxmcp_gateway_config_io
  cxxmcp-gateway optional CLI
  docs / tests / examples

future cxxmcp-gateway-ui
  GUI or console product
  embeds public runtime APIs or talks to a gateway daemon admin API
```

The dependency direction is fixed:

```text
gui / cli / external app -> runtime -> core -> cxxmcp SDK
```

The CLI and future GUI are consumers of the gateway libraries. They must not
become owners of the MCP data-plane architecture.

## Target Architecture

`cxxmcp-gateway` sits between downstream MCP clients and upstream MCP servers.

```text
Downstream MCP client
  |
  | Streamable HTTP
  v
cxxmcp-gateway
  |
  +-- upstream MCP client --> process stdio MCP server
  +-- upstream MCP client --> Streamable HTTP MCP server
  +-- upstream MCP client --> future transport
```

To downstream clients, the gateway behaves as one MCP server. To upstream
servers, the gateway behaves as one or more MCP clients.

The gateway data plane owns aggregation and routing. The SDK owns protocol
types, peer/service lifecycle primitives, transports, and JSON-RPC machinery.

## Architectural Layers

### Core

Target: deterministic gateway behavior that can be tested without running a
hosted HTTP server or starting upstream processes.

Responsibilities:

- `GatewayConfig`;
- upstream validation;
- exposed name construction and parsing;
- catalog merge helpers such as `merge_tool_catalogs()`;
- request route decisions;
- gateway-level error mapping.

Anti-goals:

- no CLI parsing;
- no file format parsing;
- no profile or policy store;
- no logging framework dependency;
- no transport implementation duplication.
- no hidden process, network, or blocking side effects in APIs that look like
  pure routing operations.

### Runtime

Target: hosted gateway execution on top of SDK primitives.

Responsibilities:

- host Streamable HTTP endpoint;
- create and own upstream peers;
- manage upstream sessions;
- execute upstream `tools/list` aggregation and `tools/call` routing;
- pool or reuse upstream connections when validated;
- coordinate shutdown;
- surface runtime errors cleanly.

Anti-goals:

- no business logic that belongs in core;
- no custom JSON-RPC transport stack;
- no direct dependency on product/control-plane features.

### CLI

Target: minimal operational entry point.

Responsibilities:

- parse command-line options;
- construct `GatewayConfig`;
- start `GatewayRuntime`;
- print concise errors;
- return meaningful exit codes.

Anti-goals:

- no routing logic;
- no persistent state;
- no user profile management.

### Future Control Plane

Target: optional modules that configure or restrict the data plane.

Possible responsibilities:

- config file loading;
- auth integration;
- allow/deny lists;
- audit/event sinks;
- admin APIs;
- deployment templates.

These must remain outside the core routing layer until the data plane is proven.
They are blocked until tool routing, session lifecycle, error mapping, and the
supported-method matrix are stable. They must remain optional modules or
host-application responsibilities and must not change transparent routing
defaults silently.

Control-plane APIs must be separated from MCP data-plane endpoints. Admin APIs
may expose status, config reload, health, audit, policy, and UI backends, but
they must not be multiplexed into the MCP endpoint.

Future GUI modes:

- embedded runtime mode for local desktop and developer tools;
- daemon/admin API mode for production, server, enterprise, remote, and
  multi-user deployments.

Daemon/admin API mode is the preferred operational architecture because GUI
process lifecycle should not determine MCP data-plane availability.

## Phase 0: Reset and Compile

Status: current baseline.

Scope:

- remove old app/profile/policy/import-export implementation;
- remove bundled CLI and logging libraries;
- remove `tcb`;
- build as C++23;
- expose `cxxmcp_gateway_core`, `cxxmcp_gateway_runtime`, and
  `cxxmcp_gateway_cli`;
- install a consumable CMake package.

Acceptance criteria:

- clean C++23 configure/build against HTTP-enabled `cxxmcp`;
- `ctest` covers router basics and CLI smoke;
- install-tree `find_package(cxxmcp-gateway CONFIG REQUIRED)` works;
- source search shows no dependency on old `runtime/include`, `tools/cli`,
  `CLI11`, `spdlog`, or `tcb`.

## Phase 0.5: Library Packaging Contract

Goal: make the library-first promise true for C++ consumers before the data
plane grows.

Scope:

- make `BUILD_SHARED_LIBS` behavior real, or explicitly document static-only;
- add PIC/export-symbol strategy for static-as-dependency and future shared
  builds;
- define CMake components: `core`, `runtime`, optional `config_io`, and `cli`;
- make `find_package(cxxmcp-gateway COMPONENTS runtime)` fail clearly when the
  runtime component is unavailable;
- split or document umbrella headers so core-only consumers do not accidentally
  include runtime-only APIs;
- make CLI/tests default ON only for top-level builds and OFF when embedded as a
  subproject;
- keep CLI as an optional executable component, not a library API;
- provide build-tree package export or remove build-tree consumption from the
  contract;
- provide `validate_gateway_config()` or an equivalent normalized-config entry
  point in core.
- document split triggers for CLI and GUI.

Exit criteria:

- install-tree and build-tree package smoke tests are covered;
- consumers can link `cxxmcp-gateway::core` without pulling in CLI behavior;
- runtime linkage and component availability are explicit;
- public APIs distinguish pure routing decisions from process/network execution.

CLI split triggers:

- independent release cadence or compatibility matrix;
- heavy or platform-specific dependencies;
- shell framework, daemon manager, installer, or auto-update logic;
- profile store, config migration, or interactive onboarding;
- policy, auth, audit, admin API, or GUI state;
- CLI tests, docs, or packaging outgrowing reference-runner scope;
- CLI requirements shaping core/runtime APIs or defaults.

GUI split triggers:

- Qt, Electron, Tauri, web frontend, or another substantial UI stack;
- asset pipeline, signing, installer, auto-update, or platform packaging;
- dashboard, logs, metrics, profile manager, policy editor, or auth setup flow;
- multi-user or remote management;
- independent product release cadence.

## Phase 1: Tool Data Plane

Goal: make tools aggregation and routing correct enough for real use.

Scope:

- validate upstream ids and duplicate ids at startup;
- define stable exposed-name rules;
- aggregate `tools/list` from all enabled, tool-capable upstreams;
- preserve upstream metadata;
- route `tools/call`;
- normalize gateway errors;
- preserve upstream MCP errors when possible;
- add real upstream integration tests.
- define gateway `initialize` capability advertisement;
- advertise tools only when `tools/list` and `tools/call` routing are available;
- explicitly decide whether `tools/listChanged` is advertised; MVP should not
  advertise it until notification forwarding exists;
- document the current per-request upstream session limitation;
- record upstream initialization result and capabilities so they can influence
  downstream capability advertisement and routing behavior.

Supported MCP method matrix for Phase 1:

| Method or capability | Phase 1 behavior |
| -------------------- | ---------------- |
| `initialize` | SDK-owned downstream lifecycle; gateway capabilities must match routed methods |
| `ping` | SDK-owned |
| `tools/list` | Aggregated from enabled, tool-capable upstreams |
| `tools/call` | Routed by exposed tool name |
| `notifications/initialized` | SDK-owned |
| `notifications/tools/list_changed` | Not advertised and not forwarded in MVP |
| `resources/list` | Aggregated from enabled, resource-capable upstreams with gateway-owned resource URIs |
| `resources/read` | Routed by gateway resource URI |
| `resources/templates/list` | Aggregated from enabled, resource-capable upstreams with gateway-owned resource URI templates |
| resource subscriptions/listChanged/updated | Not advertised and not forwarded in MVP |
| `prompts/list` | Aggregated from enabled, prompt-capable upstreams |
| `prompts/get` | Routed by exposed prompt name |
| `notifications/prompts/list_changed` | Not advertised and not forwarded in MVP |
| tasks | Not advertised unless SDK and gateway routing support exist |
| `completion/complete` | Routed for gateway prompt names and gateway resource template URIs when upstream supports completion |
| `logging/setLevel` | Not advertised and not forwarded in MVP |
| `notifications/message` | Ignored successfully as an unsupported notification; not forwarded upstream |
| progress/cancellation | Not forwarded in MVP |
| other unsupported requests | JSON-RPC `MethodNotFound`, except SDK-owned lifecycle/liveness methods |
| other unsupported notifications | Ignored successfully; not forwarded upstream |

Runtime must generate downstream capability advertisement from real routed
behavior, current gateway implementation, upstream initialization results, and
namespace/routing reachability. CLI, GUI, config files, and control-plane state
may request capabilities, but they must not force unsupported capabilities into
the downstream MCP advertisement.
Upstream client initialization must follow the same boundary: the gateway must
not advertise optional client capabilities such as roots, sampling, elicitation,
or tasks until those flows have explicit forwarding and ownership semantics.

Required tests:

- one stdio upstream;
- one Streamable HTTP upstream;
- multiple upstreams;
- duplicate upstream tool names;
- disabled upstream;
- unknown exposed tool;
- upstream process not found;
- upstream HTTP unavailable;
- upstream timeout;
- upstream-returned MCP error.
- capability advertisement matches supported methods;
- unsupported methods fail predictably;
- malformed exposed names follow the error mapping contract.

Exit criteria:

- tool list and call behavior are documented;
- error codes and messages are stable enough for clients;
- integration tests cover both supported upstream transports.
- gateway error mapping table is implemented and tested;
- namespace grammar is validated;
- `tools/list` failure policy is documented and tested.

## Phase 2: Session and Lifecycle

Goal: make runtime behavior predictable under repeated traffic.

Scope:

- decide per-call connection versus pooled connection behavior;
- implement upstream session reuse if it improves latency and correctness;
- define upstream initialization and notification lifecycle;
- support graceful shutdown with active upstream sessions;
- ensure stdio child processes are cleaned up;
- add configurable upstream timeout.
- move upstream `Peer`/`Service` lifecycle ownership into runtime or a dedicated
  upstream connection manager;
- keep core limited to config validation, namespace rules, catalog merging, and
  route decisions.

Required tests:

- repeated calls to one upstream;
- concurrent calls to one upstream;
- concurrent calls to multiple upstreams;
- gateway shutdown while idle;
- gateway shutdown while calls are active;
- stdio process cleanup on stop.

Exit criteria:

- no leaked stdio child processes in normal shutdown paths;
- no known data races in core/runtime state;
- session behavior is documented.

## Phase 3: Additional MCP Capabilities

Goal: extend beyond tools without weakening tool-path correctness.

Candidate capabilities:

- resource subscriptions and change notifications;
- prompt list-change notifications;
- tasks, if supported by the SDK surface;
- other MCP capabilities when SDK support is mature and runtime advertisement
  can be driven by initialized upstream capabilities.

Rules:

- add one capability family at a time;
- start with list/read or list/get flows before mutation flows;
- define namespace rules separately for each capability family instead of
  reusing tool names by default;
- each capability needs integration tests before being treated as supported.
- each capability must define capability advertisement before implementation;
- capabilities that depend on upstream support, such as completion, require
  capability-aware upstream discovery or cache state before they can be
  advertised downstream;
- each capability must define namespace rules separately from tool names;
- each capability must define notification behavior;
- subscription ownership and cancellation must be explicit before resources or
  long-running tasks are advertised;
- upstream error detail text must be preserved across the gateway boundary.

Capability expansion checklist:

- supported methods;
- advertised capabilities;
- namespace or URI mapping;
- list/read/get/call routing behavior;
- changed/listChanged notification behavior;
- subscription ownership, if any;
- progress and cancellation, if any;
- error mapping and error detail-text preservation;
- integration tests for both stdio and Streamable HTTP upstreams.

Current resource routing baseline:

- concrete `resources/list` and `resources/read` use gateway-owned resource
  URIs of the form
  `cxxmcp-gateway-resource://<encoded-upstream>/<encoded-uri>`;
- the encoded upstream id is decoded and validated with the gateway upstream id
  grammar;
- the encoded URI must decode to a non-empty upstream resource URI;
- resource catalog merging preserves upstream resource metadata and adds
  `_meta.gateway.upstreamId` plus `_meta.gateway.upstreamResourceUri`;
- `resources/list` uses the same fail-fast upstream policy as `tools/list`;
- `resources/read` routes by gateway resource URI and maps missing or disabled
  upstreams to `ResourceNotFound`;
- gateway content results rewrite returned content URIs into the gateway
  resource URI namespace;
- `resources/templates/list` exposes upstream resource templates as gateway URI
  templates under the same gateway resource URI namespace while preserving URI
  template variables such as `{path}`;
- runtime advertises `resources` only when the config is valid and at least one
  upstream is enabled;
- resource subscriptions and resource change notifications remain out of scope
  until their ownership and routing rules are specified, so
  `resources/listChanged` and `subscribe` are not advertised.

Current prompt routing design:

- prompt list/get uses exposed prompt names of the form
  `<upstream>.<prompt>`, with the same upstream id grammar as tools;
- prompt names after the first `.` are preserved as upstream prompt names;
- prompt catalog merging preserves upstream prompt metadata and adds
  `_meta.gateway.upstreamId` plus `_meta.gateway.upstreamPromptName`;
- `prompts/list` uses the same fail-fast upstream policy as `tools/list`;
- `prompts/get` routes by exposed prompt name and forwards arguments
  unchanged;
- missing or disabled gateway upstreams map to `InvalidParams` because the
  current SDK protocol surface has no standard prompt-not-found error code;
- runtime advertises `prompts` only when the config is valid and at least one
  upstream is enabled;
- prompt list-change notifications remain out of scope, so
  `prompts/listChanged` is not advertised.

Current completion routing design:

- `completion/complete` routes prompt references that use exposed prompt names
  of the form `<upstream>.<prompt>`;
- `completion/complete` routes resource references that use gateway resource
  template URIs;
- prompt references are rewritten to upstream prompt names before forwarding;
- resource references are rewritten to upstream URI templates before
  forwarding;
- completion advertisement requires initialized upstream capability records
  proving completion support; config alone never forces completion into
  downstream capabilities;
- completion execution rejects initialized upstreams whose capability records do
  not advertise completion support, without marking the upstream degraded;
- completion has no list-change notification behavior.

Exit criteria:

- supported capability matrix is documented;
- unsupported methods return predictable gateway behavior;
- no capability is advertised unless routing is implemented.

## Phase 4: Configuration IO

Goal: make the gateway practical to operate without coupling core to a file
format.

Scope:

- add a separate config loading layer;
- choose one initial config format;
- validate config before runtime startup;
- support environment-variable substitution only if the rules are explicit;
- keep `GatewayConfig` as the internal normalized model.

Current implementation status:

- `cxxmcp_gateway_config_io` provides JSON-to-`GatewayConfig` loading;
- the CLI can use `--config <file>` when config IO is built;
- command-line upstream flags are appended to the loaded config and the merged
  config is validated before runtime startup;
- CLI endpoint flags (`--host`, `--port`, and `--path`) are not part of the
  config file format yet, and root-level endpoint fields with those names are
  rejected rather than silently ignored;
- environment-variable substitution is intentionally not implemented yet.

Exit criteria:

- invalid config fails before binding the HTTP endpoint;
- CLI flags and config file precedence are documented;
- config loader can be tested without starting the runtime.

## Phase 5: Policy, Auth, and Audit

Goal: add control-plane features only after routing is stable.

Possible scope:

- downstream authentication integration;
- upstream credential handling;
- tool allow/deny policy;
- audit events for list and call operations;
- rate limiting;
- data filtering or DLP hooks.

Rules:

- policy must be explicit and testable;
- default behavior should remain transparent routing;
- policy failures must be distinguishable from upstream failures;
- audit hooks must not require a logging framework dependency in core.

Exit criteria:

- policy behavior is documented with examples;
- tests cover allow, deny, and policy error cases;
- audit events have a stable schema.

## Phase 6: Operational Maturity

Goal: make the gateway reliable as a deployable component.

Scope:

- cross-platform CI for Windows, Linux, and macOS;
- package smoke tests for build-tree and install-tree consumption;
- release checklist;
- compatibility notes against supported `cxxmcp` SDK versions;
- performance baseline for `tools/list` and `tools/call`;
- basic observability hooks without binding core to a concrete logger.

Current operational gates are documented in
[`operational_gates.md`](operational_gates.md).
The release-candidate checklist is documented in
[`release_checklist.md`](release_checklist.md).

Exit criteria:

- release-blocking tests are defined;
- supported platform and compiler matrix is documented;
- performance regressions can be measured.

## Decision Rules

1. Prefer SDK primitives over local reimplementation.
2. Add a feature only when its gateway boundary is clear.
3. Keep core free of CLI, filesystem config, product workflow, and logging
   framework dependencies.
4. Treat integration tests as required for every new routed MCP capability.
5. Do not advertise a capability until the gateway can route it.
6. Control-plane features must not change data-plane defaults silently.
7. If a feature increases operational complexity, document its failure modes
   before merging it.
8. Keep MCP data-plane endpoints separate from admin/control-plane endpoints.
9. Keep CLI and GUI as consumers of gateway libraries, not owners of gateway
   architecture.
10. Split CLI or GUI into a separate package when their dependencies, release
    cadence, or product semantics exceed reference-runner or optional-consumer
    scope.

## Current Verified Baseline

The current MVP baseline has verified coverage for:

1. Library packaging contract: shared/static builds, package components,
   top-level versus subproject defaults, and build-tree/install-tree package
   smoke tests that configure, build, and run downstream consumers and the
   build-tree and installed CLI; unavailable `runtime`, `config_io`, and
   `cli` components fail clearly when requested; source dependency and hygiene
   guards cover removed legacy dependencies, forbidden legacy paths, conflict
   markers, and trailing whitespace.
2. Core/runtime split: core owns config validation, namespace rules, catalog
   merging, and route decisions; runtime owns SDK peer/service and upstream
   process/network execution.
3. Upstream config validation: empty ids, invalid id grammar, duplicate ids
   from direct config construction, JSON config loading, and merged file plus
   command-line upstream config, missing enabled transport parameters, invalid
   HTTP timeouts, structured field type mismatches, unsupported root-level
   endpoint fields, invalid HTTP and process-stdio timeouts, config file open
   failures, and malformed config JSON.
4. Tool data-plane integration: process stdio upstreams, Streamable HTTP
   upstreams, multiple upstreams, duplicate exposed tool names, unknown or
   disabled upstreams, unavailable upstreams, upstream timeouts, malformed
   stdio and HTTP upstream responses, and upstream-returned MCP errors.
5. Resource data-plane integration: process stdio upstreams, Streamable HTTP
   upstreams, multiple upstreams, gateway-owned resource URI and resource
   template URI routing, fail-fast resource catalogs, unknown or disabled
   upstreams, and upstream-returned MCP errors.
6. Prompt data-plane integration: process stdio upstreams, Streamable HTTP
   upstreams, multiple upstreams, exposed prompt-name routing, fail-fast prompt
   catalogs, unknown or disabled upstreams, and upstream-returned MCP errors.
7. Gateway error mapping for routing, transport, timeout, protocol, and
   upstream MCP failures, including direct coverage for gateway-owned error
   construction and upstream error annotation.
8. Runtime lifecycle decision: default explicit per-call upstream sessions,
   plus opt-in persistent bounded per-upstream session pools, with initialized
   upstream capabilities recorded in runtime state.
9. Graceful shutdown and concurrency coverage: repeated calls, concurrent calls
   to one upstream, concurrent calls to multiple upstreams, concurrent
   multi-upstream catalog list fan-out, idle shutdown,
   active-call shutdown with observable `stopping` state, downstream session
   close during an active upstream call, observable initialized state during
   active upstream calls, post-stop rejection for side-effecting runtime
   operations, in-flight stopping rejection for data-plane operations and
   capability refresh, raw JSON-RPC post-stop rejection for routed methods
   while SDK-owned lifecycle and liveness methods remain delegated,
   hosted endpoint option validation, wait-before-start rejection,
   overlapping wait/stop handling, observer lifecycle reentry,
   cancellation/progress notification no-ops, stdio child cleanup after
   successful per-call sessions and on stop, persistent stdio session reuse,
   default same-upstream serialization, configured same-upstream pool
   concurrency, active-call stop, failure invalidation, pool failure isolation,
   reconnect, and cleanup on stop, plus persistent HTTP default same-upstream
   serialization, pool queue draining, and timeout recovery.
10. Supported method and capability advertisement matrix for the routed tools,
   resources, and prompts MVP, including SDK-owned lifecycle/discovery request
   pass-through, unsupported request/notification behavior, unsupported
   notification no-op coverage, and serialized JSON shape for non-advertised
   sub-capabilities.
11. Completion data-plane integration: prompt completions and resource-template
    completions route through existing gateway namespaces for process stdio
    and Streamable HTTP upstreams, raw `completion/complete` requests route
    through the JSON-RPC handler, execution rejects completion-negative
    upstream capability caches, route-stage unknown/disabled upstream errors
    follow the prompt/resource namespace contracts, unsupported completion ref
    types are rejected, and hosted advertisement is gated by refreshed upstream
    capabilities.
12. Optional performance measurement tooling for stdio/HTTP cold and cached
    `tools/list`, per-call `tools/call`, opt-in persistent-session
    `tools/call`, and persistent HTTP pool pair calls, excluded from the
    default build and release-blocking CI.
13. Capability-aware advertisement refinement: `server_capabilities()` remains
    side-effect-free, uses config-based MVP advertisement before upstream
    discovery, narrows tools/resources/prompts plus completion advertisement
    once all enabled upstream capability records are cached, and unions
    advertised capability families across multiple initialized upstream caches;
    capability-negative upstreams are skipped for cached aggregate catalog
    listing and rejected locally for routed operations; hosted endpoints retain
    the capability snapshot captured at `start_http()`.
14. Explicit capability refresh API: hosts can call
    `GatewayRuntime::refresh_upstream_capabilities()` before `start_http()` to
    initialize upstream capability caches, clear cached aggregate catalogs,
    and avoid routing a data-plane request.
15. Runtime catalog cache: successful aggregate tools/resources/templates/
    prompts catalog lists are cached until explicit invalidation, capability
    refresh, or runtime recreation, while cache misses retain concurrent
    multi-upstream fan-out and whole-request failure semantics; explicit
    catalog invalidation does not close opt-in persistent upstream sessions.
16. Current local Release performance baseline recorded in
    [`release_baseline.md`](release_baseline.md) against a clean
    `caomengxuan666/cxxmcp` SDK revision.
17. Basic runtime observability hooks: library consumers can install a
    `GatewayRuntimeObserver` to receive runtime lifecycle and upstream status
    events without adding a logging framework dependency to core or runtime.
18. Release-readiness checklist documented in
    [`release_checklist.md`](release_checklist.md), including package
    consumption, component install, SDK revision, performance evidence, and
    public-contract gates; [`release_evidence.md`](release_evidence.md) maps
    those gates to concrete tests, documents, and commands.
19. Library onboarding and public runtime API docs: C++ hosts have a minimal
    integration guide in [`getting_started.md`](getting_started.md), and
    lifecycle, concurrency, session, cache, capability, observer, routing, and
    error-shape contracts are summarized in
    [`api_contract.md`](api_contract.md).
20. Compatibility policy: supported consumer shape, SDK revision boundaries,
    source/API versus ABI stability, CI platform matrix, and feature support
    limits are documented in [`compatibility.md`](compatibility.md).
21. Buildable embedded-runtime example: `examples/embedded_runtime_host.cpp`
    compiles behind `CXXMCP_GATEWAY_BUILD_EXAMPLES=ON` and demonstrates
    host-owned config construction, observer callbacks, capability prewarm,
    hosted startup, and opt-in persistent upstream sessions without changing
    default package components; `gateway_examples_build` covers the optional
    example build in CTest.

## Remaining Near-Term Backlog

1. Keep broadening lifecycle evidence where the SDK exposes stronger hooks,
   especially around persistent pool shutdown, timeout, and reconnect behavior.
2. Refresh the performance baseline whenever the release-candidate SDK revision
   or routing/runtime implementation changes materially.
3. Design the next routed MCP capability family only after its namespace,
   advertisement, notification behavior, integration tests, and upstream
   capability-discovery requirements are specified.
