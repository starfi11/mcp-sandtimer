#include "mcp_sandtimer/MCPSandTimerServer.h"
#include "mcp_sandtimer/TimerClient.h"
#include "mcp_sandtimer/ToolDefinition.h"
#include "mcp_sandtimer/Version.h"
#include "mcp_sandtimer/Json.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct Options {
    std::string host = "127.0.0.1";
    std::uint16_t port = 61420;
    int timeout_ms = 5000;
    bool list_tools = false;
    bool show_version = false;
    bool show_help = false;
};

void PrintUsage() {
    std::cout << "Usage: mcp-sandtimer [options]\n"
              << "\n"
              << "Options:\n"
              << "  --host <hostname>     Address of the sandtimer TCP server (default 127.0.0.1)\n"
              << "  --port <port>         TCP port exposed by sandtimer (default 61420)\n"
              << "  --timeout <seconds>   Connection timeout in seconds (default 5)\n"
              << "  --list-tools          Print the MCP tool descriptions as JSON and exit\n"
              << "  --version             Print version information and exit\n"
              << "  -h, --help            Show this message\n";
}

bool ParseInteger(const std::string& text, long long& output) {
    char* end = nullptr;
    long long value = std::strtoll(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0') {
        return false;
    }
    output = value;
    return true;
}

Options ParseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--host requires an argument");
            }
            options.host = argv[++i];
        } else if (arg == "--port") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--port requires an argument");
            }
            long long value = 0;
            if (!ParseInteger(argv[++i], value) || value <= 0 || value > 65535) {
                throw std::runtime_error("--port expects a positive integer between 1 and 65535");
            }
            options.port = static_cast<std::uint16_t>(value);
        } else if (arg == "--timeout") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--timeout requires an argument");
            }
            long long value = 0;
            if (!ParseInteger(argv[++i], value) || value < 0) {
                throw std::runtime_error("--timeout expects a non-negative integer");
            }
            options.timeout_ms = static_cast<int>(value * 1000);
        } else if (arg == "--list-tools") {
            options.list_tools = true;
        } else if (arg == "--version") {
            options.show_version = true;
        } else if (arg == "--help" || arg == "-h") {
            options.show_help = true;
        } else {
            throw std::runtime_error("Unrecognised argument: " + arg);
        }
    }
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Options options = ParseOptions(argc, argv);
        using mcp_sandtimer::json::Value;

        if (options.show_help) {
            PrintUsage();
            return 0;
        }

        if (options.show_version) {
            std::cout << "mcp-sandtimer " << mcp_sandtimer::kVersion << std::endl;
            return 0;
        }

        if (options.list_tools) {
            Value::Array tools_json;
            for (const auto& tool : mcp_sandtimer::MCPSandTimerServer::ToolDefinitions()) {
                tools_json.push_back(tool.ToJson());
            }
            Value payload = Value(std::move(tools_json));
            std::cout << payload.dump() << std::endl;
            return 0;
        }

        mcp_sandtimer::TimerClient client(options.host, options.port, std::chrono::milliseconds(options.timeout_ms));
        mcp_sandtimer::MCPSandTimerServer server(std::move(client));
        server.Serve();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "mcp-sandtimer: " << ex.what() << std::endl;
        return 1;
    }
}
