#include "FFmpegCapabilityProbe.hpp"

#include <charconv>
#include <ranges>

#include "../../../file/File.hpp"
#include "../../../log/Log.hpp"
#include "../process/ToolOutput.hpp"

std::optional<FFmpegInstall> FFmpegCapabilityProbe::Read(const FFmpegCandidate& candidate)
{
    FFmpegInstall install{
        .origin = candidate.origin, .ffmpeg = candidate.ffmpeg, .ffprobe = candidate.ffprobe};
    ReadLists(install);

    if (install.origin == FFmpegInstall::Origin::bundled)
    {
        return install;
    }

    if (const auto reason = RejectionReason(install); !reason.empty())
    {
        Log::Warning(LogCategory::Transcode, "ffmpeg at {} not used: {}",
            File::ToUtf8(install.ffmpeg), reason);
        return std::nullopt;
    }
    return install;
}

bool FFmpegCapabilityProbe::EncoderHasOption(
    const FFmpegInstall& install, const std::string& encoder, std::string_view option)
{
    const auto help = ToolOutput::Run(install.ffmpeg, {"-hide_banner", "-h", "encoder=" + encoder});
    return help && HelpHasOption(*help, option);
}

void FFmpegCapabilityProbe::ReadLists(FFmpegInstall& install)
{
    if (const auto output = ToolOutput::Run(install.ffmpeg, {"-hide_banner", "-version"}))
    {
        install.version = ParseVersionOutput(*output);
    }
    if (const auto output = ToolOutput::Run(install.ffmpeg, {"-hide_banner", "-codecs"}))
    {
        install.encodersByCodec = ParseCodecs(*output);
    }
    if (const auto output = ToolOutput::Run(install.ffmpeg, {"-hide_banner", "-muxers"}))
    {
        install.muxers = ParseMuxers(*output);
    }
}

std::string FFmpegCapabilityProbe::RejectionReason(const FFmpegInstall& install)
{
    const auto version = ParseVersion(install.version);
    if (!version)
    {
        return install.version.empty() ? "ffmpeg doesn't run"
                                       : "unknown version \"" + install.version + "\"";
    }
    if (!IsNewEnough(*version))
    {
        return "version " + install.version + " is older than " +
            std::to_string(kMinimumVersion.major) + "." + std::to_string(kMinimumVersion.minor);
    }
    if (!ProbeRuns(install))
    {
        return "ffprobe doesn't run";
    }
    return {};
}

bool FFmpegCapabilityProbe::ProbeRuns(const FFmpegInstall& install)
{
    return ToolOutput::Run(install.ffprobe, {"-hide_banner", "-version"}).has_value();
}

std::string FFmpegCapabilityProbe::ParseVersionOutput(std::string_view output)
{
    constexpr std::string_view marker = "version ";
    const auto start = output.find(marker);
    if (start == std::string_view::npos)
    {
        return {};
    }

    const auto words = Words(output.substr(start + marker.size()));
    return words.empty() ? std::string{} : std::string(words.front());
}

std::optional<FFmpegVersion> FFmpegCapabilityProbe::ParseVersion(std::string_view version)
{
    if (version.starts_with("N-"))
    {
        return FFmpegVersion{.major = 0, .minor = 0, .gitBuild = true};
    }
    if (version.starts_with('n'))
    {
        version.remove_prefix(1);
    }

    FFmpegVersion parsed;
    const auto* end = version.data() + version.size();
    const auto major = std::from_chars(version.data(), end, parsed.major);
    if (major.ec != std::errc{})
    {
        return std::nullopt;
    }
    if (major.ptr != end && *major.ptr == '.')
    {
        // A missing minor ("7") leaves it 0.
        std::from_chars(major.ptr + 1, end, parsed.minor);
    }
    return parsed;
}

bool FFmpegCapabilityProbe::IsNewEnough(const FFmpegVersion& version)
{
    if (version.gitBuild)
    {
        return true;
    }
    return version.major != kMinimumVersion.major ? version.major > kMinimumVersion.major
                                                   : version.minor >= kMinimumVersion.minor;
}

std::map<std::string, std::vector<std::string>> FFmpegCapabilityProbe::ParseCodecs(
    std::string_view output)
{
    std::map<std::string, std::vector<std::string>> encodersByCodec;

    for (const auto row : TableRows(output))
    {
        // " DEV.LS h264   H.264 / AVC ... (encoders: libx264 h264_nvenc)"
        const auto words = Words(row);
        if (words.size() < 2 || words[0].size() < 2 || words[0][1] != 'E')
        {
            continue;
        }

        const std::string codec(words[1]);
        auto listed = Parenthesized(row, "encoders");
        auto& encoders = encodersByCodec[codec];
        if (listed.empty())
        {
            encoders.push_back(codec);
            continue;
        }
        for (const auto encoder : listed)
        {
            encoders.emplace_back(encoder);
        }
    }
    return encodersByCodec;
}

std::set<std::string> FFmpegCapabilityProbe::ParseMuxers(std::string_view output)
{
    std::set<std::string> muxers;

    for (const auto row : TableRows(output))
    {
        // "  E  dash   DASH Muxer" (newer) or " E dash   DASH Muxer" (older):
        // the flags column holds only D, E, d and dots.
        const auto words = Words(row);
        if (words.size() < 2 || !words[0].contains('E') ||
            words[0].find_first_not_of("DEd.") != std::string_view::npos)
        {
            continue;
        }
        for (const auto name : words[1] | std::views::split(','))
        {
            muxers.emplace(name.begin(), name.end());
        }
    }
    return muxers;
}

bool FFmpegCapabilityProbe::HelpHasOption(std::string_view helpOutput, std::string_view option)
{
    for (const auto line : helpOutput | std::views::split('\n'))
    {
        const auto words = Words(std::string_view(line.begin(), line.end()));
        if (!words.empty() && words[0].size() == option.size() + 1 && words[0].starts_with('-') &&
            words[0].substr(1) == option)
        {
            return true;
        }
    }
    return false;
}

std::vector<std::string_view> FFmpegCapabilityProbe::TableRows(std::string_view output)
{
    std::vector<std::string_view> rows;
    bool inTable = false;

    for (const auto range : output | std::views::split('\n'))
    {
        std::string_view line(range.begin(), range.end());
        if (line.ends_with('\r'))
        {
            line.remove_suffix(1);
        }

        const auto words = Words(line);
        if (!inTable)
        {
            inTable = words.size() == 1 && words[0].starts_with("--");
            continue;
        }
        if (!words.empty())
        {
            rows.push_back(line);
        }
    }
    return rows;
}

std::vector<std::string_view> FFmpegCapabilityProbe::Words(std::string_view text)
{
    std::vector<std::string_view> words;
    for (const auto range : text | std::views::split(' '))
    {
        std::string_view word(range.begin(), range.end());
        if (!word.empty() && word != "\r" && word != "\t")
        {
            words.push_back(word);
        }
    }
    return words;
}

std::vector<std::string_view> FFmpegCapabilityProbe::Parenthesized(
    std::string_view row, std::string_view label)
{
    const auto marker = "(" + std::string(label) + ":";
    const auto start = row.find(marker);
    if (start == std::string_view::npos)
    {
        return {};
    }

    const auto contentStart = start + marker.size();
    const auto end = row.find(')', contentStart);
    return Words(row.substr(contentStart,
        end == std::string_view::npos ? std::string_view::npos : end - contentStart));
}
