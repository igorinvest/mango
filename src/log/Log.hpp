#pragma once

#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "LogLevel.hpp"

// The engine-wide log. While a stream is current its lines go to
// streams/<fastHash>/mango.log (next to ffmpeg.log); otherwise only to the
// carry-over buffer that's written at the top of the next stream's log.
// Debug builds also mirror every line to stderr. Never blocks on I/O.
//
// Messages are formatted at the call site:
//   Log::Info(LogCategory::Cast, "LOAD sent to {}: {}", device, title);
// The formatting overloads skip building a disabled (Debug) line entirely.
namespace Log
{

[[nodiscard]] bool IsEnabled(LogLevel level);

void Write(LogLevel level, std::string_view category, std::string_view message);

inline void Error(std::string_view category, std::string_view message)
{
    Write(LogLevel::Error, category, message);
}
inline void Warning(std::string_view category, std::string_view message)
{
    Write(LogLevel::Warning, category, message);
}
inline void Info(std::string_view category, std::string_view message)
{
    Write(LogLevel::Info, category, message);
}
inline void Debug(std::string_view category, std::string_view message)
{
    Write(LogLevel::Debug, category, message);
}

template <typename... Args>
void Format(LogLevel level, std::string_view category, std::format_string<Args...> format, Args&&... args)
{
    if (IsEnabled(level))
    {
        Write(level, category, std::format(format, std::forward<Args>(args)...));
    }
}

template <typename... Args>
void Error(std::string_view category, std::format_string<Args...> format, Args&&... args)
{
    Format(LogLevel::Error, category, format, std::forward<Args>(args)...);
}
template <typename... Args>
void Warning(std::string_view category, std::format_string<Args...> format, Args&&... args)
{
    Format(LogLevel::Warning, category, format, std::forward<Args>(args)...);
}
template <typename... Args>
void Info(std::string_view category, std::format_string<Args...> format, Args&&... args)
{
    Format(LogLevel::Info, category, format, std::forward<Args>(args)...);
}
template <typename... Args>
void Debug(std::string_view category, std::format_string<Args...> format, Args&&... args)
{
    Format(LogLevel::Debug, category, format, std::forward<Args>(args)...);
}

// StreamManager::StartStream, once the folder exists. streamDescription
// (title, fastHash, tracks) goes into the file's header.
void OpenStreamLog(const std::filesystem::path& streamFolder, std::string_view streamDescription);

// StreamManager::StopCurrentStream, before deleting the folder. Returns
// once the file is closed.
void CloseStreamLog();

// Returns once everything logged so far is written.
void Flush();

// Shown in each stream log's header; set by the native app at startup.
void SetAppVersion(std::string_view version);

// "error", "warning", "info", "debug" (any case); std::nullopt otherwise.
// For Engine::Log, whose callers pass the level as a string.
[[nodiscard]] std::optional<LogLevel> LevelFromString(std::string_view level);

inline constexpr std::string_view kStreamLogFileName = "mango.log";

}  // namespace Log
