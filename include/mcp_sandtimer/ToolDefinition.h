#pragma once

#include <string>
#include <vector>

#include "mcp_sandtimer/Json.h"

namespace mcp_sandtimer {

struct ToolDefinition {
    std::string name;
    std::string description;
    json::Value input_schema;

    json::Value ToJson() const;
};

const std::vector<ToolDefinition>& GetToolDefinitions();

}  // namespace mcp_sandtimer
