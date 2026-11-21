#include "Log.hpp"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <vector>

#include "../configuration/Settings.hpp"
#include "../platform/OsVersion.hpp"
#include "Logger.hpp"

#ifndef MANGO_ENGINE_VERSION
#define MANGO_ENGINE_VERSION "unknown"
#endif

namespace
{

#ifdef NDEBUG
constexpr bool kDebugBuild = false;
#else
constexpr bool kDebugBuild = true;
#endif

// Debug level is always on in Debug builds; a Release build turns it on
// with "logDebug": true in settings.json (edited by hand).
LoggerOptions GlobalOptions()
{
    LoggerOptions options;
    options.debugEnabled = kDebugBuild || Settings::GetBoolSetting("logDebug").value_or(false);
    options.mirrorToStderr = kDebugBuild;
    return options;
}

// Never destroyed: lines can still be logged from other statics' destructors
// and detached threads while the process exits.
Logger& GlobalLogger()
{
    static Logger* logger = new Logger(GlobalOptions());
    return *logger;
}

std::mutex appVersionMutex;
std::string appVersion = "unknown";

std::string AppVersion()
{
    std::lock_guard lock(appVersionMutex);
    return appVersion;
}

std::string HeaderLine()
{
    return std::format("Mango log: app {}, engine {}, {}, debug level {}", AppVersion(),
        MANGO_ENGINE_VERSION, OsVersion(), GlobalLogger().IsEnabled(LogLevel::Debug) ? "on" : "off");
}

}  // namespace

namespace Log
{

bool IsEnabled(LogLevel level) { return GlobalLogger().IsEnabled(level); }

void Write(LogLevel level, std::string_view category, std::string_view message)
{
    GlobalLogger().Write(level, category, message);
}

void OpenStreamLog(const std::filesystem::path& streamFolder, std::string_view streamDescription)
{
    std::vector<std::string> header{HeaderLine(), std::format("Stream: {}", streamDescription)};
    GlobalLogger().Open(streamFolder / kStreamLogFileName, std::move(header));
}

void CloseStreamLog() { GlobalLogger().Close(); }

void Flush() { GlobalLogger().Flush(); }

void SetAppVersion(std::string_view version)
{
    std::lock_guard lock(appVersionMutex);
    appVersion = version;
}

std::optional<LogLevel> LevelFromString(std::string_view level)
{
    std::string lower(level);
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower == "error")
    {
        return LogLevel::Error;
    }
    if (lower == "warning")
    {
        return LogLevel::Warning;
    }
    if (lower == "info")
    {
        return LogLevel::Info;
    }
    if (lower == "debug")
    {
        return LogLevel::Debug;
    }
    return std::nullopt;
}

}  // namespace Log
