#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../../MediaJob.hpp"
#include "../install/FFmpegInstall.hpp"

class ChildProcess;

class FFmpegProcess final : public MediaJob
{
   public:
    // ffmpeg's stderr (progress and error diagnostics), written into
    // workingDirectory when one is given — without it, a transcode that
    // fails for a reason specific to one file leaves no trace.
    static constexpr const char* kLogFileName = "ffmpeg.log";

    // onExit fires exactly once, from an internal monitor thread, the moment
    // the child exits (naturally or via Stop()) — argument is true only when
    // it exited on its own with a zero status, i.e. the transcode finished.
    //
    // Runs install's ffmpeg. nullptr when it can't be started.
    //
    // workingDirectory, when given, becomes ffmpeg's current directory.
    // ffmpeg's dash muxer resolves relative -init_seg_name/-media_seg_name
    // filenames against its own cwd rather than against the (absolute)
    // manifest path also on the command line, so callers that rely on those
    // relative segment names landing next to the manifest must pass the
    // manifest's directory here.
    static std::unique_ptr<FFmpegProcess> Start(const FFmpegInstall& install,
        const std::vector<std::string>& arguments,
        std::function<void(bool completedSuccessfully)> onExit = nullptr,
        std::optional<std::filesystem::path> workingDirectory = std::nullopt);

    ~FFmpegProcess() override;

    FFmpegProcess(const FFmpegProcess&) = delete;
    FFmpegProcess& operator=(const FFmpegProcess&) = delete;

    // The underlying ChildProcess is pinned to its address, so instances must
    // be too — not used anywhere today, but keeping it would be unsafe.
    FFmpegProcess(FFmpegProcess&&) = delete;
    FFmpegProcess& operator=(FFmpegProcess&&) = delete;

    void Stop() override;

    void Wait() override;

    bool IsRunning() const;

   private:
    explicit FFmpegProcess(std::unique_ptr<ChildProcess> process);

    std::unique_ptr<ChildProcess> m_process;
};
