#include "finevk/core/logging.hpp"

#include <cstdio>
#include <ctime>
#include <chrono>

namespace finevk {

Logger& Logger::global() {
    static Logger instance;
    return instance;
}

void Logger::log(LogLevel level, LogCategory category, std::string_view message,
                 const char* file, int line) {
    if (level < minLevel_) {
        return;
    }

    // Get current time
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    char timeStr[32];
    std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", std::localtime(&time));

    // Select output stream based on level
    FILE* out = (level >= LogLevel::Warning) ? stderr : stdout;

    // Format: [TIME.ms] [LEVEL] [CATEGORY] message
    if (file && line > 0) {
        std::fprintf(out, "[%s.%03d] [%-7s] [%-11s] %.*s (%s:%d)\n",
                     timeStr, static_cast<int>(ms.count()),
                     levelToString(level), categoryToString(category),
                     static_cast<int>(message.size()), message.data(),
                     file, line);
    } else {
        std::fprintf(out, "[%s.%03d] [%-7s] [%-11s] %.*s\n",
                     timeStr, static_cast<int>(ms.count()),
                     levelToString(level), categoryToString(category),
                     static_cast<int>(message.size()), message.data());
    }

    std::fflush(out);
}

void Logger::trace(LogCategory category, std::string_view message) {
    log(LogLevel::Trace, category, message);
}

void Logger::debug(LogCategory category, std::string_view message) {
    log(LogLevel::Debug, category, message);
}

void Logger::info(LogCategory category, std::string_view message) {
    log(LogLevel::Info, category, message);
}

void Logger::warning(LogCategory category, std::string_view message) {
    log(LogLevel::Warning, category, message);
}

void Logger::error(LogCategory category, std::string_view message) {
    log(LogLevel::Error, category, message);
}

void Logger::fatal(LogCategory category, std::string_view message) {
    log(LogLevel::Fatal, category, message);
}

void Logger::vulkanMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                           VkDebugUtilsMessageTypeFlagsEXT /*type*/,
                           const VkDebugUtilsMessengerCallbackDataEXT* data) {
    LogLevel level;
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        level = LogLevel::Error;
    } else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        level = LogLevel::Warning;
    } else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        level = LogLevel::Info;
    } else {
        level = LogLevel::Trace;
    }

    log(level, LogCategory::Vulkan, data->pMessage);
}

const char* Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:   return "TRACE";
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARNING";
        case LogLevel::Error:   return "ERROR";
        case LogLevel::Fatal:   return "FATAL";
    }
    return "UNKNOWN";
}

const char* Logger::categoryToString(LogCategory category) {
    switch (category) {
        case LogCategory::Core:        return "Core";
        case LogCategory::Vulkan:      return "Vulkan";
        case LogCategory::Resource:    return "Resource";
        case LogCategory::Render:      return "Render";
        case LogCategory::Game:        return "Game";
        case LogCategory::Performance: return "Performance";
    }
    return "Unknown";
}

} // namespace finevk
