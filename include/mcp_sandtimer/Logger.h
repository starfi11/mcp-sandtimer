#pragma once

#include <fstream>
#include <mutex>
#include <string>

namespace mcp_sandtimer {

class Logger {
public:
    enum class Level {
        Debug = 0,
        Info = 1,
        Error = 2
    };

    static Logger& Instance();

    static void SetLevel(Level level);
    static void SetLevel(const std::string& level_name);

    static void Debug(const std::string& message);
    static void Info(const std::string& message);
    static void Error(const std::string& message);

private:
    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void Log(Level level, const std::string& message);
    bool ShouldLog(Level level) const;
    static std::string LevelToString(Level level);
    static Level ParseLevelName(const std::string& level_name, Level default_level);

    std::ofstream stream_;
    mutable std::mutex mutex_;
    Level level_ = Level::Info;
};

}  // namespace mcp_sandtimer

