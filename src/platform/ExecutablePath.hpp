#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// Directory containing the currently running executable, so bundled tools
// (ffmpeg/ffprobe) shipped alongside it can be found regardless of the
// process's current working directory. Backed by ExecutablePath_Apple.cpp,
// ExecutablePath_Linux.cpp and ExecutablePath_Windows.cpp.
std::filesystem::path ExecutableDirectory();

// A tool's file name with the platform's executable suffix (".exe" on
// Windows, none elsewhere).
std::string ExecutableFileName(std::string_view toolName);

// Resolves a tool bundled next to the running executable.
std::filesystem::path ResolveBundledTool(std::string_view toolName);
