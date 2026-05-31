# Compatibility Policy

`cxxmcp-gateway` is currently a pre-1.0, library-first package. This policy
describes what consumers can rely on for the current release-candidate line and
what is intentionally not promised yet.

## Supported Consumer Shape

The supported consumer is a C++23 host that:

- uses CMake package consumption;
- links one or more exported gateway components;
- uses an HTTP-enabled `cxxmcp` SDK install compatible with the CI-tested SDK
  revision;
- owns application policy, auth, credentials, deployment, logging, metrics,
  and process supervision outside the gateway library.

The default package exports `cxxmcp-gateway::core`. Optional components export:

- `cxxmcp-gateway::runtime` when `CXXMCP_GATEWAY_BUILD_RUNTIME=ON`;
- `cxxmcp-gateway::config_io` when `CXXMCP_GATEWAY_BUILD_CONFIG_IO=ON`;
- `cxxmcp-gateway::cli` when `CXXMCP_GATEWAY_BUILD_CLI=ON`.

Subproject consumers get core/runtime by default without CLI, config IO, tests,
or performance tools.

## SDK Compatibility

The gateway is validated against the `cxxmcp` SDK revision configured by
`CXXMCP_REF` in CI and against the exact SDK revision recorded in release
evidence.

Until `cxxmcp` and `cxxmcp-gateway` publish a versioned compatibility matrix:

- do not claim compatibility beyond the CI-tested SDK revision;
- rebuild the gateway when the SDK revision changes;
- refresh package smoke tests, runtime integration tests, and performance
  evidence after material SDK updates;
- treat SDK transport, peer, service, and protocol API changes as potential
  gateway integration changes.

## API and ABI Stability

The public API is source-level C++ API, not a stable binary ABI commitment.

- Public headers, exported CMake targets, component names, routing contracts,
  and runtime behavior documented in `api_contract.md` are release-candidate
  contracts.
- Breaking source changes should be documented in release notes and reflected
  in the getting-started/API docs.
- Shared-library builds are tested because consumers need them, but ABI
  stability across independently upgraded binaries is not promised before a
  versioned ABI policy exists.
- Windows shared builds use automatic symbol export while the ABI is still
  stabilizing.

## Platform and Toolchain

The supported platform matrix is the release-blocking CI matrix:

- Linux static and shared gateway libraries;
- macOS static and shared gateway libraries;
- Windows static and shared gateway libraries;
- C++23;
- HTTP-enabled `cxxmcp` SDK.

Other platforms may work, but they are not release-blocking until added to the
CI matrix and operational gates.

## Feature Compatibility

The current stable surface is the data-plane MVP documented in
`gateway_scope.md` and `api_contract.md`:

- tools;
- resources and resource templates;
- prompts;
- selected completion routing;
- process stdio and Streamable HTTP upstreams;
- hosted Streamable HTTP downstream endpoint;
- package consumption and optional reference CLI.

The current package is not a compatibility promise for:

- a GUI or management console;
- daemon/admin APIs;
- config hot reload;
- auth, policy, audit, or credential management;
- task APIs;
- subscriptions;
- progress or cancellation forwarding;
- high-QPS connection pooling or hard real-time latency.

Those features require their own API, capability-advertisement, lifecycle, and
test contracts before they can become supported surface.
