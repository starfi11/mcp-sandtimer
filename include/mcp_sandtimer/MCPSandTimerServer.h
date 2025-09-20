#pragma once

#include <iostream>
#include <istream>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "mcp_sandtimer/Json.h"
#include "mcp_sandtimer/TimerClient.h"
#include "mcp_sandtimer/ToolDefinition.h"

namespace mcp_sandtimer {

class JSONRPCError : public std::runtime_error {
public:
    JSONRPCError(int code, std::string message, std::optional<json::Value> data = std::nullopt);

    int code() const noexcept { return code_; }
    const std::string& message() const noexcept { return message_; }
    bool has_data() const noexcept { return data_.has_value(); }
    const json::Value& data() const { return data_.value(); }

private:
    int code_;
    std::string message_;
    std::optional<json::Value> data_;
};

class MCPSandTimerServer {
public:
    MCPSandTimerServer(TimerClient client, std::istream& input = std::cin, std::ostream& output = std::cout);

    void Serve();

    static const std::vector<ToolDefinition>& ToolDefinitions();

private:
    TimerClient timer_client_;
    std::istream& input_;
    std::ostream& output_;
    bool shutdown_requested_ = false;
    bool initialized_ = false;

    std::optional<json::Value> ReadMessage();
    void Dispatch(const json::Value& message);
    void HandleNotification(const std::string& method, const json::Value& params);
    json::Value HandleRequest(const std::string& method, const json::Value& params);
    json::Value HandleInitialize(const json::Value& params);
    json::Value HandleToolCall(const json::Value& params);
    std::string HandleStart(const json::Value& arguments);
    std::string HandleReset(const json::Value& arguments);
    std::string HandleCancel(const json::Value& arguments);
    std::string ExtractLabel(const json::Value& arguments);
    void Send(const json::Value& payload);
    void SendResponse(const json::Value& id, const json::Value& result);
    void SendError(const json::Value& id, const JSONRPCError& error);
};

}  // namespace mcp_sandtimer
