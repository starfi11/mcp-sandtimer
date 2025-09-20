#include "mcp_sandtimer/MCPSandTimerServer.h"

#include <iostream>
#include <set>
#include <string>

int main() {
    const auto& tools = mcp_sandtimer::MCPSandTimerServer::ToolDefinitions();
    if (tools.size() != 3) {
        std::cerr << "Expected 3 tools but found " << tools.size() << std::endl;
        return 1;
    }

    std::set<std::string> names;
    for (const auto& tool : tools) {
        if (tool.name.empty()) {
            std::cerr << "Tool name must not be empty" << std::endl;
            return 1;
        }
        if (tool.description.empty()) {
            std::cerr << "Tool description must not be empty" << std::endl;
            return 1;
        }
        names.insert(tool.name);
    }

    if (names.count("start_timer") == 0 || names.count("reset_timer") == 0 || names.count("cancel_timer") == 0) {
        std::cerr << "Tool names are incomplete" << std::endl;
        return 1;
    }

    return 0;
}
