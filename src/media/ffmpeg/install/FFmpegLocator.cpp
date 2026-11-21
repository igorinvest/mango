#include "FFmpegLocator.hpp"

#include <algorithm>
#include <cstdlib>
#include <ranges>
#include <string>

#include "../../../platform/ExecutablePath.hpp"

#if defined(__linux__)
#include "../../../configuration/Settings.hpp"
#include "../../../file/File.hpp"
#endif

namespace
{
#if defined(_WIN32)
constexpr char kPathSeparator = ';';
#else
constexpr char kPathSeparator = ':';
#endif

bool IsFile(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}
}  // namespace

std::vector<FFmpegCandidate> FFmpegLocator::Candidates()
{
    std::vector<FFmpegCandidate> candidates;
    if (auto bundled = Bundled())
    {
        candidates.push_back(std::move(*bundled));
    }

#if defined(__linux__)
    if (auto configured = Configured())
    {
        candidates.push_back(std::move(*configured));
    }
    if (auto fromPath = FromSystemPath())
    {
        candidates.push_back(std::move(*fromPath));
    }
#endif

    return Deduplicate(std::move(candidates));
}

std::optional<FFmpegCandidate> FFmpegLocator::InFolder(
    FFmpegInstall::Origin origin, const std::filesystem::path& folder)
{
    FFmpegCandidate candidate{.origin = origin,
        .ffmpeg = folder / ExecutableFileName("ffmpeg"),
        .ffprobe = folder / ExecutableFileName("ffprobe")};

    if (!IsFile(candidate.ffmpeg) || !IsFile(candidate.ffprobe))
    {
        return std::nullopt;
    }
    return candidate;
}

std::optional<FFmpegCandidate> FFmpegLocator::InSearchPath(std::string_view searchPath)
{
    for (const auto folder : searchPath | std::views::split(kPathSeparator))
    {
        const std::string_view name(folder.begin(), folder.end());
        if (name.empty())
        {
            continue;
        }
        if (auto candidate = InFolder(FFmpegInstall::Origin::systemPath, std::filesystem::path(name)))
        {
            return candidate;
        }
    }
    return std::nullopt;
}

std::vector<FFmpegCandidate> FFmpegLocator::Deduplicate(std::vector<FFmpegCandidate> candidates)
{
    std::vector<FFmpegCandidate> unique;
    std::vector<std::filesystem::path> seen;

    for (auto& candidate : candidates)
    {
        auto canonical = Canonical(candidate.ffmpeg);
        if (std::ranges::contains(seen, canonical))
        {
            continue;
        }
        seen.push_back(std::move(canonical));
        unique.push_back(std::move(candidate));
    }
    return unique;
}

std::optional<FFmpegCandidate> FFmpegLocator::Bundled()
{
    return InFolder(FFmpegInstall::Origin::bundled, ExecutableDirectory());
}

#if defined(__linux__)
std::optional<FFmpegCandidate> FFmpegLocator::Configured()
{
    const auto folder = Settings::GetStringSetting("ffmpegFolder");
    if (!folder || folder->empty())
    {
        return std::nullopt;
    }
    return InFolder(FFmpegInstall::Origin::configured, File::FromUtf8(*folder));
}

std::optional<FFmpegCandidate> FFmpegLocator::FromSystemPath()
{
    const char* searchPath = std::getenv("PATH");
    return searchPath ? InSearchPath(searchPath) : std::nullopt;
}
#endif

std::filesystem::path FFmpegLocator::Canonical(const std::filesystem::path& path)
{
    std::error_code ec;
    auto canonical = std::filesystem::canonical(path, ec);
    return ec ? path : canonical;
}
