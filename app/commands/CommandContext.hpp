#pragma once

#include <atomic>

#include "console/Output.hpp"
#include "console/RemoteWatcher.hpp"
#include "console/SessionWatcher.hpp"
#include "Engine.hpp"

namespace mango::cli
{

// State the console commands share. The TV, what plays on it and the
// devices found live in the session (see SessionWatcher).
struct CommandContext
{
    CommandContext(mango::Engine& engine, SessionWatcher& watcher, RemoteWatcher& remote, Output& out)
        : engine(engine), watcher(watcher), remote(remote), output(&out)
    {
    }

    mango::Engine& engine;
    SessionWatcher& watcher;
    RemoteWatcher& remote;

    [[nodiscard]] mango::Session& session() { return watcher.Session(); }

    // Where the running command prints: the terminal, or the client that
    // sent it. Console::Execute sets it for each command.
    [[nodiscard]] Output& out() const { return *output; }
    Output* output;

    // Set by `quit` (and `shutdown` on Linux), from whichever thread ran it.
    std::atomic<bool> quitRequested = false;
};

}  // namespace mango::cli
