#include "SessionPolicy.hpp"

#include <algorithm>

void SessionPolicy::OnStreamStarted(TimePoint now)
{
    m_endedAt.reset();
    NoteAlive(now);
}

void SessionPolicy::OnPlaybackEnded(TimePoint now)
{
    // The first end counts: repeated polls of an idle TV say it again and
    // again, and must not keep pushing the close back.
    if (!m_endedAt)
    {
        m_endedAt = now;
    }
}

void SessionPolicy::OnStillPlaying(TimePoint now)
{
    m_endedAt.reset();
    NoteAlive(now);
}

bool SessionPolicy::ShouldClose(TimePoint now, std::optional<TimePoint> lastRequest)
{
    ForgetEndIfFetchedSince(lastRequest);
    if (lastRequest)
    {
        NoteAlive(*lastRequest);
    }

    if (m_endedAt && now - *m_endedAt >= EndQuietPeriod)
    {
        return true;
    }
    return now - m_lastAlive >= IdleBackstop;
}

void SessionPolicy::NoteAlive(TimePoint time) { m_lastAlive = std::max(m_lastAlive, time); }

void SessionPolicy::ForgetEndIfFetchedSince(std::optional<TimePoint> lastRequest)
{
    if (m_endedAt && lastRequest && *lastRequest > *m_endedAt + EndStragglerWindow)
    {
        m_endedAt.reset();
    }
}
