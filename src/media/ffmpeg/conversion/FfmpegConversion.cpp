#include "FfmpegConversion.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <utility>

#include "FFmpegArgumentFormatter.hpp"

namespace
{
// Only the last progress block's "out_time=" is ever needed, and the file
// grows by one block roughly every 0.5s, so only its end is read.
constexpr std::size_t kProgressTailBytes = 4096;

// ffmpeg's verbose log is long, but the error that ended a run is at its
// end.
constexpr std::size_t kLogTailBytes = 16384;

// Parses ffmpeg's "HH:MM:SS.micro" timestamp into seconds.
std::optional<double> ParseTimestamp(const std::string& value)
{
    const char* cursor = value.c_str();
    char* end = nullptr;

    const long hours = std::strtol(cursor, &end, 10);
    if (end == cursor || *end != ':')
    {
        return std::nullopt;
    }

    cursor = end + 1;
    const long minutes = std::strtol(cursor, &end, 10);
    if (end == cursor || *end != ':')
    {
        return std::nullopt;
    }

    cursor = end + 1;
    const double seconds = std::strtod(cursor, &end);
    if (end == cursor)
    {
        return std::nullopt;
    }

    return static_cast<double>(hours) * 3600.0 + static_cast<double>(minutes) * 60.0 + seconds;
}

struct LogPattern
{
    std::string_view text;
    ConversionError error;
};

// First match wins, so the more specific causes come first.
constexpr std::array kLogPatterns = {
    LogPattern{"Unknown encoder", ConversionError::NoVideoEncoder},
    LogPattern{"Encoder not found", ConversionError::NoVideoEncoder},
    LogPattern{"Error while opening encoder", ConversionError::NoVideoEncoder},
    LogPattern{"Could not open encoder", ConversionError::NoVideoEncoder},
    LogPattern{"Decoder not found", ConversionError::UnsupportedCodec},
    LogPattern{"Unknown decoder", ConversionError::UnsupportedCodec},
    LogPattern{"Unsupported codec", ConversionError::UnsupportedCodec},
    LogPattern{"Could not find codec parameters", ConversionError::UnsupportedCodec},
    LogPattern{"Error opening input", ConversionError::FileUnreadable},
    LogPattern{"Invalid data found when processing input", ConversionError::FileUnreadable},
    LogPattern{"Error opening output", ConversionError::OutputFailed},
    LogPattern{"Could not write header", ConversionError::OutputFailed},
    LogPattern{"No space left on device", ConversionError::OutputFailed},
    LogPattern{"Permission denied", ConversionError::OutputFailed},
    LogPattern{"failed to rename", ConversionError::OutputFailed},
};
}  // namespace

std::unique_ptr<FfmpegConversion> FfmpegConversion::Start(const FFmpegInstall& install,
    const std::vector<std::string>& arguments,
    const std::filesystem::path& outputFolder, ConversionExitCallback onExit)
{
    auto exitState = std::make_shared<ExitState>();

    auto process = FFmpegProcess::Start(install,
        arguments,
        [exitState, outputFolder, onExit = std::move(onExit)](bool completedSuccessfully)
        {
            if (!onExit)
            {
                return;
            }
            if (completedSuccessfully)
            {
                onExit({});
                return;
            }
            onExit(std::unexpected(FailureReason(*exitState, outputFolder)));
        },
        outputFolder);

    if (!process)
    {
        return nullptr;
    }

    auto conversion =
        std::unique_ptr<FfmpegConversion>(new FfmpegConversion(outputFolder, std::move(exitState)));
    conversion->m_process = std::move(process);
    return conversion;
}

FfmpegConversion::FfmpegConversion(
    std::filesystem::path outputFolder, std::shared_ptr<ExitState> exitState)
    : m_outputFolder(std::move(outputFolder)), m_exitState(std::move(exitState))
{
}

void FfmpegConversion::Stop()
{
    m_exitState->stopRequested = true;
    m_process->Stop();
}

void FfmpegConversion::Wait() { m_process->Wait(); }

std::optional<double> FfmpegConversion::ProgressSeconds() const
{
    return ParseLatestOutTime(
        ReadTail(m_outputFolder / FFmpegArgumentFormatter::kProgressFileName, kProgressTailBytes));
}

std::optional<double> FfmpegConversion::ParseLatestOutTime(std::string_view progressTail)
{
    constexpr std::string_view key = "out_time=";
    const auto keyPos = progressTail.rfind(key);
    if (keyPos == std::string_view::npos)
    {
        return std::nullopt;
    }

    const auto valueStart = keyPos + key.size();
    const auto lineEnd = progressTail.find('\n', valueStart);
    const std::string value(progressTail.substr(valueStart,
        (lineEnd == std::string_view::npos ? progressTail.size() : lineEnd) - valueStart));

    return ParseTimestamp(value);
}

ConversionError FfmpegConversion::ClassifyLog(std::string_view logTail)
{
    const auto match = std::ranges::find_if(
        kLogPatterns, [logTail](const LogPattern& pattern) { return logTail.contains(pattern.text); });
    return match != kLogPatterns.end() ? match->error : ConversionError::Unknown;
}

ConversionError FfmpegConversion::FailureReason(
    const ExitState& exitState, const std::filesystem::path& outputFolder)
{
    if (exitState.stopRequested)
    {
        return ConversionError::Cancelled;
    }
    return ClassifyLog(ReadTail(outputFolder / FFmpegProcess::kLogFileName, kLogTailBytes));
}

std::string FfmpegConversion::ReadTail(const std::filesystem::path& path, std::size_t maxBytes)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return {};
    }

    file.seekg(0, std::ios::end);
    const auto size = static_cast<std::streamoff>(file.tellg());
    if (size <= 0)
    {
        return {};
    }

    const auto readSize = std::min<std::streamoff>(static_cast<std::streamoff>(maxBytes), size);
    file.seekg(-readSize, std::ios::end);

    std::string tail(static_cast<std::size_t>(readSize), '\0');
    file.read(tail.data(), readSize);
    tail.resize(static_cast<std::size_t>(file.gcount()));
    return tail;
}
