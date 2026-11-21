#pragma once

#include <atomic>
#include <chrono>
#include <optional>

// When the HTTP server last served a stream file (/stream/...) to any client.
// A player that's actually playing keeps fetching segments, whoever started
// it (native app, CLI, another device) — so this is the one signal that
// covers every way playback can happen. Native apps use it to keep the
// machine awake (see SleepPreventer on macOS/Windows).
class StreamActivity
{
public:
    // Called from the /stream route — any httplib worker thread.
    static void NoteRequest();

    // std::nullopt if nothing has been served since launch.
    static std::optional<std::chrono::steady_clock::duration> TimeSinceLastRequest();

private:
    static constexpr auto kNever = std::chrono::steady_clock::rep{-1};

    static std::atomic<std::chrono::steady_clock::rep> s_lastRequestTicks;
};
