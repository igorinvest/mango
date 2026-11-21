#include "PlaybackTracker.hpp"

namespace mango
{

PlaybackTracker::PlaybackTracker(SessionBackend& backend, SessionThread& thread, CastWorker& worker,
    std::chrono::milliseconds interval, ContextProvider context, StatusHandler onStatus)
    : m_backend(backend),
      m_thread(thread),
      m_worker(worker),
      m_interval(interval),
      m_context(std::move(context)),
      m_onStatus(std::move(onStatus))
{
}

bool PlaybackTracker::IsControllable(const CastPlaybackStatus& status)
{
    return status.state == "Playing" || status.state == "Paused" || status.state == "Buffering";
}

void PlaybackTracker::SetDevice(std::optional<CastDevice> device)
{
    m_thread.Cancel(m_timer);
    m_timer = 0;
    ++m_generation;
    m_device = std::move(device);
    m_status.reset();
    m_querying = false;

    if (m_device)
    {
        QueryNow();
        SchedulePoll();
    }
}

void PlaybackTracker::SchedulePoll()
{
    m_timer = m_thread.PostAfter(m_interval,
        [this]
        {
            QueryNow();
            SchedulePoll();
        });
}

void PlaybackTracker::QueryNow()
{
    if (!m_device || m_querying)
    {
        return;
    }

    m_querying = true;
    Poll(*m_device, m_context(), m_generation);
}

void PlaybackTracker::Poll(const CastDevice& device, const PollContext& context, std::uint64_t generation)
{
    m_worker.Run(
        [this, device, context, generation]
        {
            PolledStatus polled{device, m_backend.QueryStatus(device), context, {}};
            const auto& status = polled.status;

            // isFromThisServer, not just "something plays": another
            // server's content isn't this file.
            if (context.progress && status.isFromThisServer && IsControllable(status))
            {
                m_backend.ReportPlaybackProgress(context.progress->fastHash, status.currentTimeSeconds,
                    context.progress->audioTrackId, context.progress->subtitleTrackId);
            }
            if (!context.judgedFastHash.empty())
            {
                polled.endKind = m_backend.ClassifyCastEnd(context.judgedFastHash, status);
            }

            m_thread.Post([this, polled, generation] { OnPolled(polled, generation); });
        });
}

void PlaybackTracker::OnPolled(const PolledStatus& polled, std::uint64_t generation)
{
    if (generation != m_generation)
    {
        return;
    }

    m_querying = false;
    m_status = polled.status;
    m_onStatus(polled);
}

}  // namespace mango
