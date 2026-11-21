#include "FFmpegProcess.hpp"

#include <format>

#include "../../../file/File.hpp"
#include "../../../platform/ChildProcess.hpp"
#include "../../../log/Log.hpp"

namespace
{

// The full command line: with ffmpeg.log next to it, enough to rerun a
// failed conversion by hand.
void LogStart(const std::filesystem::path& executable, const std::vector<std::string>& arguments, const std::optional<std::filesystem::path>& workingDirectory)
{
    if (!Log::IsEnabled(LogLevel::Info))
    {
        return;
    }

    std::string commandLine = File::ToUtf8(executable);
    for (const auto& argument : arguments)
    {
        commandLine += ' ' + argument;
    }

    Log::Info(LogCategory::Transcode, "starting {}{}", commandLine,
        workingDirectory ? std::format(" (in {})", workingDirectory->string()) : std::string());
}

}  // namespace

std::unique_ptr<FFmpegProcess> FFmpegProcess::Start(const FFmpegInstall& install,
    const std::vector<std::string>& arguments,
    std::function<void(bool)> onExit, std::optional<std::filesystem::path> workingDirectory)
{
    if (arguments.empty())
    {
        return nullptr;
    }

    ChildProcess::RedirectOptions options;
    if (workingDirectory)
    {
        options.stderrPath = *workingDirectory / kLogFileName;
    }
    options.workingDirectory = workingDirectory;

    LogStart(install.ffmpeg, arguments, workingDirectory);

    auto process = ChildProcess::Start(install.ffmpeg, arguments, options,
        [onExit = std::move(onExit)](bool completedSuccessfully)
        {
            Log::Format(completedSuccessfully ? LogLevel::Info : LogLevel::Warning, LogCategory::Transcode,
                "ffmpeg exited {}", completedSuccessfully ? "successfully" : "with an error");

            if (onExit)
            {
                onExit(completedSuccessfully);
            }
        });

    if (!process)
    {
        Log::Error(LogCategory::Transcode, "failed to start ffmpeg");
        return nullptr;
    }

    return std::unique_ptr<FFmpegProcess>(new FFmpegProcess(std::move(process)));
}

FFmpegProcess::FFmpegProcess(std::unique_ptr<ChildProcess> process) : m_process(std::move(process)) {}

FFmpegProcess::~FFmpegProcess() = default;

void FFmpegProcess::Stop() { m_process->Stop(); }

void FFmpegProcess::Wait() { m_process->Wait(); }

bool FFmpegProcess::IsRunning() const { return m_process->IsRunning(); }
