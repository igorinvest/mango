#pragma once

#include <thread>

#include "commands/CommandContext.hpp"
#include "console/Console.hpp"
#include "console/Output.hpp"
#include "console/RemoteWatcher.hpp"
#include "console/SessionWatcher.hpp"
#include "Engine.hpp"

namespace mango::cli
{

// The process that runs the engine: the engine, the watchers the commands
// use, and the console that runs them. On Linux only one per user runs
// (see InstanceLock.hpp); later terminals attach to it.
class EngineOwner
{
   public:
    // events: where notices go on their own (the terminal, and on Linux
    // every attached client too).
    explicit EngineOwner(Output& events);
    ~EngineOwner();

    EngineOwner(const EngineOwner&) = delete;
    EngineOwner& operator=(const EngineOwner&) = delete;

    // Starts the engine and prints the banner; ffmpeg's lines follow when
    // it's found.
    void Start();
    void Stop();

    [[nodiscard]] Console& GetConsole() { return m_console; }
    [[nodiscard]] bool QuitRequested() const { return m_context.quitRequested; }

   private:
    mango::Engine m_engine;
    SessionWatcher m_watcher;
    RemoteWatcher m_remote;
    CommandContext m_context;
    Console m_console;
    std::thread m_toolsReport;
};

// Blocks until the process is stopped (SIGTERM, or Ctrl+C), which ends it
// from the engine's signal handler (see ShutdownCoordinator.hpp).
void WaitUntilStopped();

}  // namespace mango::cli
