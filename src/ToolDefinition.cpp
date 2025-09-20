#include "mcp_sandtimer/ToolDefinition.h"

#include <utility>

namespace mcp_sandtimer {

json::Value ToolDefinition::ToJson() const {
    return json::make_object({
        {"name", json::Value(name)},
        {"description", json::Value(description)},
        {"inputSchema", input_schema}
    });
}

const std::vector<ToolDefinition>& GetToolDefinitions() {
    static const std::vector<ToolDefinition> kDefinitions = [] {
        using json::Value;
        using json::make_array;
        using json::make_object;

        Value start_schema = make_object({
            {"type", Value("object")},
            {"properties", make_object({
                {"label", make_object({
                    {"type", Value("string")},
                    {"description", Value("Identifier shown in the sandtimer window.")},
                    {"minLength", Value(1)}
                })},
                {"time", make_object({
                    {"type", Value("number")},
                    {"description", Value("Duration for the countdown in seconds.")},
                    {"minimum", Value(1)}
                })}
            })},
            {"required", make_array({Value("label"), Value("time")})},
            {"additionalProperties", Value(false)}
        });

        Value reset_schema = make_object({
            {"type", Value("object")},
            {"properties", make_object({
                {"label", make_object({
                    {"type", Value("string")},
                    {"description", Value("Identifier of the timer to reset.")},
                    {"minLength", Value(1)}
                })}
            })},
            {"required", make_array({Value("label")})},
            {"additionalProperties", Value(false)}
        });

        Value cancel_schema = make_object({
            {"type", Value("object")},
            {"properties", make_object({
                {"label", make_object({
                    {"type", Value("string")},
                    {"description", Value("Identifier of the timer to cancel.")},
                    {"minLength", Value(1)}
                })}
            })},
            {"required", make_array({Value("label")})},
            {"additionalProperties", Value(false)}
        });

        return std::vector<ToolDefinition>{
            ToolDefinition{"start_timer", "Start or restart a sandtimer countdown.", start_schema},
            ToolDefinition{"reset_timer", "Reset an existing sandtimer back to its original duration.", reset_schema},
            ToolDefinition{"cancel_timer", "Close an active sandtimer window and cancel its countdown.", cancel_schema},
        };
    }();
    return kDefinitions;
}

}  // namespace mcp_sandtimer
