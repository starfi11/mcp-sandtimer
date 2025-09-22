#include "mcp_sandtimer/Logger.h"

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace mcp_sandtimer {
namespace {

std::string CurrentTimestamp() {
    using Clock = std::chrono::system_clock;
    const auto now = Clock::now();
    const std::time_t time_now = Clock::to_time_t(now);

    std::tm tm_snapshot;
#if defined(_WIN32)
    localtime_s(&tm_snapshot, &time_now);
#else
    localtime_r(&time_now, &tm_snapshot);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_snapshot, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

}  // namespace

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

Logger::Logger() : stream_("mcp-sandtimer.log", std::ios::app) {
    level_ = Level::INFO;
    const char* env = std::getenv("MCP_SANDTIMER_LOG_LEVEL");
    if (env) {
        level_ = ParseLevelName(env, level_);
    }
    if (!stream_) {
        std::cerr << "Failed to open log file: mcp-sandtimer.log" << std::endl;
    }
}

Logger::~Logger() {
    if (stream_.is_open()) {
        stream_.flush();
    }
}

void Logger::SetLevel(Level level) {
    auto& logger = Instance();
    std::lock_guard<std::mutex> lock(logger.mutex_);
    logger.level_ = level;
}

void Logger::SetLevel(const std::string& level_name) {
    auto& logger = Instance();
    std::lock_guard<std::mutex> lock(logger.mutex_);
    logger.level_ = ParseLevelName(level_name, logger.level_);
}

void Logger::Debug(const std::string& message) {
    Instance().Log(Level::DEBUG, message);
}

void Logger::Info(const std::string& message) {
    Instance().Log(Level::INFO, message);
}

void Logger::Error(const std::string& message) {
    Instance().Log(Level::ERROR, message);
}

void Logger::Log(Level level, const std::string& message) {
    if (!ShouldLog(level)) {
        return;
    }

    const std::string formatted = "[" + CurrentTimestamp() + "][" + LevelToString(level) + "] " + message;

    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_.is_open()) {
        stream_ << formatted << std::endl;
        stream_.flush();
    }
    if (level == Level::ERROR) {
        std::cerr << formatted << std::endl;
    }
}

bool Logger::ShouldLog(Level level) const {
    return static_cast<int>(level) >= static_cast<int>(level_);
}

std::string Logger::LevelToString(Level level) {
    switch (level) {
        case Level::DEBUG:
            return "DEBUG";
        case Level::INFO:
            return "INFO";
        case Level::ERROR:
            return "ERROR";
    }
    return "INFO";
}

Logger::Level Logger::ParseLevelName(const std::string& level_name, Level default_level) {
    std::string normalized;
    normalized.reserve(level_name.size());
    for (char ch : level_name) {
        normalized.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }

    if (normalized == "DEBUG") {
        return Level::DEBUG;
    }
    if (normalized == "INFO") {
        return Level::INFO;
    }
    if (normalized == "ERROR") {
        return Level::ERROR;
    }
    return default_level;
}

}  // namespace mcp_sandtimer

