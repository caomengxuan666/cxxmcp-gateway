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
  future cxxmcp_gateway_config_io
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
- define CMake components: `core`, `runtime`, future `config_io`, and `cli`;
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
- aggregate `tools/list` from all enabled upstreams;
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
| `tools/list` | Aggregated from enabled upstreams |
| `tools/call` | Routed by exposed tool name |
| `notifications/initialized` | SDK-owned |
| `notifications/tools/list_changed` | Not advertised and not forwarded in MVP |
| resources | Not advertised |
| prompts | Not advertised |
| tasks | Not advertised unless SDK and gateway routing support exist |
| completion | Not advertised |
| progress/cancellation | Not forwarded in MVP |
| other unsupported requests | JSON-RPC `MethodNotFound`, except SDK-owned lifecycle/liveness methods |
| other unsupported notifications | Ignored successfully; not forwarded upstream |

Runtime must generate downstream capability advertisement from real routed
behavior, current gateway implementation, upstream initialization results, and
namespace/routing reachability. CLI, GUI, config files, and control-plane state
may request capabilities, but they must not force unsupported capabilities into
the downstream MCP advertisement.

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

- resources;
- prompts;
- tasks, if supported by the SDK surface;
- completion or other MCP capabilities when SDK support is mature.

Rules:

- add one capability family at a time;
- start with list/read or list/get flows before mutation flows;
- use the same upstream namespace model unless there is a protocol reason not
  to;
- each capability needs integration tests before being treated as supported.
- each capability must define capability advertisement before implementation;
- each capability must define namespace rules separately from tool names;
- each capability must define notification behavior;
- subscription ownership and cancellation must be explicit before resources or
  long-running tasks are advertised;
- upstream error data must be preserved across the gateway boundary.

Capability expansion checklist:

- supported methods;
- advertised capabilities;
- namespace or URI mapping;
- list/read/get/call routing behavior;
- changed/listChanged notification behavior;
- subscription ownership, if any;
- progress and cancellation, if any;
- error mapping and error data preservation;
- integration tests for both stdio and Streamable HTTP upstreams.

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
  config file format yet;
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

## Current Near-Term Backlog

1. Fix library packaging contract: shared/static policy, package components,
   top-level CLI/test defaults, and build-tree package smoke.
2. Split pure core routing from runtime upstream execution so core APIs do not
   hide process or network side effects.
3. Add upstream config validation for empty ids, duplicate ids, and missing
   transport parameters.
4. Add integration fixtures for stdio and Streamable HTTP upstreams.
5. Define gateway error mapping for routing, transport, timeout, and upstream
   protocol failures.
6. Decide upstream connection lifecycle: per-call, pooled, or configurable.
7. Add graceful shutdown tests.
8. Implement and test supported method/capability advertisement matrix.
9. Add install-tree and build-tree package smoke to regular tests.
