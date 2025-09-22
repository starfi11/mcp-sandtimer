#include "mcp_sandtimer/MCPSandTimerServer.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

#include "mcp_sandtimer/ToolDefinition.h"
#include "mcp_sandtimer/Version.h"
#include "mcp_sandtimer/Logger.h"
// MCP 协议服务端核心实现，从MCP客户端（Cursor）读取 JSON-RPC消息，并进行处理

namespace mcp_sandtimer {
namespace {
constexpr const char* kProtocolVersion = "0.1";

// 去除字符串前后空白字符
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

// 转换字符串为小写
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

// 不断从 stdin 读取 JSON-RPC 消息，调度执行并返回响应
void MCPSandTimerServer::Serve() {
    Logger::Info("Serve loop started");
    while (!shutdown_requested_) {
        Logger::Info("Waiting for next JSON-RPC message from client");
        std::optional<json::Value> message;
        try {
            message = ReadMessage();
        } catch (const JSONRPCError& error) {
            Logger::Error(std::string("Failed to read JSON-RPC message: ") + error.what());
            std::cerr << "Failed to read JSON-RPC message: " << error.what() << std::endl;
            continue;
        }

        if (!message.has_value()) {
            Logger::Info("No more messages to read. Exiting serve loop.");
            break;
        }

        try {
            Dispatch(*message);
        } catch (const JSONRPCError& error) {
            Logger::Error(std::string("JSON-RPC error during dispatch: ") + error.what());
            try {
                const auto& object = message->as_object();
                auto id_iter = object.find("id");
                if (id_iter != object.end()) {
                    SendError(id_iter->second, error);
                }
            } catch (const json::ParseError& parse_error) {
                Logger::Error(std::string("Unable to send error response due to invalid JSON message: ") +
                              parse_error.what());
                std::cerr << "Unable to send error response: invalid JSON message. " << parse_error.what()
                          << std::endl;
            }
        } catch (const std::exception& ex) {
            Logger::Error(std::string("Unexpected exception during dispatch: ") + ex.what());
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
            } catch (const std::exception& send_error) {
                Logger::Error(std::string("Failed to send internal error response: ") + send_error.what() +
                              ", original error: " + ex.what());
                std::cerr << "Failed to send internal error response: " << send_error.what()
                          << ", original error: " << ex.what() << std::endl;
            }
        }
    }
    Logger::Info("Serve loop exited");
}

const std::vector<ToolDefinition>& MCPSandTimerServer::ToolDefinitions() {
    return GetToolDefinitions();
}

// 读取 MCP/JSON-RPC 消息
std::optional<json::Value> MCPSandTimerServer::ReadMessage() {
    std::string line;
    std::size_t content_length = 0;
    bool saw_header = false;
    std::ostringstream header_stream;
    bool has_headers = false;

    // 循环逐行读取头部
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
        if (has_headers) {
            header_stream << "; ";
        }
        header_stream << line;
        has_headers = true;
        auto colon = line.find(':');
        if (colon == std::string::npos) {
            throw JSONRPCError(-32700, "Invalid header line", json::make_object({{"header", json::Value(line.c_str())}}));
        }
        std::string key = ToLower(line.substr(0, colon));
        std::string value = Trim(line.substr(colon + 1));
        if (key == "content-length") {
            try {
                content_length = static_cast<std::size_t>(std::stoul(value));
            } catch (const std::exception& parse_error) {
                Logger::Error(std::string("Failed to parse Content-Length header value '") + value + "': " +
                              parse_error.what());
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

    // 按 content_length 读取 JSON 负载
    std::string payload(content_length, '\0');
    input_.read(payload.data(), static_cast<std::streamsize>(content_length));
    if (static_cast<std::size_t>(input_.gcount()) != content_length) {
        throw JSONRPCError(-32700, "Unexpected end of stream while reading payload");
    }

    if (has_headers) {
        Logger::Debug(std::string("Read headers: ") + header_stream.str());
    }
    Logger::Debug(std::string("Read payload: ") + payload);
    Logger::Info("Received message from client");

    try {
        return json::Value::parse(payload);
    } catch (const json::ParseError& error) {
        Logger::Error(std::string("Failed to parse JSON payload: ") + error.what());
        throw JSONRPCError(-32700, "Parse error", json::make_object({{"message", json::Value(error.what())}}));
    }
}

// JSON-RPC 消息分流处理（请求/通知）
void MCPSandTimerServer::Dispatch(const json::Value& message) {
    const auto& object = message.as_object();
    auto method_iter = object.find("method");
    if (method_iter == object.end() || !method_iter->second.is_string()) {
        throw JSONRPCError(-32600, "Invalid Request", json::make_object({{"message", json::Value("Missing method.")}}));
    }
    const std::string& method = method_iter->second.as_string();

    auto params_iter = object.find("params");
    json::Value params = params_iter != object.end() ? params_iter->second : json::Value(json::Value::Object{});

    Logger::Info(std::string("Dispatching method: ") + method);
    Logger::Debug(std::string("Dispatch params: ") + params.dump());

    auto id_iter = object.find("id");
    if (id_iter == object.end()) {
        Logger::Info(std::string("Message is a notification, dispatching HandleNotification for method: ") + method);
        HandleNotification(method, params);
        return;
    }

    Logger::Info("Message is a request, invoking HandleRequest");
    json::Value result = HandleRequest(method, params);
    SendResponse(id_iter->second, result);
}

void MCPSandTimerServer::HandleNotification(const std::string& method, const json::Value& params) {
    (void)params;
    Logger::Info(std::string("HandleNotification received method: ") + method);
    if (method == "notifications/initialized") {
        Logger::Debug("notifications/initialized received - no action taken");
        return;
    }
    if (method == "notifications/cancelled") {
        Logger::Info("Received cancellation notification from client");
        std::cerr << "Received cancellation notification" << std::endl;
        return;
    }
    Logger::Info(std::string("Notification not explicitly handled: ") + method);
    std::cerr << "Ignoring notification: " << method << std::endl;
}

// 方法调度器
json::Value MCPSandTimerServer::HandleRequest(const std::string& method, const json::Value& params) {
    Logger::Info(std::string("Handling request: ") + method);
    Logger::Debug(std::string("Request params: ") + params.dump());
    if (method == "initialize") {
        Logger::Info("Branching to HandleInitialize");
        return HandleInitialize(params);
    }
    if (method == "shutdown") {
        Logger::Info("Shutdown request received - signalling serve loop to exit");
        shutdown_requested_ = true;
        return json::Value(nullptr);
    }
    if (method == "tools/list") {
        Logger::Info("Branching to tools/list handler");
        json::Value::Array tools_array;
        for (const auto& tool : GetToolDefinitions()) {
            tools_array.push_back(tool.ToJson());
        }
        return json::make_object({{"tools", json::Value(std::move(tools_array))}});
    }
    if (method == "tools/call") {
        Logger::Info("Branching to HandleToolCall");
        return HandleToolCall(params);
    }
    if (method == "ping") {
        Logger::Info("Responding to ping request with pong message");
        return json::make_object({{"message", json::Value("pong")}});
    }
    throw JSONRPCError(-32601, "Method not found", json::make_object({{"method", json::Value(method.c_str())}}));
}

// MCP 协议中的 initialize 请求。客户端启动时的握手步骤
json::Value MCPSandTimerServer::HandleInitialize(const json::Value& params) {
    (void)params;
    initialized_ = true;
    // 构造服务端信息
    json::Value server_info = json::make_object({
        {"name", json::Value("mcp-sandtimer")},
        {"version", json::Value(kVersion)}
    });
    // 声明能力
    json::Value capabilities = json::make_object({
        {"tools", json::make_object({{"listChanged", json::Value(false)}})}
    });
    // 返回握手结果
    return json::make_object({
        {"protocolVersion", json::Value(kProtocolVersion)},
        {"serverInfo", server_info},
        {"capabilities", capabilities}
    });
}

json::Value MCPSandTimerServer::HandleToolCall(const json::Value& params) {
    Logger::Info("Handling tool call request");
    Logger::Debug(std::string("Tool call params: ") + params.dump());
    const auto& object = params.as_object();
    auto name_iter = object.find("name");
    if (name_iter == object.end() || !name_iter->second.is_string()) {
        throw JSONRPCError(-32602, "Invalid params", json::make_object({{"message", json::Value("Tool name must be provided as a string.")}}));
    }
    std::string name = name_iter->second.as_string();
    Logger::Info(std::string("Tool call name: ") + name);

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
    Logger::Debug(std::string("Tool call arguments: ") + arguments.dump());

    std::string text;
    if (name == "start_timer") {
        Logger::Info("Dispatching to HandleStart");
        text = HandleStart(arguments);
    } else if (name == "reset_timer") {
        Logger::Info("Dispatching to HandleReset");
        text = HandleReset(arguments);
    } else if (name == "cancel_timer") {
        Logger::Info("Dispatching to HandleCancel");
        text = HandleCancel(arguments);
    } else {
        throw JSONRPCError(-32601, "Tool not found", json::make_object({{"name", json::Value(name.c_str())}}));
    }

    json::Value::Array content;
    content.push_back(json::make_object({{"type", json::Value("text")}, {"text", json::Value(text.c_str())}}));
    return json::make_object({{"content", json::Value(std::move(content))}});
}

std::string MCPSandTimerServer::HandleStart(const json::Value& arguments) {
    Logger::Info("HandleStart called");
    Logger::Debug(std::string("Start arguments: ") + arguments.dump());
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
        Logger::Error(std::string("TimerClientError while starting timer: ") + error.what());
        throw JSONRPCError(-32001, "Failed to reach sandtimer", json::make_object({{"message", json::Value(error.what())}}));
    }
    std::ostringstream oss;
    oss << "Started timer '" << label << "' for " << seconds << " seconds.";
    return oss.str();
}

std::string MCPSandTimerServer::HandleReset(const json::Value& arguments) {
    Logger::Info("HandleReset called");
    Logger::Debug(std::string("Reset arguments: ") + arguments.dump());
    std::string label = ExtractLabel(arguments);
    try {
        timer_client_.reset_timer(label);
    } catch (const TimerClientError& error) {
        Logger::Error(std::string("TimerClientError while resetting timer: ") + error.what());
        throw JSONRPCError(-32001, "Failed to reach sandtimer", json::make_object({{"message", json::Value(error.what())}}));
    }
    return "Reset timer '" + label + "'.";
}

std::string MCPSandTimerServer::HandleCancel(const json::Value& arguments) {
    Logger::Info("HandleCancel called");
    Logger::Debug(std::string("Cancel arguments: ") + arguments.dump());
    std::string label = ExtractLabel(arguments);
    try {
        timer_client_.cancel_timer(label);
    } catch (const TimerClientError& error) {
        Logger::Error(std::string("TimerClientError while cancelling timer: ") + error.what());
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
    Logger::Debug(std::string("Send raw payload: ") + encoded);
    output_ << "Content-Length: " << encoded.size() << "\r\n\r\n" << encoded;
    output_.flush();
}

void MCPSandTimerServer::SendResponse(const json::Value& id, const json::Value& result) {
    json::Value response = json::make_object({
        {"jsonrpc", json::Value("2.0")},
        {"id", id},
        {"result", result}
    });
    Logger::Info(std::string("SendResponse: ") + response.dump());
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
    Logger::Info(std::string("SendError: ") + response.dump());
    Send(response);
}

}  // namespace mcp_sandtimer
