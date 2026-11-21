#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Cross-platform child process: spawn, gracefully stop (escalating to a
// forced kill if it doesn't exit in time), wait, and poll. One implementation
// lives in ChildProcess_Posix.cpp, the other in ChildProcess_Windows.cpp;
// CMake picks the matching file for the target platform.
class ChildProcess
{
   public:
    struct RedirectOptions
    {
        // When set, the child's stdout is redirected to this file instead of
        // inheriting the parent's.
        std::optional<std::filesystem::path> stdoutPath;

        // When set, the child's stderr is redirected to this file instead of
        // inheriting the parent's. ffmpeg/ffprobe write their progress and
        // error diagnostics to stderr, not stdout, so this is what callers
        // need to capture in order to see why a run failed.
        std::optional<std::filesystem::path> stderrPath;

        // When set, the child starts with this as its current directory
        // instead of inheriting the parent's. Needed for tools (e.g. ffmpeg's
        // dash muxer) that resolve relative output filenames against their
        // own cwd rather than against an absolute path given elsewhere on
        // the command line.
        std::optional<std::filesystem::path> workingDirectory;
    };

    // onExit, when provided, fires exactly once from an internal monitor
    // thread the moment the child exits (naturally or via Stop()) — the
    // argument is true only if it exited on its own with a zero status.
    // Leave it null for a process you'll observe synchronously instead, via
    // Wait()/IsRunning().
    static std::unique_ptr<ChildProcess> Start(const std::filesystem::path& executablePath,
        const std::vector<std::string>& arguments, RedirectOptions redirect = {},
        std::function<void(bool completedSuccessfully)> onExit = nullptr);

    ~ChildProcess();

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    // The monitor thread (when onExit is used) captures `this`, so instances
    // must stay pinned to one address.
    ChildProcess(ChildProcess&&) = delete;
    ChildProcess& operator=(ChildProcess&&) = delete;

    // Politely asks the process to exit, escalating to a forced kill if it
    // hasn't within a few seconds. Blocks until the process is gone.
    void Stop();

    // Blocks until the process exits on its own, without asking it to stop.
    // Returns true if it exited normally with a zero exit code.
    bool Wait();

    bool IsRunning() const;

   private:
    struct Impl;
    explicit ChildProcess(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};
