#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <mutex>
#include <thread>

#include "ShutdownCoordinator.hpp"
#include "log/Log.hpp"

// A signal handler may only do async-signal-safe work, and StopActiveStream()
// needs mutexes, files and threads. So the handler just writes a byte into a
// pipe (the self-pipe trick), and a thread blocked on the other end does the
// real work. Unlike sigwait(), this works whichever thread the kernel picks
// for the signal, so threads started before the handler need no signal mask.
// Handles SIGTERM (systemctl stop, kill) and SIGINT (Ctrl+C in the console).
namespace
{

int g_wakeWriteEnd = -1;

extern "C" void OnTerminationSignal(int signalNumber)
{
    // A second Ctrl+C while the stream shuts down ends the process at once.
    signal(signalNumber, SIG_DFL);

    const int savedErrno = errno;
    const char byte = 1;
    [[maybe_unused]] const auto written = write(g_wakeWriteEnd, &byte, 1);
    errno = savedErrno;
}

bool HandleSignal(int signalNumber)
{
    struct sigaction action{};
    action.sa_handler = OnTerminationSignal;
    sigemptyset(&action.sa_mask);
    // getline() on stdin and other blocking calls continue after the signal.
    action.sa_flags = SA_RESTART;
    return sigaction(signalNumber, &action, nullptr) == 0;
}

[[noreturn]] void WaitForSignal(int readEnd)
{
    char byte = 0;
    while (read(readEnd, &byte, 1) < 0 && errno == EINTR)
    {
    }

    ShutdownCoordinator::StopActiveStream();
    std::exit(0);
}

void Install()
{
    // O_CLOEXEC: ffmpeg and the other child processes don't inherit it.
    int ends[2] = {-1, -1};
    if (pipe2(ends, O_CLOEXEC) != 0)
    {
        Log::Error(LogCategory::Engine, "no signal handler: pipe2 failed ({})", errno);
        return;
    }
    g_wakeWriteEnd = ends[1];

    std::thread(WaitForSignal, ends[0]).detach();

    if (!HandleSignal(SIGTERM) || !HandleSignal(SIGINT))
    {
        Log::Error(LogCategory::Engine, "no signal handler: sigaction failed ({})", errno);
    }
}

}  // namespace

void ShutdownCoordinator::InstallSignalHandler()
{
    static std::once_flag installed;
    std::call_once(installed, Install);
}
