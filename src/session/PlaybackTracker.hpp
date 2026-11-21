#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "CastWorker.hpp"
#include "SessionBackend.hpp"
#include "SessionState.hpp"
#include "SessionThread.hpp"

namespace mango
{

// What a poll should do besides reading the status, decided when it starts.
struct PollContext
{
    // The file whose end the status is judged for, "" for none (while a
    // stream is starting or loading the TV still reports the previous one).
    std::string judgedFastHash;

    // FolderAutoplay::Token() when the poll started.
    int token = 0;

    // Reported as the playback position while the TV plays this server's
    // stream.
    std::optional<TrackChoice> progress;
};

// One poll's result.
struct PolledStatus
{
    CastDevice device;
    CastPlaybackStatus status;
    PollContext context;

    // ClassifyCastEnd's answer for context.judgedFastHash, "" without one.
    std::string endKind;
};

// Cast status has no push, so the selected Chromecast is polled: every
// interval while one is selected, and straight away after a command
// (QueryNow). Nothing polls without one. A poll still running when the next
// is due is not doubled. Each poll reports the playback position and asks
// the engine whether the playback ended. Session thread only.
class PlaybackTracker
{
   public:
    using ContextProvider = std::function<PollContext()>;
    using StatusHandler = std::function<void(const PolledStatus&)>;

    PlaybackTracker(SessionBackend& backend, SessionThread& thread, CastWorker& worker,
        std::chrono::milliseconds interval, ContextProvider context, StatusHandler onStatus);

    // Forgets the last status and starts polling device (or stops).
    void SetDevice(std::optional<CastDevice> device);

    void QueryNow();

    // The selected device's last status, nullopt before the first.
    [[nodiscard]] const std::optional<CastPlaybackStatus>& Status() const { return m_status; }

    // Playing, paused or buffering: the TV has a session to control.
    [[nodiscard]] static bool IsControllable(const CastPlaybackStatus& status);

   private:
    void SchedulePoll();
    void Poll(const CastDevice& device, const PollContext& context, std::uint64_t generation);
    void OnPolled(const PolledStatus& polled, std::uint64_t generation);

    SessionBackend& m_backend;
    SessionThread& m_thread;
    CastWorker& m_worker;
    const std::chrono::milliseconds m_interval;
    ContextProvider m_context;
    StatusHandler m_onStatus;

    std::optional<CastDevice> m_device;
    std::optional<CastPlaybackStatus> m_status;
    bool m_querying = false;
    SessionThread::TimerId m_timer = 0;

    // Bumped by SetDevice, so a poll of the previous device is dropped.
    std::uint64_t m_generation = 0;
};

}  // namespace mango
