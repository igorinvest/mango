#include "ChildProcess.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <chrono>
#include <optional>
#include <string_view>
#include <thread>

// Windows has no generic "ask nicely" signal for an arbitrary process, and a
// GUI app has no console to send Ctrl+Break through without popping one open
// for the child. Instead the child's stdin is piped, and a graceful stop
// writes "q" to it — the interactive-quit key ffmpeg/ffprobe read from stdin
// regardless of whether it's a real console. Force-kill still falls back to
// TerminateProcess() if that doesn't land in time.
namespace
{

constexpr auto kGracefulTimeout = std::chrono::seconds(3);
constexpr auto kPollInterval = std::chrono::milliseconds(100);

// Arguments are UTF-8 std::string throughout the engine; this assumes that
// convention holds all the way to a caller-supplied path. It does NOT hold
// for std::filesystem::path::string() on Windows, which yields the native
// ANSI codepage, not UTF-8 — any argument built that way needs converting via
// path::wstring() upstream instead, before it reaches here as narrow text.
std::wstring Utf8ToWide(std::string_view utf8)
{
    if (utf8.empty())
    {
        return {};
    }

    const int size =
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);

    std::wstring wide(static_cast<size_t>(size), L'\0');

    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);

    return wide;
}

// The MSVC CRT's argv-quoting algorithm: only quotes when necessary, and
// escapes backslashes/quotes so the child's own argv parsing round-trips.
std::wstring QuoteArgumentForWindows(const std::wstring& argument)
{
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
    {
        return argument;
    }

    std::wstring quoted = L"\"";

    for (auto it = argument.begin();; ++it)
    {
        size_t backslashCount = 0;

        while (it != argument.end() && *it == L'\\')
        {
            ++it;
            ++backslashCount;
        }

        if (it == argument.end())
        {
            quoted.append(backslashCount * 2, L'\\');

            break;
        }

        if (*it == L'"')
        {
            quoted.append(backslashCount * 2 + 1, L'\\');
            quoted.push_back(*it);
        }
        else
        {
            quoted.append(backslashCount, L'\\');
            quoted.push_back(*it);
        }
    }

    quoted.push_back(L'"');

    return quoted;
}

std::wstring BuildCommandLine(
    const std::filesystem::path& executablePath, const std::vector<std::string>& arguments)
{
    std::wstring commandLine = QuoteArgumentForWindows(executablePath.wstring());

    for (const auto& argument : arguments)
    {
        commandLine.push_back(L' ');
        commandLine += QuoteArgumentForWindows(Utf8ToWide(argument));
    }

    return commandLine;
}

struct SpawnResult
{
    HANDLE processHandle = nullptr;
    DWORD processId = 0;
    HANDLE stdinWriteHandle = nullptr;
};

std::optional<HANDLE> OpenInheritableOutputFile(
    const std::filesystem::path& path, const SECURITY_ATTRIBUTES& inheritable)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        const_cast<SECURITY_ATTRIBUTES*>(&inheritable), CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file == INVALID_HANDLE_VALUE)
    {
        return std::nullopt;
    }

    return file;
}

std::optional<SpawnResult> SpawnProcess(const std::filesystem::path& executablePath,
    const std::vector<std::string>& arguments, const ChildProcess::RedirectOptions& redirect)
{
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};

    HANDLE stdinRead = nullptr;
    HANDLE stdinWrite = nullptr;

    if (!CreatePipe(&stdinRead, &stdinWrite, &inheritable, 0))
    {
        return std::nullopt;
    }

    SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0);

    HANDLE stdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE ownedStdoutHandle = nullptr;

    if (redirect.stdoutPath)
    {
        const auto opened = OpenInheritableOutputFile(*redirect.stdoutPath, inheritable);

        if (!opened)
        {
            CloseHandle(stdinRead);
            CloseHandle(stdinWrite);

            return std::nullopt;
        }

        ownedStdoutHandle = *opened;
        stdoutHandle = ownedStdoutHandle;
    }

    HANDLE stderrHandle = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE ownedStderrHandle = nullptr;

    if (redirect.stderrPath)
    {
        const auto opened = OpenInheritableOutputFile(*redirect.stderrPath, inheritable);

        if (!opened)
        {
            CloseHandle(stdinRead);
            CloseHandle(stdinWrite);

            if (ownedStdoutHandle)
            {
                CloseHandle(ownedStdoutHandle);
            }

            return std::nullopt;
        }

        ownedStderrHandle = *opened;
        stderrHandle = ownedStderrHandle;
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = stdinRead;
    startupInfo.hStdOutput = stdoutHandle;
    startupInfo.hStdError = stderrHandle;

    PROCESS_INFORMATION processInfo{};
    std::wstring commandLine = BuildCommandLine(executablePath, arguments);

    const std::wstring workingDirectory =
        redirect.workingDirectory ? redirect.workingDirectory->wstring() : std::wstring();

    const BOOL created = CreateProcessW(executablePath.c_str(), commandLine.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW, nullptr, workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
        &startupInfo, &processInfo);

    CloseHandle(stdinRead);

    if (ownedStdoutHandle)
    {
        CloseHandle(ownedStdoutHandle);
    }

    if (ownedStderrHandle)
    {
        CloseHandle(ownedStderrHandle);
    }

    if (!created)
    {
        CloseHandle(stdinWrite);

        return std::nullopt;
    }

    CloseHandle(processInfo.hThread);

    return SpawnResult{processInfo.hProcess, processInfo.dwProcessId, stdinWrite};
}

}  // namespace

struct ChildProcess::Impl
{
    HANDLE processHandle = nullptr;
    DWORD processId = 0;
    HANDLE stdinWriteHandle = nullptr;
    std::atomic<bool> exited{false};
    std::atomic<bool> exitedSuccessfully{false};
    std::thread monitorThread;

    ~Impl()
    {
        if (monitorThread.joinable())
        {
            monitorThread.join();
        }

        if (stdinWriteHandle)
        {
            CloseHandle(stdinWriteHandle);
        }

        if (processHandle)
        {
            CloseHandle(processHandle);
        }
    }

    bool IsRunning() const
    {
        if (!processHandle)
        {
            return false;
        }

        if (monitorThread.joinable())
        {
            return !exited.load(std::memory_order_acquire);
        }

        return WaitForSingleObject(processHandle, 0) == WAIT_TIMEOUT;
    }

    void RequestGracefulExit()
    {
        if (!stdinWriteHandle)
        {
            return;
        }

        constexpr char quitCommand[] = "q\n";
        DWORD written = 0;

        WriteFile(stdinWriteHandle, quitCommand, sizeof(quitCommand) - 1, &written, nullptr);
    }

    void EscalateUntilStopped()
    {
        const auto start = std::chrono::steady_clock::now();

        while (IsRunning())
        {
            if (std::chrono::steady_clock::now() - start > kGracefulTimeout)
            {
                TerminateProcess(processHandle, 1);

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

        if (processHandle)
        {
            WaitForSingleObject(processHandle, INFINITE);

            DWORD exitCode = 1;
            GetExitCodeProcess(processHandle, &exitCode);

            exitedSuccessfully.store(exitCode == 0, std::memory_order_relaxed);
        }
    }

    void Stop()
    {
        if (processHandle && IsRunning())
        {
            RequestGracefulExit();

            EscalateUntilStopped();
        }

        ReapOrJoin();
    }

    bool Wait()
    {
        ReapOrJoin();

        return exitedSuccessfully.load(std::memory_order_relaxed);
    }

    void StartMonitorThread(std::function<void(bool)> onExit)
    {
        monitorThread = std::thread(
            [this, onExit = std::move(onExit)]
            {
                WaitForSingleObject(processHandle, INFINITE);

                DWORD exitCode = 1;
                GetExitCodeProcess(processHandle, &exitCode);

                const bool success = exitCode == 0;

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

    auto spawnResult = SpawnProcess(executablePath, arguments, redirect);

    if (!spawnResult)
    {
        return nullptr;
    }

    auto impl = std::make_unique<Impl>();
    impl->processHandle = spawnResult->processHandle;
    impl->processId = spawnResult->processId;
    impl->stdinWriteHandle = spawnResult->stdinWriteHandle;

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
