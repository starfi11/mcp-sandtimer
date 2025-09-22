#include "mcp_sandtimer/ToolDefinition.h"

#include <utility>

namespace mcp_sandtimer {

// 把 ToolDefinition 转为 JSON 格式
json::Value ToolDefinition::ToJson() const {
    return json::make_object({
        {"name", json::Value(name)},
        {"description", json::Value(description)},
        {"inputSchema", input_schema}
    });
}

// 集中定义 MCP 工具列表及其输入参数 JSON Schema
const std::vector<ToolDefinition>& GetToolDefinitions() {
    static const std::vector<ToolDefinition> kDefinitions = [] {
        using json::Value;

        // 用 JSON 字符串直接定义 schema
        Value start_schema = Value::parse(R"json(
        {
          "type": "object",
          "properties": {
            "label": {
              "type": "string",
              "description": "Identifier shown in the sandtimer window.",
              "minLength": 1
            },
            "time": {
              "type": "number",
              "description": "Duration for the countdown in seconds.",
              "minimum": 1
            }
          },
          "required": ["label", "time"],
          "additionalProperties": false
        }
        )json");

        Value reset_schema = Value::parse(R"json(
        {
          "type": "object",
          "properties": {
            "label": {
              "type": "string",
              "description": "Identifier of the timer to reset.",
              "minLength": 1
            }
          },
          "required": ["label"],
          "additionalProperties": false
        }
        )json");

        Value cancel_schema = Value::parse(R"json(
        {
          "type": "object",
          "properties": {
            "label": {
              "type": "string",
              "description": "Identifier of the timer to cancel.",
              "minLength": 1
            }
          },
          "required": ["label"],
          "additionalProperties": false
        }
        )json");

        return std::vector<ToolDefinition>{
            ToolDefinition{"start_timer", "Start or restart a sandtimer countdown.", start_schema},
            ToolDefinition{"reset_timer", "Reset an existing sandtimer back to its original duration.", reset_schema},
            ToolDefinition{"cancel_timer", "Close an active sandtimer window and cancel its countdown.", cancel_schema},
        };
    }();
    return kDefinitions;
}

}  // namespace mcp_sandtimer
