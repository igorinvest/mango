#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "FFmpegInstall.hpp"

// Where an ffmpeg + ffprobe pair was found, before FFmpegCapabilityProbe
// reads what it has.
struct FFmpegCandidate
{
    FFmpegInstall::Origin origin{FFmpegInstall::Origin::bundled};
    std::filesystem::path ffmpeg;
    std::filesystem::path ffprobe;
};

// Finds the candidate installs, in priority order: the bundled one next to
// the executable, then (Linux only) settings.json "ffmpegFolder", then the
// first PATH folder with both tools. Two candidates resolving to the same
// file count as one.
class FFmpegLocator
{
   public:
    [[nodiscard]] static std::vector<FFmpegCandidate> Candidates();

    // Both tools in folder, or std::nullopt.
    [[nodiscard]] static std::optional<FFmpegCandidate> InFolder(
        FFmpegInstall::Origin origin, const std::filesystem::path& folder);

    // The first folder in a PATH-style list that has both tools.
    [[nodiscard]] static std::optional<FFmpegCandidate> InSearchPath(std::string_view searchPath);

    // Drops later candidates whose ffmpeg is the same file as an earlier one's.
    [[nodiscard]] static std::vector<FFmpegCandidate> Deduplicate(
        std::vector<FFmpegCandidate> candidates);

   private:
    [[nodiscard]] static std::optional<FFmpegCandidate> Bundled();

#if defined(__linux__)
    [[nodiscard]] static std::optional<FFmpegCandidate> Configured();
    [[nodiscard]] static std::optional<FFmpegCandidate> FromSystemPath();
#endif

    [[nodiscard]] static std::filesystem::path Canonical(const std::filesystem::path& path);
};
