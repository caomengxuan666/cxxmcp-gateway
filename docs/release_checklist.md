# Release Checklist

This checklist is the release-candidate gate for the library-first gateway
package. It does not add product packaging, GUI, daemon, policy, auth, or admin
API requirements.

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
- Future-only capabilities remain unadvertised unless their namespace,
  routing, notification, and integration-test contracts are implemented.

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

## SDK And Performance Evidence

- Record the exact `cxxmcp` SDK commit used for validation.
- Refresh `docs/release_baseline.md` when the SDK revision or gateway
  routing/runtime implementation changes materially.
- Performance evidence includes Release-build stdio and Streamable HTTP
  timings for `tools/list` and `tools/call`.

## Public Contract

- `find_package(cxxmcp-gateway CONFIG REQUIRED)` exports `core`.
- `find_package(... COMPONENTS runtime config_io cli)` resolves only for
  components that were built and installed.
- `BUILD_SHARED_LIBS` remains honored by library targets.
- `<cxxmcp/gateway.hpp>` remains core-only.
- Runtime observability hooks do not add a logging-framework dependency to
  core or runtime.
