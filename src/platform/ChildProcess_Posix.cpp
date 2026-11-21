#include "ChildProcess.hpp"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

namespace
{

constexpr auto kGracefulTimeout = std::chrono::seconds(3);
constexpr auto kPollInterval = std::chrono::milliseconds(100);

std::vector<char*> BuildArgv(
    const std::filesystem::path& executablePath, const std::vector<std::string>& arguments)
{
    std::vector<char*> argv;

    argv.reserve(arguments.size() + 2);

    argv.push_back(const_cast<char*>(executablePath.c_str()));

    for (const auto& argument : arguments)
    {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }

    argv.push_back(nullptr);

    return argv;
}

void RedirectOutputOrExit(const std::filesystem::path& path, int targetFd)
{
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd < 0)
    {
        _exit(EXIT_FAILURE);
    }

    dup2(fd, targetFd);
    close(fd);
}

[[noreturn]] void RunChildOrExit(const std::filesystem::path& executablePath, std::vector<char*>& argv,
    const ChildProcess::RedirectOptions& redirect)
{
    if (redirect.stdoutPath)
    {
        RedirectOutputOrExit(*redirect.stdoutPath, STDOUT_FILENO);
    }

    if (redirect.stderrPath)
    {
        RedirectOutputOrExit(*redirect.stderrPath, STDERR_FILENO);
    }

    if (redirect.workingDirectory && chdir(redirect.workingDirectory->c_str()) != 0)
    {
        _exit(EXIT_FAILURE);
    }

    execv(executablePath.c_str(), argv.data());

    _exit(EXIT_FAILURE);
}

}  // namespace

// The real implementation: fork/exec plus the graceful-stop/reap machinery
// shared by every child process this engine spawns. ChildProcess itself just
// forwards to this.
struct ChildProcess::Impl
{
    pid_t pid = -1;
    std::atomic<bool> exited{false};
    std::atomic<bool> exitedSuccessfully{false};
    std::thread monitorThread;

    bool IsRunning() const
    {
        if (pid <= 0)
        {
            return false;
        }

        // With a monitor thread, its blocking waitpid() is the only reap —
        // polling here again would race it. Without one, this WNOHANG poll
        // both checks and reaps.
        if (monitorThread.joinable())
        {
            return !exited.load(std::memory_order_acquire);
        }

        int status = 0;

        return waitpid(pid, &status, WNOHANG) == 0;
    }

    void EscalateUntilStopped()
    {
        const auto start = std::chrono::steady_clock::now();

        while (IsRunning())
        {
            if (std::chrono::steady_clock::now() - start > kGracefulTimeout)
            {
                kill(pid, SIGKILL);

                break;
            }

            std::this_thread::sleep_for(kPollInterval);
        }
    }

    void ReapOrJoin()
    {
        if (monitorThread.joinable())
        {
            monitorThread.join();

            return;
        }

        if (pid > 0)
        {
            int status = 0;

            waitpid(pid, &status, 0);

            exitedSuccessfully.store(
                WIFEXITED(status) && WEXITSTATUS(status) == 0, std::memory_order_relaxed);
        }
    }

    void Stop()
    {
        if (pid > 0 && IsRunning())
        {
            kill(pid, SIGTERM);

            EscalateUntilStopped();
        }

        ReapOrJoin();

        pid = -1;
    }

    bool Wait()
    {
        ReapOrJoin();

        pid = -1;

        return exitedSuccessfully.load(std::memory_order_relaxed);
    }

    void StartMonitorThread(std::function<void(bool)> onExit)
    {
        monitorThread = std::thread(
            [this, onExit = std::move(onExit)]
            {
                int status = 0;
                waitpid(pid, &status, 0);

                const bool success = WIFEXITED(status) && WEXITSTATUS(status) == 0;

                exitedSuccessfully.store(success, std::memory_order_relaxed);
                exited.store(true, std::memory_order_release);

                if (onExit)
                {
                    onExit(success);
                }
            });
    }
};

std::unique_ptr<ChildProcess> ChildProcess::Start(const std::filesystem::path& executablePath,
    const std::vector<std::string>& arguments, RedirectOptions redirect,
    std::function<void(bool)> onExit)
{
    if (!std::filesystem::exists(executablePath))
    {
        return nullptr;
    }

    auto argv = BuildArgv(executablePath, arguments);

    const pid_t pid = fork();

    if (pid < 0)
    {
        return nullptr;
    }

    if (pid == 0)
    {
        RunChildOrExit(executablePath, argv, redirect);
    }

    auto impl = std::make_unique<Impl>();
    impl->pid = pid;

    auto process = std::unique_ptr<ChildProcess>(new ChildProcess(std::move(impl)));

    if (onExit)
    {
        process->m_impl->StartMonitorThread(std::move(onExit));
    }

    return process;
}

ChildProcess::ChildProcess(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

ChildProcess::~ChildProcess() { Stop(); }

void ChildProcess::Stop() { m_impl->Stop(); }

bool ChildProcess::Wait() { return m_impl->Wait(); }

bool ChildProcess::IsRunning() const { return m_impl->IsRunning(); }
