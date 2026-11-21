#include "FFprobeProcess.hpp"

#include "../../../platform/ChildProcess.hpp"

std::unique_ptr<FFprobeProcess> FFprobeProcess::Start(const FFmpegInstall& install,
    const std::vector<std::string>& arguments, const std::filesystem::path& outputPath)
{
    if (arguments.empty())
    {
        return nullptr;
    }

    ChildProcess::RedirectOptions redirect;
    redirect.stdoutPath = outputPath;

    auto process = ChildProcess::Start(install.ffprobe, arguments, redirect);

    if (!process)
    {
        return nullptr;
    }

    return std::unique_ptr<FFprobeProcess>(new FFprobeProcess(std::move(process)));
}

FFprobeProcess::FFprobeProcess(std::unique_ptr<ChildProcess> process) : m_process(std::move(process)) {}

FFprobeProcess::~FFprobeProcess() = default;

FFprobeProcess::FFprobeProcess(FFprobeProcess&&) noexcept = default;

FFprobeProcess& FFprobeProcess::operator=(FFprobeProcess&&) noexcept = default;

void FFprobeProcess::Stop() { m_process->Stop(); }

bool FFprobeProcess::Wait() { return m_process->Wait(); }

bool FFprobeProcess::IsRunning() const { return m_process->IsRunning(); }
