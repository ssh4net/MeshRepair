#include "include/logger.h"

#include <atomic>
#include <iostream>
#include <string>

#if defined(MESHREPAIR_USE_SPDLOG)
#    include <spdlog/spdlog.h>
#    include <spdlog/sinks/stdout_color_sinks.h>
#endif

namespace MeshRepair {

namespace {

    std::atomic<LogLevel> g_minLevel { LogLevel::Info };
#if defined(MESHREPAIR_USE_SPDLOG)
    std::atomic<bool> g_loggerConfigured { false };
#endif
    std::string g_prefix;
    bool g_useColors = true;

    const char* categoryToString(LogCategory category)
    {
        switch (category) {
        case LogCategory::Cli: return "cli";
        case LogCategory::Engine: return "engine";
        case LogCategory::Preprocess: return "preprocess";
        case LogCategory::Fill: return "fill";
        case LogCategory::Progress: return "progress";
        case LogCategory::Empty: return "";
        default: return "Log";
        }
    }

#if defined(MESHREPAIR_USE_SPDLOG)
    spdlog::level::level_enum toSpdLevel(LogLevel level)
    {
        switch (level) {
        case LogLevel::Error: return spdlog::level::err;
        case LogLevel::Warn: return spdlog::level::warn;
        case LogLevel::Info: return spdlog::level::info;
        case LogLevel::Detail: return spdlog::level::info;
        case LogLevel::Debug: return spdlog::level::debug;
        default: return spdlog::level::info;
        }
    }

    spdlog::level::level_enum toSpdMinLevel(LogLevel level)
    {
        switch (level) {
        case LogLevel::Error: return spdlog::level::err;
        case LogLevel::Warn: return spdlog::level::warn;
        case LogLevel::Info: return spdlog::level::info;
        case LogLevel::Detail: return spdlog::level::info;
        case LogLevel::Debug: return spdlog::level::trace;
        default: return spdlog::level::info;
        }
    }
#endif

}  // namespace

void
initLogger(const LoggerConfig& config)
{
    g_minLevel.store(config.minLevel, std::memory_order_relaxed);
    g_prefix = config.prefix;
    g_useColors = config.useColors;

#if defined(MESHREPAIR_USE_SPDLOG)
    const std::string logger_name = "meshrepair";
    spdlog::drop(logger_name);

    std::shared_ptr<spdlog::logger> logger;
    logger = config.useStderr ? spdlog::stderr_color_mt(logger_name) : spdlog::stdout_color_mt(logger_name);

    std::string basePattern = config.useColors ? "%^%l:%$ %v" : "%l: %v";
    if (!g_prefix.empty()) {
        basePattern = g_prefix + " " + basePattern;
    }
    logger->set_pattern(basePattern);
    logger->set_level(toSpdMinLevel(config.minLevel));
    logger->flush_on(spdlog::level::err);
    spdlog::set_level(toSpdMinLevel(config.minLevel));
    spdlog::set_default_logger(logger);
    g_loggerConfigured.store(true, std::memory_order_relaxed);
#else
    (void)config;
#endif
}

void
setLogLevel(LogLevel level)
{
    g_minLevel.store(level, std::memory_order_relaxed);

#if defined(MESHREPAIR_USE_SPDLOG)
    if (g_loggerConfigured.load(std::memory_order_relaxed)) {
        auto spd_level = toSpdMinLevel(level);
        spdlog::set_level(spd_level);
        if (auto logger = spdlog::default_logger()) {
            logger->set_level(spd_level);
        }
    }
#endif
}

LogLevel
getLogLevel()
{
    return g_minLevel.load(std::memory_order_relaxed);
}

void
logMessage(LogCategory category, LogLevel level, std::string_view message)
{
    LogLevel currentMin = g_minLevel.load(std::memory_order_relaxed);
    if (category == LogCategory::Preprocess && currentMin < LogLevel::Debug) {
        return;
    }
    if (static_cast<int>(level) > static_cast<int>(currentMin)) {
        return;
    }

#if defined(MESHREPAIR_USE_SPDLOG)
    if (g_loggerConfigured.load(std::memory_order_relaxed)) {
        auto spdLevel = toSpdLevel(level);
        std::string categoryStr = categoryToString(category);
        if (!categoryStr.empty())
            spdlog::log(spdLevel, "({}) {}", categoryStr, message);
        else
            spdlog::log(spdLevel, "{}", message);

        return;
    }
#endif

    const char* prefix = categoryToString(category);
    const char* levelStr;
    switch (level) {
    case LogLevel::Error: levelStr = "ERROR"; break;
    case LogLevel::Warn: levelStr = "WARN"; break;
    case LogLevel::Info: levelStr = "INFO"; break;
    case LogLevel::Detail: levelStr = "DETAIL"; break;
    case LogLevel::Debug: levelStr = "DEBUG"; break;
    default: levelStr = "INFO"; break;
    }

    std::ostream& os = std::cerr;
    if (!g_prefix.empty()) {
        os << g_prefix << " ";
    }
    os << "[" << levelStr << "] " << prefix << ": " << message << '\n';
}

LogLevel
logLevelFromVerbosity(int verbosity)
{
    if (verbosity <= 0) {
        return LogLevel::Warn;
    }
    if (verbosity == 1) {
        return LogLevel::Info;
    }
    if (verbosity == 2) {
        return LogLevel::Info;
    }
    if (verbosity == 3) {
        return LogLevel::Detail;
    }
    return LogLevel::Debug;
}

}  // namespace MeshRepair
