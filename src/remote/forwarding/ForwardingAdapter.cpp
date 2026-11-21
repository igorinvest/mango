#include "ForwardingAdapter.hpp"

#include <array>
#include <chrono>

#include <httplib.h>

#include "../../log/Log.hpp"
#include "../../server/RequestGuard.hpp"

namespace mango::remote
{

namespace
{

using namespace std::chrono_literals;

// A segment can wait for the conversion to reach it, and a subtitle for its
// extraction (see ServerWrapper's WaitForSubtitleTrack).
constexpr auto kReadTimeout = 120s;
constexpr auto kConnectTimeout = 5s;

// B stopped reading without a cancel: its link is likely gone.
constexpr auto kStallTimeout = 60s;

constexpr auto kRoomPollInterval = 10ms;

// What B's TV may send on to A's server.
constexpr std::array kRequestHeaders{"Range", "If-Range"};

// What A's server answers that the TV needs.
constexpr std::array kResponseHeaders{
    "Content-Type", "Content-Length", "Content-Range", "Accept-Ranges", "Cache-Control", "Last-Modified", "ETag"};

std::vector<std::string_view> SplitPath(std::string_view path)
{
    std::vector<std::string_view> parts;
    while (!path.empty())
    {
        const auto slash = path.find('/');
        parts.push_back(path.substr(0, slash));
        path = slash == std::string_view::npos ? std::string_view() : path.substr(slash + 1);
    }
    return parts;
}

httplib::Headers RequestHeaders(const forward::Headers& headers)
{
    httplib::Headers passed;
    for (const auto* name : kRequestHeaders)
    {
        if (auto value = forward::FindHeader(headers, name); !value.empty())
        {
            passed.emplace(name, std::move(value));
        }
    }
    return passed;
}

forward::Headers ResponseHeaders(const httplib::Headers& headers)
{
    forward::Headers passed;
    for (const auto* name : kResponseHeaders)
    {
        if (const auto found = headers.find(name); found != headers.end())
        {
            passed.emplace_back(name, found->second);
        }
    }
    return passed;
}

std::unique_ptr<httplib::Client> MakeClient(const std::string& serverUrl)
{
    auto client = std::make_unique<httplib::Client>(serverUrl);
    client->set_connection_timeout(kConnectTimeout);
    client->set_read_timeout(kReadTimeout);
    client->set_keep_alive(false);
    return client;
}

}  // namespace

ForwardingAdapter::ForwardingAdapter(std::shared_ptr<LinkTransport> transport, Host host)
    : m_transport(std::move(transport)), m_host(std::move(host))
{
}

ForwardingAdapter::~ForwardingAdapter()
{
    std::map<std::uint32_t, std::unique_ptr<Job>> jobs;
    {
        std::lock_guard lock(m_mutex);
        m_closing = true;
        jobs = std::move(m_jobs);
    }

    for (auto& [exchangeId, job] : jobs)
    {
        Stop(*job);
    }
    for (auto& [exchangeId, job] : jobs)
    {
        if (job->thread.joinable())
        {
            job->thread.join();
        }
    }
}

void ForwardingAdapter::OnFrame(const forward::Frame& frame)
{
    switch (frame.type)
    {
        case forward::FrameType::Request:
            if (auto request = forward::ReadRequest(frame))
            {
                Begin(frame.exchangeId, std::move(*request));
            }
            else
            {
                SendError(frame.exchangeId, 400, "bad request");
            }
            break;
        case forward::FrameType::Cancel: Cancel(frame.exchangeId); break;
        case forward::FrameType::Ack:
            if (const auto bytes = forward::ReadAck(frame))
            {
                Acknowledge(frame.exchangeId, *bytes);
            }
            break;
        default: break;  // A only receives requests, cancels and acks.
    }
}

bool ForwardingAdapter::IsServerPath(std::string_view path)
{
    if (!path.starts_with('/'))
    {
        return false;
    }

    const auto parts = SplitPath(path.substr(1));
    const auto safe = [&](std::size_t i) { return RequestGuard::IsSafePathSegment(parts[i]); };
    if (parts.size() == 3 && parts[0] == "stream")
    {
        return safe(1) && safe(2);
    }
    return parts.size() == 2 && parts[0] == "cover" && safe(1);
}

void ForwardingAdapter::Begin(std::uint32_t exchangeId, forward::RequestMessage request)
{
    std::lock_guard lock(m_mutex);
    if (m_closing)
    {
        return;
    }

    ReapLocked();
    if (m_jobs.contains(exchangeId))
    {
        return;  // A repeated exchange ID: B's bug, the first one keeps running.
    }
    if (m_jobs.size() >= kMaxRequests)
    {
        Log::Warning(LogCategory::Remote, "forwarding: too many requests, refused {}", request.path);
        SendError(exchangeId, 503, "busy");
        return;
    }

    auto job = std::make_unique<Job>();
    job->exchangeId = exchangeId;
    job->request = std::move(request);
    auto& started = *job;
    m_jobs.emplace(exchangeId, std::move(job));
    started.thread = std::thread([this, &started] {
        Run(started);
        started.done = true;
    });
}

void ForwardingAdapter::Cancel(std::uint32_t exchangeId)
{
    std::lock_guard lock(m_mutex);
    if (auto* job = FindLocked(exchangeId))
    {
        Stop(*job);
    }
}

void ForwardingAdapter::Stop(Job& job)
{
    {
        std::lock_guard lock(job.mutex);
        job.cancelled = true;
        if (job.client)
        {
            job.client->stop();
        }
    }
    job.progress.notify_all();
}

void ForwardingAdapter::Acknowledge(std::uint32_t exchangeId, std::uint64_t bytesWritten)
{
    std::lock_guard lock(m_mutex);
    if (auto* job = FindLocked(exchangeId))
    {
        {
            std::lock_guard jobLock(job->mutex);
            job->acked = std::max(job->acked, bytesWritten);
        }
        job->progress.notify_all();
    }
}

ForwardingAdapter::Job* ForwardingAdapter::FindLocked(std::uint32_t exchangeId)
{
    const auto found = m_jobs.find(exchangeId);
    return found != m_jobs.end() ? found->second.get() : nullptr;
}

void ForwardingAdapter::ReapLocked()
{
    std::erase_if(m_jobs, [](auto& entry) {
        auto& job = *entry.second;
        if (!job.done)
        {
            return false;
        }
        job.thread.join();
        return true;
    });
}

void ForwardingAdapter::Run(Job& job)
{
    const auto& request = job.request;
    if (request.method != "GET" && request.method != "HEAD")
    {
        SendError(job.exchangeId, 405, "method not allowed");
        return;
    }

    if (const auto local = m_host.localRoute ? m_host.localRoute(request.path) : std::nullopt)
    {
        ServeLocal(job, *local);
        return;
    }

    if (!IsServerPath(request.path))
    {
        Log::Warning(LogCategory::Remote, "forwarding: refused {}", request.path);
        SendError(job.exchangeId, 404, "not forwarded");
        return;
    }

    const auto serverUrl = m_host.serverUrl ? m_host.serverUrl() : std::nullopt;
    if (!serverUrl)
    {
        SendError(job.exchangeId, 404, "nothing is streaming");
        return;
    }

    if (request.method == "HEAD")
    {
        FetchHead(job, *serverUrl);
    }
    else
    {
        FetchFromServer(job, *serverUrl);
    }
}

void ForwardingAdapter::ServeLocal(Job& job, const LocalResponse& response)
{
    forward::Headers headers{{"Content-Type", response.contentType},
        {"Content-Length", std::to_string(response.body.size())}};
    if (!SendHead(job, response.status, std::move(headers)))
    {
        return;
    }
    if (job.request.method == "GET" && !SendBody(job, response.body.data(), response.body.size()))
    {
        return;
    }
    SendEnd(job);
}

void ForwardingAdapter::FetchFromServer(Job& job, const std::string& serverUrl)
{
    const auto client = MakeClient(serverUrl);
    if (!Attach(job, client.get()))
    {
        return;
    }
    bool headSent = false;

    const auto result = client->Get(
        job.request.path, RequestHeaders(job.request.headers),
        [&](const httplib::Response& response) {
            headSent = SendHead(job, response.status, ResponseHeaders(response.headers));
            return headSent;
        },
        [&](const char* data, std::size_t size) { return SendBody(job, data, size); });
    Attach(job, nullptr);

    if (job.cancelled)
    {
        return;  // B asked for it, or the link is gone: nothing to tell.
    }
    if (!result)
    {
        Log::Warning(LogCategory::Remote, "forwarding {} failed: {}", job.request.path, httplib::to_string(result.error()));
        SendError(job.exchangeId, 502, headSent ? "response cut off" : "server unreachable");
        return;
    }
    SendEnd(job);
}

void ForwardingAdapter::FetchHead(Job& job, const std::string& serverUrl)
{
    const auto client = MakeClient(serverUrl);
    if (!Attach(job, client.get()))
    {
        return;
    }
    const auto result = client->Head(job.request.path, RequestHeaders(job.request.headers));
    Attach(job, nullptr);
    if (!result)
    {
        SendError(job.exchangeId, 502, "server unreachable");
        return;
    }
    if (SendHead(job, result->status, ResponseHeaders(result->headers)))
    {
        SendEnd(job);
    }
}

bool ForwardingAdapter::Attach(Job& job, httplib::Client* client)
{
    std::lock_guard lock(job.mutex);
    job.client = client;
    return !job.cancelled;
}

bool ForwardingAdapter::SendHead(Job& job, int status, forward::Headers headers)
{
    return Send(job, forward::EncodeHead(job.exchangeId, {.status = status, .headers = std::move(headers)}));
}

bool ForwardingAdapter::SendBody(Job& job, const char* data, std::size_t size)
{
    while (size > 0)
    {
        if (!WaitForRoom(job))
        {
            return false;
        }

        const auto part = std::min(size, forward::kChunkSize);
        if (!Send(job, forward::Encode(forward::FrameType::Chunk, job.exchangeId, std::string_view(data, part))))
        {
            return false;
        }
        {
            std::lock_guard lock(job.mutex);
            job.sent += part;
        }
        data += part;
        size -= part;
    }
    return true;
}

bool ForwardingAdapter::WaitForRoom(Job& job)
{
    std::unique_lock lock(job.mutex);
    auto lastAcked = job.acked;
    auto lastProgress = std::chrono::steady_clock::now();

    while (!job.cancelled && m_transport->IsOpen())
    {
        if (job.sent - job.acked < kWindow && m_transport->BufferedAmount() < kMaxBuffered)
        {
            return true;
        }

        if (job.acked != lastAcked)
        {
            lastAcked = job.acked;
            lastProgress = std::chrono::steady_clock::now();
        }
        else if (std::chrono::steady_clock::now() - lastProgress > kStallTimeout)
        {
            Log::Warning(LogCategory::Remote, "forwarding {}: B stopped reading", job.request.path);
            job.cancelled = true;
            break;
        }
        job.progress.wait_for(lock, kRoomPollInterval);
    }
    return false;
}

void ForwardingAdapter::SendEnd(Job& job) { Send(job, forward::Encode(forward::FrameType::End, job.exchangeId)); }

void ForwardingAdapter::SendError(std::uint32_t exchangeId, int status, const std::string& message)
{
    m_transport->Send(forward::EncodeError(exchangeId, {.status = status, .message = message}));
}

bool ForwardingAdapter::Send(Job& job, std::string frame)
{
    if (job.cancelled)
    {
        return false;
    }
    if (!m_transport->Send(std::move(frame)))
    {
        job.cancelled = true;
        return false;
    }
    return true;
}

}  // namespace mango::remote
