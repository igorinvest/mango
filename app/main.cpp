#if defined(__linux__)

#include "LinuxMain.hpp"

#else

#include <print>

#include "console/Output.hpp"
#include "EngineOwner.hpp"

namespace
{

// macOS and Windows: the console runs the engine itself, in this process.
int RunInProcess()
{
    mango::cli::StdoutOutput terminal;
    mango::cli::EngineOwner owner(terminal);
    owner.Start();
    owner.GetConsole().Run(terminal);

    // stdin ended without `quit` (a piped script): keep serving until the
    // process is killed, as before the console existed.
    if (!owner.QuitRequested())
    {
        std::println("\nInput closed. Serving until stopped (Ctrl+C).");
        mango::cli::WaitUntilStopped();
    }

    owner.Stop();
    return 0;
}

}  // namespace

#endif

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
#if defined(__linux__)
    return mango::cli::RunLinux(argc, argv);
#else
    return RunInProcess();
#endif
}
