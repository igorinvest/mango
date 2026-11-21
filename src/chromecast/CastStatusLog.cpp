#include "CastStatusLog.hpp"

#include <cmath>
#include <format>

#include "../log/Log.hpp"

namespace mango
{

namespace
{

bool IsPlayingOrPaused(const CastPlaybackStatus& status)
{
    return status.state == "Playing" || status.state == "Paused";
}

// What the receiver's own duration says about the stream: live (-1), no
// media (0) or a known length. Its exact value isn't compared — a growing
// one would log every poll.
enum class TvDurationKind
{
    Live,
    None,
    Known,
};

TvDurationKind KindOf(double tvDurationSeconds)
{
    if (tvDurationSeconds < 0)
    {
        return TvDurationKind::Live;
    }
    return tvDurationSeconds > 0 ? TvDurationKind::Known : TvDurationKind::None;
}

}  // namespace

CastStatusLog& CastStatusLog::GetInstance()
{
    static CastStatusLog instance;
    return instance;
}

void CastStatusLog::OnCommand(const std::string& device, const std::string& command, const std::string& result)
{
    Log::Info(LogCategory::Cast, "{} sent to {}: {}", command, device, result);

    std::lock_guard lock(m_mutex);
    m_devices[device].lastCommandAt = Clock::now();
}

void CastStatusLog::OnStatus(const std::string& device, const CastPlaybackStatus& status)
{
    Log::Debug(LogCategory::Cast, "poll {}: {}", device, Describe(status));

    const auto now = Clock::now();
    std::lock_guard lock(m_mutex);
    auto& state = m_devices[device];

    if (!state.lastStatus || IsChange(*state.lastStatus, status))
    {
        Log::Info(LogCategory::Cast, "status {}: {}", device, Describe(status));
    }

    if (state.lastStatus)
    {
        LogRemoteActions(device, state, status, now);
    }

    state.lastStatus = status;
    state.lastStatusAt = now;
}

bool CastStatusLog::IsChange(const CastPlaybackStatus& previous, const CastPlaybackStatus& current)
{
    return previous.state != current.state || previous.idleReason != current.idleReason ||
           previous.isFromThisServer != current.isFromThisServer || previous.fastHash != current.fastHash ||
           KindOf(previous.tvDurationSeconds) != KindOf(current.tvDurationSeconds);
}

std::string CastStatusLog::Describe(const CastPlaybackStatus& status)
{
    return std::format("{}{} pos={:.1f} duration={:.1f} tv={:.1f} fromThisServer={} fastHash={} title=\"{}\"",
        status.state, status.idleReason.empty() ? "" : "/" + status.idleReason, status.currentTimeSeconds,
        status.durationSeconds, status.tvDurationSeconds, status.isFromThisServer ? "yes" : "no",
        status.fastHash.empty() ? "-" : status.fastHash, status.mediaTitle);
}

void CastStatusLog::LogRemoteActions(const std::string& device, const DeviceState& state,
    const CastPlaybackStatus& current, Clock::time_point now) const
{
    // Anything right after the app's own command is that command's effect.
    if (state.lastCommandAt && now - *state.lastCommandAt < commandEffectWindow)
    {
        return;
    }

    const auto& previous = *state.lastStatus;
    LogRemoteStateChange(device, previous, current);

    const double elapsedSeconds = std::chrono::duration<double>(now - state.lastStatusAt).count();
    LogRemoteSeek(device, previous, current, elapsedSeconds);
}

void CastStatusLog::LogRemoteStateChange(
    const std::string& device, const CastPlaybackStatus& previous, const CastPlaybackStatus& current)
{
    if (previous.isFromThisServer && !current.isFromThisServer && IsPlayingOrPaused(current))
    {
        Log::Info(LogCategory::Cast, "TV {}: switched to other content \"{}\" (not from the app)", device,
            current.mediaTitle);
        return;
    }

    if (!current.isFromThisServer && !previous.isFromThisServer)
    {
        return;
    }

    if (previous.state == "Playing" && current.state == "Paused")
    {
        Log::Info(LogCategory::Cast, "TV {}: paused at {:.1f} (not from the app)", device, current.currentTimeSeconds);
    }
    else if (previous.state == "Paused" && current.state == "Playing")
    {
        Log::Info(LogCategory::Cast, "TV {}: resumed at {:.1f} (not from the app)", device, current.currentTimeSeconds);
    }
    else if (current.state == "Idle" && current.idleReason == "CANCELLED" && previous.idleReason != "CANCELLED")
    {
        Log::Info(LogCategory::Cast, "TV {}: stopped (not from the app)", device);
    }
}

void CastStatusLog::LogRemoteSeek(const std::string& device, const CastPlaybackStatus& previous,
    const CastPlaybackStatus& current, double elapsedSeconds)
{
    if (!IsPlayingOrPaused(previous) || !IsPlayingOrPaused(current) || previous.fastHash != current.fastHash ||
        !current.isFromThisServer)
    {
        return;
    }

    // While playing the position moves by up to the elapsed time; while
    // paused it stays put.
    const double expected =
        previous.currentTimeSeconds + (previous.state == "Playing" ? elapsedSeconds : 0.0);
    if (std::abs(current.currentTimeSeconds - expected) <= seekMarginSeconds)
    {
        return;
    }

    Log::Info(LogCategory::Cast, "TV {}: seek {:.1f} -> {:.1f} (not from the app)", device,
        previous.currentTimeSeconds, current.currentTimeSeconds);
}

}  // namespace mango
