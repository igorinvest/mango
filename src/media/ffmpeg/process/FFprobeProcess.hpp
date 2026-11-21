#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "../install/FFmpegInstall.hpp"

class ChildProcess;

class FFprobeProcess
{
   public:
    // Runs install's ffprobe with stdout written to outputPath. nullptr when
    // it can't be started.
    static std::unique_ptr<FFprobeProcess> Start(const FFmpegInstall& install,
        const std::vector<std::string>& arguments, const std::filesystem::path& outputPath);

    ~FFprobeProcess();

    FFprobeProcess(const FFprobeProcess&) = delete;
    FFprobeProcess& operator=(const FFprobeProcess&) = delete;

    FFprobeProcess(FFprobeProcess&& other) noexcept;
    FFprobeProcess& operator=(FFprobeProcess&& other) noexcept;

    void Stop();

    // Returns true if ffprobe exited normally with a zero exit code.
    bool Wait();

    bool IsRunning() const;

   private:
    explicit FFprobeProcess(std::unique_ptr<ChildProcess> process);

    std::unique_ptr<ChildProcess> m_process;
};
