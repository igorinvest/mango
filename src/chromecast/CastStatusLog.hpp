#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "Engine.hpp"

namespace mango
{

// Logs what the TVs report, without a line per poll: a status *change*
// (state, idleReason, isFromThisServer, fastHash) at Info, every poll at
// Debug. Also infers what someone did on the TV's own remote — a change
// the app didn't send — and logs it as "TV: <what> (not from the app)".
class CastStatusLog
{
   public:
    static CastStatusLog& GetInstance();

    CastStatusLog(const CastStatusLog&) = delete;
    CastStatusLog& operator=(const CastStatusLog&) = delete;

    // Logs a command and its result. Its effect on the next polls is then
    // expected, not reported as the remote's.
    void OnCommand(const std::string& device, const std::string& command, const std::string& result);

    void OnStatus(const std::string& device, const CastPlaybackStatus& status);

    // How long after a command a change is still taken as its effect.
    static constexpr auto commandEffectWindow = std::chrono::seconds(10);

    // How far a position may drift from what the elapsed time predicts
    // before it counts as a seek.
    static constexpr double seekMarginSeconds = 10.0;

   private:
    using Clock = std::chrono::steady_clock;

    struct DeviceState
    {
        std::optional<CastPlaybackStatus> lastStatus;
        Clock::time_point lastStatusAt;
        std::optional<Clock::time_point> lastCommandAt;
    };

    CastStatusLog() = default;

    static bool IsChange(const CastPlaybackStatus& previous, const CastPlaybackStatus& current);
    static std::string Describe(const CastPlaybackStatus& status);

    void LogRemoteActions(const std::string& device, const DeviceState& state, const CastPlaybackStatus& current,
        Clock::time_point now) const;
    static void LogRemoteStateChange(
        const std::string& device, const CastPlaybackStatus& previous, const CastPlaybackStatus& current);
    static void LogRemoteSeek(const std::string& device, const CastPlaybackStatus& previous,
        const CastPlaybackStatus& current, double elapsedSeconds);

    std::mutex m_mutex;
    std::unordered_map<std::string, DeviceState> m_devices;
};

}  // namespace mango
