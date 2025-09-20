#include "mcp_sandtimer/MCPSandTimerServer.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

#include "mcp_sandtimer/ToolDefinition.h"
#include "mcp_sandtimer/Version.h"

namespace mcp_sandtimer {
namespace {
constexpr const char* kProtocolVersion = "0.1";

std::string Trim(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string ToLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

}  // namespace

JSONRPCError::JSONRPCError(int code, std::string message, std::optional<json::Value> data)
    : std::runtime_error(message), code_(code), message_(std::move(message)), data_(std::move(data)) {}

MCPSandTimerServer::MCPSandTimerServer(TimerClient client, std::istream& input, std::ostream& output)
    : timer_client_(std::move(client)), input_(input), output_(output) {}

void MCPSandTimerServer::Serve() {
    while (!shutdown_requested_) {
        std::optional<json::Value> message;
        try {
            message = ReadMessage();
        } catch (const JSONRPCError& error) {
            std::cerr << "Failed to read JSON-RPC message: " << error.what() << std::endl;
            continue;
        }

        if (!message.has_value()) {
            break;
        }

        try {
            Dispatch(*message);
        } catch (const JSONRPCError& error) {
            try {
                const auto& object = message->as_object();
                auto id_iter = object.find("id");
                if (id_iter != object.end()) {
                    SendError(id_iter->second, error);
                }
            } catch (const json::ParseError&) {
                std::cerr << "Unable to send error response: invalid JSON message." << std::endl;
            }
        } catch (const std::exception& ex) {
            try {
                const auto& object = message->as_object();
                auto id_iter = object.find("id");
                if (id_iter != object.end()) {
                    JSONRPCError internal_error(
                        -32603,
                        "Internal error",
                        json::make_object({{"message", json::Value("An unexpected error occurred.")}}));
                    SendError(id_iter->second, internal_error);
                }
            } catch (const std::exception&) {
                std::cerr << "Failed to send internal error response: " << ex.what() << std::endl;
            }
        }
    }
}

const std::vector<ToolDefinition>& MCPSandTimerServer::ToolDefinitions() {
    return GetToolDefinitions();
}

std::optional<json::Value> MCPSandTimerServer::ReadMessage() {
    std::string line;
    std::size_t content_length = 0;
    bool saw_header = false;

    while (true) {
        if (!std::getline(input_, line)) {
            if (!saw_header && input_.eof()) {
                return std::nullopt;
            }
            throw JSONRPCError(-32700, "Unexpected end of stream while reading headers");
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }
        saw_header = true;
        auto colon = line.find(':');
        if (colon == std::string::npos) {
            throw JSONRPCError(-32700, "Invalid header line", json::make_object({{"header", json::Value(line.c_str())}}));
        }
        std::string key = ToLower(line.substr(0, colon));
        std::string value = Trim(line.substr(colon + 1));
        if (key == "content-length") {
            try {
                content_length = static_cast<std::size_t>(std::stoul(value));
            } catch (const std::exception&) {
                throw JSONRPCError(-32600, "Invalid Content-Length header");
            }
        }
    }

    if (!saw_header && input_.eof()) {
        return std::nullopt;
    }

    if (content_length == 0) {
        throw JSONRPCError(-32600, "Missing Content-Length header");
    }

    std::string payload(content_length, '\0');
    input_.read(payload.data(), static_cast<std::streamsize>(content_length));
    if (static_cast<std::size_t>(input_.gcount()) != content_length) {
        throw JSONRPCError(-32700, "Unexpected end of stream while reading payload");
    }

    try {
        return json::Value::parse(payload);
    } catch (const json::ParseError& error) {
        throw JSONRPCError(-32700, "Parse error", json::make_object({{"message", json::Value(error.what())}}));
    }
}

void MCPSandTimerServer::Dispatch(const json::Value& message) {
    const auto& object = message.as_object();
    auto method_iter = object.find("method");
    if (method_iter == object.end() || !method_iter->second.is_string()) {
        throw JSONRPCError(-32600, "Invalid Request", json::make_object({{"message", json::Value("Missing method.")}}));
    }
    const std::string& method = method_iter->second.as_string();

    auto params_iter = object.find("params");
    json::Value params = params_iter != object.end() ? params_iter->second : json::Value(json::Value::Object{});

    auto id_iter = object.find("id");
    if (id_iter == object.end()) {
        HandleNotification(method, params);
        return;
    }

    json::Value result = HandleRequest(method, params);
    SendResponse(id_iter->second, result);
}

void MCPSandTimerServer::HandleNotification(const std::string& method, const json::Value& params) {
    (void)params;
    if (method == "notifications/initialized") {
        return;
    }
    if (method == "notifications/cancelled") {
        std::cerr << "Received cancellation notification" << std::endl;
        return;
    }
    std::cerr << "Ignoring notification: " << method << std::endl;
}

json::Value MCPSandTimerServer::HandleRequest(const std::string& method, const json::Value& params) {
    if (method == "initialize") {
        return HandleInitialize(params);
    }
    if (method == "shutdown") {
        shutdown_requested_ = true;
        return json::Value(nullptr);
    }
    if (method == "tools/list") {
        json::Value::Array tools_array;
        for (const auto& tool : GetToolDefinitions()) {
            tools_array.push_back(tool.ToJson());
        }
        return json::make_object({{"tools", json::Value(std::move(tools_array))}});
    }
    if (method == "tools/call") {
        return HandleToolCall(params);
    }
    if (method == "ping") {
        return json::make_object({{"message", json::Value("pong")}});
    }
    throw JSONRPCError(-32601, "Method not found", json::make_object({{"method", json::Value(method.c_str())}}));
}

json::Value MCPSandTimerServer::HandleInitialize(const json::Value& params) {
    (void)params;
    initialized_ = true;
    json::Value server_info = json::make_object({
        {"name", json::Value("mcp-sandtimer")},
        {"version", json::Value(kVersion)}
    });
    json::Value capabilities = json::make_object({
        {"tools", json::make_object({{"listChanged", json::Value(false)}})}
    });
    return json::make_object({
        {"protocolVersion", json::Value(kProtocolVersion)},
        {"serverInfo", server_info},
        {"capabilities", capabilities}
    });
}

json::Value MCPSandTimerServer::HandleToolCall(const json::Value& params) {
    const auto& object = params.as_object();
    auto name_iter = object.find("name");
    if (name_iter == object.end() || !name_iter->second.is_string()) {
        throw JSONRPCError(-32602, "Invalid params", json::make_object({{"message", json::Value("Tool name must be provided as a string.")}}));
    }
    std::string name = name_iter->second.as_string();

    json::Value arguments;
    auto args_iter = object.find("arguments");
    if (args_iter != object.end()) {
        if (!args_iter->second.is_object()) {
            throw JSONRPCError(-32602, "Invalid params", json::make_object({{"message", json::Value("Tool arguments must be provided as an object.")}}));
        }
        arguments = args_iter->second;
    } else {
        arguments = json::Value(json::Value::Object{});
    }

    std::string text;
    if (name == "start_timer") {
        text = HandleStart(arguments);
    } else if (name == "reset_timer") {
        text = HandleReset(arguments);
    } else if (name == "cancel_timer") {
        text = HandleCancel(arguments);
    } else {
        throw JSONRPCError(-32601, "Tool not found", json::make_object({{"name", json::Value(name.c_str())}}));
    }

    json::Value::Array content;
    content.push_back(json::make_object({{"type", json::Value("text")}, {"text", json::Value(text.c_str())}}));
    return json::make_object({{"content", json::Value(std::move(content))}});
}

std::string MCPSandTimerServer::HandleStart(const json::Value& arguments) {
    std::string label = ExtractLabel(arguments);
    const auto& object = arguments.as_object();
    auto time_iter = object.find("time");
    if (time_iter == object.end() || !time_iter->second.is_number()) {
        throw JSONRPCError(-32602, "Invalid params", json::make_object({{"message", json::Value("The 'time' property must be a positive number.")}}));
    }
    double seconds_value = time_iter->second.as_number();
    if (seconds_value <= 0) {
        throw JSONRPCError(-32602, "Invalid params", json::make_object({{"message", json::Value("Timer length must be greater than zero.")}}));
    }
    int seconds = static_cast<int>(seconds_value);
    try {
        timer_client_.start_timer(label, seconds);
    } catch (const TimerClientError& error) {
        throw JSONRPCError(-32001, "Failed to reach sandtimer", json::make_object({{"message", json::Value(error.what())}}));
    }
    std::ostringstream oss;
    oss << "Started timer '" << label << "' for " << seconds << " seconds.";
    return oss.str();
}

std::string MCPSandTimerServer::HandleReset(const json::Value& arguments) {
    std::string label = ExtractLabel(arguments);
    try {
        timer_client_.reset_timer(label);
    } catch (const TimerClientError& error) {
        throw JSONRPCError(-32001, "Failed to reach sandtimer", json::make_object({{"message", json::Value(error.what())}}));
    }
    return "Reset timer '" + label + "'.";
}

std::string MCPSandTimerServer::HandleCancel(const json::Value& arguments) {
    std::string label = ExtractLabel(arguments);
    try {
        timer_client_.cancel_timer(label);
    } catch (const TimerClientError& error) {
        throw JSONRPCError(-32001, "Failed to reach sandtimer", json::make_object({{"message", json::Value(error.what())}}));
    }
    return "Cancelled timer '" + label + "'.";
}

std::string MCPSandTimerServer::ExtractLabel(const json::Value& arguments) {
    const auto& object = arguments.as_object();
    auto iter = object.find("label");
    if (iter == object.end() || !iter->second.is_string()) {
        throw JSONRPCError(-32602, "Invalid params", json::make_object({{"message", json::Value("A non-empty string label is required.")}}));
    }
    std::string label = Trim(iter->second.as_string());
    if (label.empty()) {
        throw JSONRPCError(-32602, "Invalid params", json::make_object({{"message", json::Value("A non-empty string label is required.")}}));
    }
    return label;
}

void MCPSandTimerServer::Send(const json::Value& payload) {
    const std::string encoded = payload.dump();
    output_ << "Content-Length: " << encoded.size() << "\r\n\r\n" << encoded;
    output_.flush();
}

void MCPSandTimerServer::SendResponse(const json::Value& id, const json::Value& result) {
    json::Value response = json::make_object({
        {"jsonrpc", json::Value("2.0")},
        {"id", id},
        {"result", result}
    });
    Send(response);
}

void MCPSandTimerServer::SendError(const json::Value& id, const JSONRPCError& error) {
    json::Value error_object = json::make_object({
        {"code", json::Value(error.code())},
        {"message", json::Value(error.message().c_str())}
    });
    if (error.has_data()) {
        error_object.as_object()["data"] = error.data();
    }
    json::Value response = json::make_object({
        {"jsonrpc", json::Value("2.0")},
        {"id", id},
        {"error", error_object}
    });
    Send(response);
}

}  // namespace mcp_sandtimer
