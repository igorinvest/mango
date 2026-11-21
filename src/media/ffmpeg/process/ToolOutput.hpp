#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Runs a short command-line tool (ffmpeg -codecs, ffprobe -version, ...) to
// completion and returns what it printed. ChildProcess only redirects to
// files, so stdout goes through a temporary file.
class ToolOutput
{
   public:
    // stdout, when the tool exited with status 0; std::nullopt otherwise.
    [[nodiscard]] static std::optional<std::string> Run(
        const std::filesystem::path& executable, const std::vector<std::string>& arguments);

   private:
    // A file name no other run at the same time uses.
    [[nodiscard]] static std::filesystem::path TemporaryPath();

    [[nodiscard]] static std::string ReadAll(const std::filesystem::path& path);
};
