#pragma once

#include <chrono>
#include <optional>

// When a streaming session's listener should close (see StreamingSession).
// Pure: every input carries its own time, so it can be tested without
// waiting.
//
// An end signal (finished, stopped, the TV switched away) doesn't close at
// once. A status poll can report the old media's end (IDLE/INTERRUPTED)
// right as a new LOAD of this server's stream starts, and closing then would
// break the new one. So an end closes only once nothing has been fetched for
// EndQuietPeriod after it, and a new stream or a fetch after it cancels it.
class SessionPolicy
{
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    // Quiet time after an end signal before the listener closes.
    static constexpr auto EndQuietPeriod = std::chrono::seconds(30);

    // Fetches this soon after an end signal are the ended playback's
    // stragglers, not a new one.
    static constexpr auto EndStragglerWindow = std::chrono::seconds(5);

    // No fetch and no "still playing" status for this long closes the
    // session whatever else was said. Long, because a paused TV fetches
    // nothing; it applies when no app polls the TV (CLI, VLC).
    static constexpr auto IdleBackstop = std::chrono::hours(2);

    // A stream was initiated: a new session, or a new stream in it.
    void OnStreamStarted(TimePoint now);

    // A player said this server's playback ended.
    void OnPlaybackEnded(TimePoint now);

    // A TV status poll shows this server's current stream playing, paused
    // or buffering.
    void OnStillPlaying(TimePoint now);

    // lastRequest: when the server last served a stream file, if ever.
    [[nodiscard]] bool ShouldClose(TimePoint now, std::optional<TimePoint> lastRequest);

private:
    void NoteAlive(TimePoint time);
    void ForgetEndIfFetchedSince(std::optional<TimePoint> lastRequest);

    TimePoint m_lastAlive{};
    std::optional<TimePoint> m_endedAt;
};
