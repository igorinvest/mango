#include "PlaybackEndDetector.hpp"

#include <algorithm>
#include <format>

#include "../../log/Log.hpp"
#include "../library/Library.hpp"
#include "PlaybackHistory.hpp"

namespace
{

PlayerEndReason FromIdleReason(const std::string& idleReason)
{
    if (idleReason == "FINISHED")
    {
        return PlayerEndReason::Ended;
    }
    if (idleReason == "CANCELLED")
    {
        return PlayerEndReason::Cancelled;
    }
    if (idleReason == "INTERRUPTED")
    {
        return PlayerEndReason::Interrupted;
    }
    if (idleReason == "ERROR")
    {
        return PlayerEndReason::Error;
    }

    // IDLE without a reason: the session is still there but says nothing —
    // most often it's about to go away.
    return PlayerEndReason::SessionGone;
}

// The furthest of what the player says now and what was last reported: a
// receiver that went IDLE often reports 0, or nothing at all.
std::optional<double> FurthestPosition(std::optional<double> a, std::optional<double> b)
{
    if (a && b)
    {
        return std::max(*a, *b);
    }
    return a ? a : b;
}

PlaybackEndKind DecideReachedEnd(std::optional<double> position, double durationSeconds)
{
    if (durationSeconds <= 0)
    {
        return PlaybackEndKind::Finished;
    }

    // Short of the end: most likely a conversion that died halfway, played
    // to the end of what exists.
    return position && PlaybackEndDetector::IsInEndWindow(*position, durationSeconds)
        ? PlaybackEndKind::Finished
        : PlaybackEndKind::Stopped;
}

std::string ToString(PlayerEndReason reason)
{
    switch (reason)
    {
        case PlayerEndReason::StillPlaying: return "StillPlaying";
        case PlayerEndReason::Ended: return "Ended";
        case PlayerEndReason::Cancelled: return "Cancelled";
        case PlayerEndReason::Interrupted: return "Interrupted";
        case PlayerEndReason::Error: return "Error";
        case PlayerEndReason::SessionGone: return "SessionGone";
        case PlayerEndReason::OtherContent: return "OtherContent";
        case PlayerEndReason::Unknown: break;
    }
    return "Unknown";
}

std::string Seconds(std::optional<double> seconds) { return seconds ? std::format("{:.1f}", *seconds) : "-"; }

}  // namespace

PlaybackEndKind PlaybackEndDetector::Classify(const std::string& fastHash, const PlayerEnd& end) const
{
    const auto file = Library::GetInstance().GetCachedFile(fastHash);
    const double durationSeconds = file ? static_cast<double>(file->mediaInfo.durationSec) : 0.0;

    std::optional<double> lastReported;
    if (const auto record = PlaybackHistory::GetInstance().Get(fastHash))
    {
        lastReported = record->positionSeconds;
    }

    const bool stoppedByUser = IsStoppedByUser(fastHash);
    const auto kind = Decide(end, durationSeconds, lastReported, stoppedByUser);
    LogVerdict(fastHash, end, kind,
        std::format("pos={} last={} duration={:.0f} window={:.0f} stoppedByUser={}", Seconds(end.positionSeconds),
            Seconds(lastReported), durationSeconds,
            std::max(endWindowMinimumSeconds, durationSeconds * endWindowFraction), stoppedByUser ? "yes" : "no"));
    return kind;
}

void PlaybackEndDetector::LogVerdict(
    const std::string& fastHash, const PlayerEnd& end, PlaybackEndKind kind, const std::string& inputs) const
{
    const auto line = std::format("{} {} reason={} {}", ToString(kind), fastHash, ::ToString(end.reason), inputs);
    Log::Debug(LogCategory::PlaybackEnd, "{}", line);

    // The same status keeps giving the same verdict poll after poll: only
    // the first non-Playing one (per file, kind and reason) is worth Info.
    const auto key = std::format("{}|{}|{}", fastHash, ToString(kind), ::ToString(end.reason));
    std::lock_guard lock(m_mutex);
    if (kind == PlaybackEndKind::Playing)
    {
        m_lastLoggedVerdict.reset();
        return;
    }
    if (m_lastLoggedVerdict == key)
    {
        return;
    }

    m_lastLoggedVerdict = key;
    Log::Info(LogCategory::PlaybackEnd, "{}", line);
}

PlaybackEndKind PlaybackEndDetector::Decide(const PlayerEnd& end, double durationSeconds,
    std::optional<double> lastReportedSeconds, bool stoppedByUser)
{
    switch (end.reason)
    {
        case PlayerEndReason::StillPlaying: return PlaybackEndKind::Playing;
        case PlayerEndReason::Unknown: return PlaybackEndKind::Unknown;
        default: break;
    }

    if (stoppedByUser)
    {
        return PlaybackEndKind::Stopped;
    }

    switch (end.reason)
    {
        case PlayerEndReason::Ended:
            return DecideReachedEnd(FurthestPosition(end.positionSeconds, lastReportedSeconds), durationSeconds);

        // The receiver dropped the session (a missed poll can skip its
        // short IDLE/FINISHED) — only an end if it was already there.
        case PlayerEndReason::SessionGone:
            return durationSeconds > 0 && lastReportedSeconds &&
                    IsInEndWindow(*lastReportedSeconds, durationSeconds)
                ? PlaybackEndKind::Finished
                : PlaybackEndKind::Stopped;

        default: return PlaybackEndKind::Stopped;
    }
}

bool PlaybackEndDetector::IsInEndWindow(double positionSeconds, double durationSeconds)
{
    const double window = std::max(endWindowMinimumSeconds, durationSeconds * endWindowFraction);
    return positionSeconds >= durationSeconds - window;
}

PlayerEnd PlaybackEndDetector::FromCastStatus(const std::string& state, const std::string& idleReason,
    std::optional<bool> isFromThisServer, double currentTimeSeconds)
{
    if (state == "Playing" || state == "Paused" || state == "Buffering")
    {
        // A MEDIA_STATUS without its media object (sent around a seek)
        // can't tell whose content this is.
        if (!isFromThisServer)
        {
            return {PlayerEndReason::Unknown, std::nullopt};
        }
        return {*isFromThisServer ? PlayerEndReason::StillPlaying : PlayerEndReason::OtherContent,
            currentTimeSeconds};
    }

    // An IDLE media session reports idleReason; with no media session at
    // all (the app is still up — "Active" — or gone — "Idle") it's empty.
    if (state == "Idle" || state == "Active")
    {
        if (idleReason.empty())
        {
            return {PlayerEndReason::SessionGone, std::nullopt};
        }
        return {FromIdleReason(idleReason), currentTimeSeconds > 0 ? std::optional(currentTimeSeconds) : std::nullopt};
    }

    return {PlayerEndReason::Unknown, std::nullopt};
}

PlayerEnd PlaybackEndDetector::FromLocalEnd(const std::string& endReason, double positionSeconds)
{
    if (endReason == "Ended")
    {
        return {PlayerEndReason::Ended, positionSeconds};
    }
    if (endReason == "Error")
    {
        return {PlayerEndReason::Error, positionSeconds};
    }
    if (endReason == "Stopped")
    {
        return {PlayerEndReason::Cancelled, positionSeconds};
    }
    return {PlayerEndReason::Unknown, std::nullopt};
}

void PlaybackEndDetector::NotifyStoppedByUser(const std::string& fastHash)
{
    std::lock_guard lock(m_mutex);
    m_stoppedByUserFastHash = fastHash;
}

void PlaybackEndDetector::BeginSession(const std::string& fastHash)
{
    std::lock_guard lock(m_mutex);
    if (m_stoppedByUserFastHash == fastHash)
    {
        m_stoppedByUserFastHash.reset();
    }
}

bool PlaybackEndDetector::IsStoppedByUser(const std::string& fastHash) const
{
    std::lock_guard lock(m_mutex);
    return m_stoppedByUserFastHash == fastHash;
}

std::string PlaybackEndDetector::ToString(PlaybackEndKind kind)
{
    switch (kind)
    {
        case PlaybackEndKind::Playing: return "Playing";
        case PlaybackEndKind::Finished: return "Finished";
        case PlaybackEndKind::Stopped: return "Stopped";
        case PlaybackEndKind::Unknown: break;
    }
    return "Unknown";
}
