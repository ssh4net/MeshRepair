#pragma once

#include <string_view>

namespace MeshRepair {

enum class LogLevel {
    Error = 0,
    Warn,
    Info,
    Detail,
    Debug,
};

enum class LogCategory {
    Cli,
    Engine,
    Preprocess,
    Fill,
    Progress,
    Empty,
};

struct LoggerConfig {
    LogLevel minLevel = LogLevel::Info;
    bool useStderr    = true;
    bool async        = true;
};

void
initLogger(const LoggerConfig& config);
void
setLogLevel(LogLevel level);
LogLevel
getLogLevel();

void
logMessage(LogCategory category, LogLevel level, std::string_view message);

inline void
logError(LogCategory category, std::string_view message)
{
    logMessage(category, LogLevel::Error, message);
}

inline void
logWarn(LogCategory category, std::string_view message)
{
    logMessage(category, LogLevel::Warn, message);
}

inline void
logInfo(LogCategory category, std::string_view message)
{
    logMessage(category, LogLevel::Info, message);
}

inline void
logDetail(LogCategory category, std::string_view message)
{
    logMessage(category, LogLevel::Detail, message);
}

inline void
logDebug(LogCategory category, std::string_view message)
{
    logMessage(category, LogLevel::Debug, message);
}

LogLevel
logLevelFromVerbosity(int verbosity);

}  // namespace MeshRepair
