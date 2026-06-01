# Release Checklist

This checklist is the release-candidate gate for the library-first gateway
package. It does not add product packaging, GUI, daemon, policy, auth, or admin
API requirements.

Use [`release_evidence.md`](release_evidence.md) as the evidence index for the
checks below.

## Branch And Scope

- The release branch is based on the repository default branch.
- `docs/technical_roadmap.md` and `docs/gateway_scope.md` match the shipped
  capability surface.
- `docs/getting_started.md` describes the supported C++ host integration path,
  including component selection, runtime startup, optional capability refresh,
  persistent session tradeoffs, and CLI scope.
- `docs/api_contract.md` matches the public runtime behavior for lifecycle,
  concurrency, session modes, catalog caching, capability advertisement,
  observers, routing names, and error categories.
- `docs/compatibility.md` matches the release candidate's SDK revision policy,
  API/ABI stability level, supported CI platform matrix, and feature-support
  boundaries.
- Future-only capabilities remain unadvertised unless their namespace,
  routing, notification, and integration-test contracts are implemented.
  New routed capability families must pass the
  [`capability_extension_gate.md`](capability_extension_gate.md) checklist
  before they are advertised.

## Required Validation

- GitHub Actions `ci` passes for all release-blocking jobs listed in
  `operational_gates.md`.
- Local static and shared builds pass when practical:
  - configure and build with `BUILD_SHARED_LIBS=OFF`;
  - run `ctest --output-on-failure`;
  - configure and build with `BUILD_SHARED_LIBS=ON`;
  - run `ctest --output-on-failure`.
- `git diff --check` has no real whitespace or conflict-marker failures.
- Build-tree and install-tree package smoke tests configure, build, and run
  downstream consumers.
- Component install smoke tests install, configure, build, and run downstream
  consumers for the enabled `core`, `runtime`, `config_io`, and `cli`
  components.
- Optional embedding examples build when `CXXMCP_GATEWAY_BUILD_EXAMPLES=ON`;
  the `gateway_examples_build` CTest covers this.

## SDK And Performance Evidence

- Record the exact `cxxmcp` SDK commit used for validation.
- Refresh `docs/release_baseline.md` when the SDK revision or gateway
  routing/runtime implementation changes materially.
- Performance evidence includes Release-build stdio and Streamable HTTP
  timings for `tools/list`, per-call and persistent-session `tools/call`, and
  the persistent HTTP pool pair scenario. It also includes an initialized
  direct SDK Streamable HTTP `tools/call` comparison to separate gateway
  overhead from SDK, transport, and fixture-server path cost.

## Public Contract

- `find_package(cxxmcp-gateway CONFIG REQUIRED)` exports `core`.
- Core package consumers can use config validation, route/name helpers,
  catalog merge helpers, and gateway error helpers without linking runtime or
  config IO.
- `find_package(... COMPONENTS runtime config_io cli)` resolves only for
  components that were built and installed.
- `BUILD_SHARED_LIBS` remains honored by library targets.
- `<cxxmcp/gateway.hpp>` remains core-only.
- Runtime package consumers can call both typed runtime APIs and raw
  `handle_request()`/`handle_notification()` APIs through the `runtime`
  component.
- Runtime observability hooks do not add a logging-framework dependency to
  core or runtime.
