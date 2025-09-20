# MCP Sandtimer Server (C++)

This repository provides a C++ implementation of a Model Context Protocol (MCP) server that exposes the [sandtimer](sandtimer) desktop countdown utility to ChatGPT or any MCP-compatible client. The server receives JSON-RPC requests over STDIN/STDOUT, translates them into TCP commands, and forwards them to the sandtimer process listening on `127.0.0.1:61420`.

## Features

- Implements the MCP JSON-RPC handshake (`initialize`, `tools/list`, `tools/call`, etc.).
- Provides three tools:
  - `start_timer(label: string, time: number)`
  - `reset_timer(label: string)`
  - `cancel_timer(label: string)`
- Forwards commands to sandtimer as JSON payloads over TCP, e.g. `{ "cmd": "start", "label": "demo", "time": 60 }`.
- Lightweight JSON parser/serializer with no external runtime dependencies.
- CMake-based build that targets Windows and other desktop platforms.
- GitHub Actions workflow that packages a standalone Windows executable on tagged releases.

## Build Instructions

The project requires a C++17-capable compiler and CMake 3.20+. On Linux/macOS:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On Windows (PowerShell or Developer Command Prompt):

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The resulting executable is located at `build/mcp-sandtimer` (or `build/Release/mcp-sandtimer.exe` on Windows).

### Running the server

Start the sandtimer desktop application so it listens on `127.0.0.1:61420`, then launch the MCP server:

```bash
./build/mcp-sandtimer --host 127.0.0.1 --port 61420
```

The server communicates with the MCP client over STDIN/STDOUT. Use `--help` to see available options, including `--list-tools` for quickly inspecting the tool descriptions.

### Tests

Run the lightweight test suite via CTest:

```bash
cmake --build build --target test
```

or directly:

```bash
ctest --test-dir build
```

## Packaging & Releases

Tagging the repository with `v*` (e.g. `v1.0.0`) automatically triggers the GitHub Actions workflow defined in `.github/workflows/release.yml`. The workflow:

1. Configures and builds the project on `windows-latest` (x64).
2. Executes the CTest suite in Release mode.
3. Invokes CPack to generate a ZIP archive containing `mcp-sandtimer.exe`.
4. Uploads the archive both as a build artifact and as a release asset attached to the tag.

The packaged executable bundles all dependencies required to communicate with sandtimer; no additional runtime (Python, Node.js, etc.) is necessary on the target machine.

## Command-Line Reference

| Option | Description |
| --- | --- |
| `--host <hostname>` | Override the sandtimer TCP host (default `127.0.0.1`). |
| `--port <port>` | Override the sandtimer TCP port (default `61420`). |
| `--timeout <seconds>` | Socket timeout in seconds (default `5`). |
| `--list-tools` | Print the MCP tool definitions as JSON and exit. |
| `--version` | Show version information and exit. |
| `-h`, `--help` | Display usage help. |

## License

This project is released under the MIT License. See [LICENSE](LICENSE) for details.
