#include "StreamStarter.hpp"

#include "log/Log.hpp"

namespace mango
{

namespace
{

bool Switched(const std::string& result) { return result == "Success" || result == "SentUnconfirmed"; }

}  // namespace

StreamStarter::StreamStarter(SessionBackend& backend, SessionThread& thread, CastWorker& worker,
    std::chrono::milliseconds readyPollInterval, StreamStarterCallbacks callbacks)
    : m_backend(backend),
      m_thread(thread),
      m_worker(worker),
      m_readyPollInterval(readyPollInterval),
      m_callbacks(std::move(callbacks))
{
}

bool StreamStarter::Start(StreamRequest request)
{
    if (m_starting)
    {
        return false;
    }

    m_starting = true;
    m_callbacks.onStateChanged();
    Initiate(request);
    return true;
}

void StreamStarter::Initiate(const StreamRequest& request)
{
    m_worker.Run(
        [this, request]
        {
            const auto result = m_backend.InitiateStream(
                request.entry.fastHash, request.tracks, request.playlist, request.deviceKey);

            std::string switchResult;
            if (request.inPlaceDevice && result.success)
            {
                switchResult = m_backend.SetSubtitleTrack(
                    *request.inPlaceDevice, request.entry.fastHash, result.subtitleTrackId);
            }

            m_thread.Post([this, request, result, switchResult] { OnInitiated(request, result, switchResult); });
        });
}

void StreamStarter::OnInitiated(
    const StreamRequest& request, const StreamInitiation& result, const std::string& switchResult)
{
    if (request.inPlaceDevice && result.success)
    {
        const bool switched = Switched(switchResult);
        Log::Info(LogCategory::App, "subtitle {} in place: {}{}", result.subtitleTrackId, switchResult,
            switched ? "" : ", reloading instead");
        if (switched)
        {
            Finish();
            m_callbacks.onSwitchedInPlace(request, result);
            return;
        }
        // Never declared to the receiver (first pick since the last
        // LOAD), or the TV didn't answer: reload instead.
    }

    if (!result.success)
    {
        Log::Error(LogCategory::App, "stream start failed for {}: {}", request.entry.fastHash,
            result.error.empty() ? "no result" : result.error);
        Finish();
        m_callbacks.onFailed(request, result.error);
        return;
    }

    if (result.startedNewTranscode && request.handsOver)
    {
        BeginHeadStart(request, result);
        return;
    }

    Finish();
    m_callbacks.onReady(request, result);
}

void StreamStarter::BeginHeadStart(const StreamRequest& request, const StreamInitiation& result)
{
    const auto delay = std::chrono::milliseconds(m_backend.StreamStartDelayMs());
    if (delay <= std::chrono::milliseconds::zero())
    {
        Finish();
        m_callbacks.onReady(request, result);
        return;
    }

    const auto now = std::chrono::system_clock::now();
    m_wait = WaitRange{now, now + delay};
    m_callbacks.onStateChanged();
    PollHeadStart(request, result, std::chrono::steady_clock::now() + delay);
}

void StreamStarter::PollHeadStart(
    const StreamRequest& request, const StreamInitiation& result, std::chrono::steady_clock::time_point end)
{
    if (std::chrono::steady_clock::now() >= end || m_backend.IsStreamStartReady())
    {
        Finish();
        m_callbacks.onReady(request, result);
        return;
    }

    m_thread.PostAfter(m_readyPollInterval, [this, request, result, end] { PollHeadStart(request, result, end); });
}

void StreamStarter::Finish()
{
    m_starting = false;
    m_wait.reset();
    m_callbacks.onStateChanged();
}

}  // namespace mango
