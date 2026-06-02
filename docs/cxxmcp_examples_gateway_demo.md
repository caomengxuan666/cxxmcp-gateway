# cxxmcp-examples Dev-Tool Gateway Demo

This optional demo proves the intended repository relationship:

```text
cxxmcp-examples
  owns copyable MCP server examples

cxxmcp-gateway
  superbuilds selected example servers
  exposes them through one gateway endpoint
```

The repositories remain independent. `cxxmcp-gateway` does not vendor the
example sources into its normal build, and this demo is disabled by default.

## What It Aggregates

The demo builds these upstream MCP servers from `cxxmcp-examples`:

- `cxxmcp_workspace_server` as upstream id `workspace`;
- `cxxmcp_git_server` as upstream id `git`;
- `cxxmcp_cmake_ctest_server` as upstream id `cmake`;
- `cxxmcp_log_triage_server` as upstream id `logs`.

The gateway then exposes one hosted MCP endpoint, with stable gateway names
such as:

```text
workspace.workspace.scan
workspace.workspace.search
workspace.workspace.read_file
git.git.status
git.git.log
cmake.cmake.list_tests
logs.logs.summarize
```

The doubled words are intentional in this first version: the upstream example
servers already include their domain in their tool names, and the gateway adds
the upstream id as the routing namespace.

## Build From The Independent cxxmcp-examples Repository

By default, the gateway build fetches the independent `cxxmcp-examples`
repository as an external project:

```powershell
cmake -S . -B build-dev-tool-gateway `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCXXMCP_GATEWAY_BUILD_CXXMCP_EXAMPLES_DEV_TOOL_GATEWAY=ON `
  -Dcxxmcp_DIR=C:\path\to\cxxmcp\install\lib\cmake\cxxmcp

cmake --build build-dev-tool-gateway --target cxxmcp_gateway_cxxmcp_examples_dev_tool_gateway
```

Pin a branch, tag, or commit with:

```powershell
-DCXXMCP_GATEWAY_CXXMCP_EXAMPLES_GIT_TAG=<branch-tag-or-commit>
```

For local development only, use an explicit checkout with:

```powershell
-DCXXMCP_GATEWAY_CXXMCP_EXAMPLES_SOURCE_DIR=C:\path\to\cxxmcp-examples
```

The local checkout is never selected implicitly. This keeps the demo
reproducible by default while still letting maintainers test unpublished
`cxxmcp-examples` changes intentionally.

On Windows, use the same build type and MSVC runtime settings as the installed
`cxxmcp` SDK. Mixing a Release demo with a Debug SDK, or the reverse, can fail
at link time because the upstream example servers link the SDK libraries.

## Run

Streamable HTTP endpoint:

```powershell
build-dev-tool-gateway\cxxmcp-gateway-cxxmcp-examples-dev-tool-gateway.exe `
  --port 39931 `
  --workspace-root C:\Users\cmx\repo\cxxmcp-gateway `
  --persistent `
  --prewarm
```

Then point an MCP client at:

```text
http://127.0.0.1:39931/mcp
```

Stdio endpoint for clients that prefer launching MCP servers as commands:

```powershell
build-dev-tool-gateway\cxxmcp-gateway-cxxmcp-examples-dev-tool-gateway.exe `
  --stdio `
  --workspace-root C:\Users\cmx\repo\cxxmcp-gateway `
  --persistent `
  --prewarm
```

Codex can register the stdio form directly:

```powershell
codex mcp add cxxmcp-gateway-examples -- `
  C:\Users\cmx\repo\cxxmcp-gateway\build-dev-tool-gateway\cxxmcp-gateway-cxxmcp-examples-dev-tool-gateway.exe `
  --stdio `
  --workspace-root C:\Users\cmx\repo\cxxmcp-gateway `
  --persistent `
  --prewarm
```

In Codex, gateway tool names are displayed with underscores, for example
`git.git.status` appears as `git_git_status`.

## Scope

This demo is intentionally a data-plane example:

- no dashboard;
- no admin API;
- no profile store;
- no cross-language binding;
- no product workflow.

It shows why the gateway exists: a host can collect several independent MCP
servers behind one endpoint while keeping routing names and upstream lifecycle
under one library-owned contract.
