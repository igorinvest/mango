#pragma once

#include <string_view>

// Ordered from most to least important: a level is enabled when it's at or
// above the logger's threshold (Debug is the only one that can be off).
enum class LogLevel
{
    Error,
    Warning,
    Info,
    Debug,
};

// The fixed set of categories, so the log stays searchable. The apps write
// with App, Playlist and UI (see Engine::Log).
namespace LogCategory
{
inline constexpr std::string_view Engine = "Engine";
inline constexpr std::string_view Server = "Server";
inline constexpr std::string_view Stream = "Stream";
inline constexpr std::string_view Transcode = "Transcode";
inline constexpr std::string_view Cast = "Cast";
inline constexpr std::string_view Discovery = "Discovery";
inline constexpr std::string_view Library = "Library";
inline constexpr std::string_view Subtitles = "Subtitles";
inline constexpr std::string_view PlaybackEnd = "PlaybackEnd";
inline constexpr std::string_view History = "History";
inline constexpr std::string_view Settings = "Settings";
inline constexpr std::string_view Remote = "Remote";
inline constexpr std::string_view App = "App";
inline constexpr std::string_view Playlist = "Playlist";
inline constexpr std::string_view UI = "UI";
}  // namespace LogCategory
