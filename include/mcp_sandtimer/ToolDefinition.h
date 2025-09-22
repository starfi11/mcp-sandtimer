#pragma once

#include <string>
#include <vector>

#include "mcp_sandtimer/Json.h"

// 定义 MCP 协议所需的工具元信息结构及其 JSON 序列化接口
namespace mcp_sandtimer {

struct ToolDefinition {
    std::string name;
    std::string description;
    json::Value input_schema;

    json::Value ToJson() const;
};

const std::vector<ToolDefinition>& GetToolDefinitions();

}  // namespace mcp_sandtimer
