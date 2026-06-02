# Positioning Guardrails

This document keeps `cxxmcp-gateway` aligned with its intended shape:

```text
library-first MCP data-plane gateway
with a runnable reference CLI
built on top of cxxmcp
```

It exists to prevent the project from drifting into a second SDK, a product
control plane, or a generic cross-language runtime.

## Product Shape

The project should be described as:

- a C++ library-first MCP gateway;
- a library-quality reference implementation of multi-upstream aggregation on
  top of `cxxmcp`;
- a reusable data-plane component for C++ hosts;
- a package with an optional runnable reference CLI.

The project should not be described as:

- an enterprise MCP gateway product;
- a management console;
- a Redis-like local daemon or middleware product;
- a replacement for `cxxmcp`;
- a general MCP SDK for other languages;
- a stable C ABI or FFI runtime.

## Reference Projects

External gateway projects are useful for orientation, but most of them compete
in product and control-plane territory. They should not directly set this
repository's scope.

| Project shape | Useful lesson | Boundary for this repository |
| --- | --- | --- |
| Kubernetes or platform gateway | Deployment, authorization, and server lifecycle are real product needs. | Keep those needs outside `core` and `runtime`; use separate product/control-plane repos if needed. |
| Registry or governance gateway | Catalog governance, audit, policy, and observability can be valuable. | Do not add policy stores, audit pipelines, or admin dashboards to this data-plane library. |
| Desktop/dashboard MCP manager | Developer experience matters for configuring and observing multiple servers. | Keep GUI/dashboard work out of this repository; a future UI should consume public runtime or admin APIs. |
| Hosted commercial gateway | Access control, logs, analytics, and tenant boundaries drive commercial value. | Those are product layers, not the current library contract. |
| `cxxmcp` SDK | Protocol, transport, peer/service lifecycle, and typed MCP helpers belong in the SDK. | Gateway consumes SDK primitives and adds only multi-upstream composition semantics. |

The best positive reference is not a full product gateway. It is a small,
well-tested, embeddable data-plane library with a runnable reference binary.

## Allowed Near-Term Work

Near-term work should improve the data-plane library without changing the
project identity:

- document the `cxxmcp` integration boundary and SDK compatibility matrix;
- keep `core`, `runtime`, optional `config_io`, and optional CLI components
  cleanly separated;
- harden multi-upstream aggregation and routing behavior;
- improve runtime lifecycle, shutdown bounds, and upstream state reporting;
- add focused examples that demonstrate C++ embedding and simple sidecar use;
- add tests for every supported routed MCP capability;
- keep unsupported MCP capability families unadvertised;
- keep package, install, and subproject behavior reliable.

## Work That Requires A Separate Decision

These areas are not forbidden forever, but they require an explicit design
record, API boundary, and test plan before implementation:

- partial catalog results when one upstream fails;
- upstream capability change handling;
- cancellation and progress forwarding;
- resource subscriptions and list-change notifications;
- task routing;
- bounded executors, backpressure, or adaptive pooling;
- a C ABI component;
- Rust, Go, Python, Node, or other language bindings;
- admin/control-plane APIs.
- Redis-like local middleware daemon packaging.

## Work That Should Stay Out

These should not be added to this repository's core data-plane surface:

- GUI or web dashboard code;
- product onboarding flows;
- profile stores and config migration systems;
- daemon supervisors, installers, signing, or auto-update logic;
- authentication workflows, RBAC, tenant management, or credential vaults;
- audit log stores or analytics pipelines;
- generic MCP SDK functionality;
- direct FFI bindings over `GatewayRuntime`;
- custom protocol or transport implementations that duplicate `cxxmcp`.

The Redis-like local middleware direction is valid, but it belongs in a daemon
repository or package that consumes this library. It must remain MCP-focused:
one stable local MCP endpoint over managed MCP upstreams, not a generic API
gateway or arbitrary plugin runtime.

## C ABI Rule

A C ABI is useful only if it is narrow and separate. It must not freeze the C++
API.

Acceptable future shape:

- `cxxmcp_gateway_runtime_create_from_json`;
- `cxxmcp_gateway_start_http`;
- `cxxmcp_gateway_handle_jsonrpc`;
- `cxxmcp_gateway_stop`;
- `cxxmcp_gateway_destroy`;
- `cxxmcp_gateway_free_string`;
- JSON/text input and output;
- opaque handles;
- explicit memory ownership and error rules.

Unacceptable shape:

- exposing C++ standard-library types;
- exposing SDK protocol structs through C;
- binding directly to `GatewayRuntime`;
- making Rust or another language drive core API design.

## Release Naming

Use these labels:

- `library-first gateway`;
- `data-plane gateway`;
- `reference CLI`;
- `simple sidecar runner`;
- `pre-1.0 C++ source API`.

Avoid these labels:

- `enterprise gateway`;
- `management platform`;
- `universal MCP runtime`;
- `stable ABI`;
- `official cross-language SDK`;
- `production control plane`.

## Decision Rule

Before adding a feature, answer:

1. Does this compose multiple upstream MCP servers into one downstream endpoint?
2. Does this belong above `cxxmcp`, rather than inside the SDK?
3. Can a C++ host embed it without adopting product workflow or control-plane
   state?
4. Can it be tested as data-plane behavior?
5. Does it avoid advertising unsupported MCP capabilities?

If the answer to any question is no, the feature should move to `cxxmcp`, an
example, a future control-plane package, or a separate product repository.
