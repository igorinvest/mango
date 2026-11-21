#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../../MediaJob.hpp"
#include "../process/FFmpegProcess.hpp"

// A stream conversion run by one ffmpeg process. Reports progress from the
// "-progress" file FFmpegArgumentFormatter points ffmpeg at, and on failure
// reads ffmpeg.log to say why.
class FfmpegConversion final : public Conversion
{
   public:
    // Runs install's ffmpeg. nullptr when it can't be launched (onExit
    // never fires then).
    [[nodiscard]] static std::unique_ptr<FfmpegConversion> Start(const FFmpegInstall& install,
        const std::vector<std::string>& arguments, const std::filesystem::path& outputFolder,
        ConversionExitCallback onExit);

    void Stop() override;
    void Wait() override;
    [[nodiscard]] std::optional<double> ProgressSeconds() const override;

    // The latest "out_time=" (HH:MM:SS.micro) in a tail of ffmpeg's
    // progress file, in seconds.
    [[nodiscard]] static std::optional<double> ParseLatestOutTime(std::string_view progressTail);

    // Maps the end of ffmpeg's log to the reason it failed.
    [[nodiscard]] static ConversionError ClassifyLog(std::string_view logTail);

   private:
    // Shared with the exit callback, which can fire before Start returns.
    struct ExitState
    {
        std::atomic<bool> stopRequested{false};
    };

    FfmpegConversion(std::filesystem::path outputFolder, std::shared_ptr<ExitState> exitState);

    // The reason a run that didn't complete ended.
    [[nodiscard]] static ConversionError FailureReason(
        const ExitState& exitState, const std::filesystem::path& outputFolder);

    // The last maxBytes of a file; empty when it can't be read.
    [[nodiscard]] static std::string ReadTail(const std::filesystem::path& path, std::size_t maxBytes);

    std::filesystem::path m_outputFolder;
    std::shared_ptr<ExitState> m_exitState;
    std::unique_ptr<FFmpegProcess> m_process;
};
