# cxxmcp-gateway

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/build-CMake-064F8C.svg)](https://cmake.org/)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

cxxmcp-gateway is a MCP gateway and runtime management tool built on top of the [cxxmcp](https://github.com/caomengxuan666/cxxmcp) SDK.

## Overview

This repository contains the gateway, runtime, and CLI tools that were previously part of the cxxmcp SDK repository. These components have been separated to keep the SDK repository focused on the core SDK functionality.

## Components

| Component | Description |
|-----------|-------------|
| `cxxmcp::runtime` | Runtime application service library |
| `cxxmcp::gateway` | Gateway service library |
| `cxxmcp::cli` | Command-line interface tool |

## Dependencies

- [cxxmcp](https://github.com/caomengxuan666/cxxmcp) SDK
- [spdlog](https://github.com/gabime/spdlog) - Logging library
- [CLI11](https://github.com/CLIUtils/CLI11) - Command-line parsing library

## Building

### Prerequisites

- CMake 3.23+
- A C++20 compiler
- cxxmcp SDK installed

### Build

```powershell
cmake -S . -B build
cmake --build build
```

### Install

```powershell
cmake --install build --prefix out/install/cxxmcp-gateway
```

## Usage

### CLI Tool

```powershell
# Show help
cxxmcp-gateway --help

# Show version
cxxmcp-gateway --version
```

### CMake Integration

```cmake
find_package(cxxmcp-gateway CONFIG REQUIRED)

target_link_libraries(my_app PRIVATE cxxmcp::runtime)
target_link_libraries(my_app PRIVATE cxxmcp::gateway)
target_link_libraries(my_app PRIVATE cxxmcp::cli)
```

## License

MIT License - see [LICENSE](LICENSE) for details.
