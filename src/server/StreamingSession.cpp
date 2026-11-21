#include "StreamingSession.hpp"

#include "../log/Log.hpp"
#include "NetworkUtils.hpp"
#include "StreamActivity.hpp"

namespace
{

// StreamActivity's "how long ago" as a point in time.
std::optional<SessionPolicy::TimePoint> LastRequest(SessionPolicy::TimePoint now)
{
    const auto sinceLast = StreamActivity::TimeSinceLastRequest();
    return sinceLast ? std::optional(now - *sinceLast) : std::nullopt;
}

}  // namespace

StreamingSession::~StreamingSession() { Disable(); }

void StreamingSession::Enable(bool listenOnlyWhileStreaming)
{
    std::lock_guard lock(m_mutex);
    if (m_enabled)
    {
        return;
    }

    m_enabled = true;
    m_listenOnlyWhileStreaming = listenOnlyWhileStreaming;
    m_watchdog = std::thread([this] { WatchUntilDisabled(); });

    if (listenOnlyWhileStreaming)
    {
        Log::Info(LogCategory::Server, "ready; listening starts with the first stream");
        return;
    }
    ListenLocked();
}

void StreamingSession::SetListenOnlyWhileStreaming(bool enabled)
{
    std::lock_guard lock(m_mutex);
    m_listenOnlyWhileStreaming = enabled;
    if (!m_enabled)
    {
        return;
    }

    if (!enabled)
    {
        ListenLocked();
        return;
    }
    if (m_open)
    {
        m_policy.OnPlaybackEnded(SessionPolicy::Clock::now());
    }
}

std::optional<std::string> StreamingSession::ListenLocked()
{
    // Read every time: the machine may be on another network than at the
    // last listen.
    auto baseUrl = m_server.Listen(NetworkUtils::GetLanIPAddress());
    m_open = baseUrl.has_value();
    return baseUrl;
}

void StreamingSession::Disable()
{
    {
        std::lock_guard lock(m_mutex);
        if (!m_enabled)
        {
            return;
        }
        m_enabled = false;
        m_open = false;
    }

    m_wake.notify_all();
    m_watchdog.join();
    m_server.Close();
}

std::optional<std::string> StreamingSession::Open()
{
    std::lock_guard lock(m_mutex);
    if (!m_enabled)
    {
        return std::nullopt;
    }

    auto baseUrl = ListenLocked();
    m_policy.OnStreamStarted(SessionPolicy::Clock::now());
    return baseUrl;
}

void StreamingSession::NotePlaybackEnded()
{
    std::lock_guard lock(m_mutex);
    if (m_open)
    {
        m_policy.OnPlaybackEnded(SessionPolicy::Clock::now());
    }
}

void StreamingSession::NoteStillPlaying()
{
    std::lock_guard lock(m_mutex);
    m_policy.OnStillPlaying(SessionPolicy::Clock::now());
}

mango::ServerStatus StreamingSession::GetStatus() const
{
    auto status = m_server.GetStatus();

    std::lock_guard lock(m_mutex);
    status.isRunning = m_enabled;
    return status;
}

void StreamingSession::WatchUntilDisabled()
{
    std::unique_lock lock(m_mutex);
    while (m_enabled)
    {
        m_wake.wait_for(lock, CheckInterval, [this] { return !m_enabled; });
        if (m_enabled)
        {
            CloseIfDue();
        }
    }
}

// Called with m_mutex held. Open() holds it too while it listens, so a
// close can't land between a stream start and its URLs being handed out.
void StreamingSession::CloseIfDue()
{
    // Always listening: only follow the LAN address (or retry a failed
    // bind) — Listen is a no-op while the address is unchanged.
    if (!m_listenOnlyWhileStreaming)
    {
        ListenLocked();
        return;
    }

    const auto now = SessionPolicy::Clock::now();
    if (!m_open || !m_policy.ShouldClose(now, LastRequest(now)))
    {
        return;
    }

    Log::Info(LogCategory::Server, "streaming session over, closing the port");
    m_server.Close();
    m_open = false;
}

void StreamingSession::SetRemoteHandler(ServerWrapper::RemoteHandler handler)
{
    m_server.SetRemoteHandler(std::move(handler));
}
