#pragma once

#include <string>
#include <string_view>
#include <cstdio>
#include <vulkan/vulkan.h>

namespace finevk {

// Log levels
enum class LogLevel {
    Trace,      // Very verbose debugging
    Debug,      // Debug information
    Info,       // Informational messages
    Warning,    // Potential problems
    Error,      // Errors that allow recovery
    Fatal       // Unrecoverable errors
};

// Log categories
enum class LogCategory {
    Core,       // Library core systems
    Vulkan,     // Vulkan API calls and validation
    Resource,   // Resource loading
    Render,     // Rendering operations
    Game,       // General game logic
    Performance // Performance warnings
};

// Simple logging interface - can be expanded later with sinks
class Logger {
public:
    static Logger& global();

    void setMinLevel(LogLevel level) { minLevel_ = level; }
    LogLevel minLevel() const { return minLevel_; }

    void log(LogLevel level, LogCategory category, std::string_view message,
             const char* file = nullptr, int line = 0);

    // Convenience methods
    void trace(LogCategory category, std::string_view message);
    void debug(LogCategory category, std::string_view message);
    void info(LogCategory category, std::string_view message);
    void warning(LogCategory category, std::string_view message);
    void error(LogCategory category, std::string_view message);
    void fatal(LogCategory category, std::string_view message);

    // Vulkan validation message integration
    void vulkanMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                       VkDebugUtilsMessageTypeFlagsEXT type,
                       const VkDebugUtilsMessengerCallbackDataEXT* data);

private:
    Logger() = default;
    LogLevel minLevel_ = LogLevel::Debug;

    static const char* levelToString(LogLevel level);
    static const char* categoryToString(LogCategory category);
};

// Logging macros with file/line info
#define FINEVK_LOG(level, category, msg) \
    finevk::Logger::global().log(level, category, msg, __FILE__, __LINE__)

#define FINEVK_TRACE(category, msg)   FINEVK_LOG(finevk::LogLevel::Trace, category, msg)
#define FINEVK_DEBUG(category, msg)   FINEVK_LOG(finevk::LogLevel::Debug, category, msg)
#define FINEVK_INFO(category, msg)    FINEVK_LOG(finevk::LogLevel::Info, category, msg)
#define FINEVK_WARN(category, msg)    FINEVK_LOG(finevk::LogLevel::Warning, category, msg)
#define FINEVK_ERROR(category, msg)   FINEVK_LOG(finevk::LogLevel::Error, category, msg)
#define FINEVK_FATAL(category, msg)   FINEVK_LOG(finevk::LogLevel::Fatal, category, msg)

} // namespace finevk
