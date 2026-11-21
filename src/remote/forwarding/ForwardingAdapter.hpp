#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "../link/LinkTransport.hpp"
#include "ForwardMessages.hpp"

namespace httplib
{
class Client;
}

namespace mango::remote
{

// A response A makes itself rather than fetching it from its HTTP server.
struct LocalResponse
{
    int status = 200;
    std::string contentType;
    std::string body;
};

// A's end of HTTP forwarding (step 6 of
// docs/scope/hybrid_streaming_architecture.md): answers B's requests by
// calling A's own HTTP server on its listening address, and streams the
// response back as head, chunks and end. Only /stream/<id>/<file> and
// /cover/<id> reach the server; Host::localRoute answers paths A serves in
// process (the library and its covers). Everything else is refused.
//
// Each request runs on its own thread, up to kMaxRequests at once. A
// request pauses while the channel holds more than kMaxBuffered unsent
// bytes, or while B is more than kWindow bytes behind in writing to its TV,
// so memory stays bounded on both sides.
class ForwardingAdapter
{
   public:
    struct Host
    {
        // A's HTTP server, "http://<ip>:<port>"; std::nullopt while it
        // isn't listening.
        std::function<std::optional<std::string>()> serverUrl;

        // A path A answers in process; std::nullopt when it isn't one.
        std::function<std::optional<LocalResponse>(const std::string& path)> localRoute;
    };

    static constexpr std::size_t kMaxRequests = 16;
    static constexpr std::size_t kMaxBuffered = 1024 * 1024;
    static constexpr std::uint64_t kWindow = 4 * 1024 * 1024;

    ForwardingAdapter(std::shared_ptr<LinkTransport> transport, Host host);

    // Cancels every request and waits for their threads.
    ~ForwardingAdapter();

    ForwardingAdapter(const ForwardingAdapter&) = delete;
    ForwardingAdapter& operator=(const ForwardingAdapter&) = delete;

    // A frame from B. Any thread; never blocks on the network.
    void OnFrame(const forward::Frame& frame);

    // /stream/<id>/<file> or /cover/<id>, each part a plain name.
    [[nodiscard]] static bool IsServerPath(std::string_view path);

   private:
    struct Job
    {
        std::uint32_t exchangeId = 0;
        forward::RequestMessage request;
        std::atomic<bool> cancelled{false};
        std::atomic<bool> done{false};

        std::mutex mutex;
        std::condition_variable progress;
        std::uint64_t sent = 0;
        std::uint64_t acked = 0;
        // The request to A's server in flight, stopped by a cancel.
        httplib::Client* client = nullptr;

        std::thread thread;
    };

    void Begin(std::uint32_t exchangeId, forward::RequestMessage request);
    void Cancel(std::uint32_t exchangeId);
    static void Stop(Job& job);
    void Acknowledge(std::uint32_t exchangeId, std::uint64_t bytesWritten);
    [[nodiscard]] Job* FindLocked(std::uint32_t exchangeId);

    // Joins the threads of finished requests. Called with m_mutex held.
    void ReapLocked();

    // On the request's thread.
    void Run(Job& job);
    void ServeLocal(Job& job, const LocalResponse& response);
    void FetchFromServer(Job& job, const std::string& serverUrl);
    void FetchHead(Job& job, const std::string& serverUrl);

    // Makes client the one a cancel stops; false when already cancelled.
    static bool Attach(Job& job, httplib::Client* client);
    bool SendHead(Job& job, int status, forward::Headers headers);
    bool SendBody(Job& job, const char* data, std::size_t size);
    bool WaitForRoom(Job& job);
    void SendEnd(Job& job);
    void SendError(std::uint32_t exchangeId, int status, const std::string& message);
    bool Send(Job& job, std::string frame);

    const std::shared_ptr<LinkTransport> m_transport;
    const Host m_host;

    std::mutex m_mutex;
    std::map<std::uint32_t, std::unique_ptr<Job>> m_jobs;
    bool m_closing = false;
};

}  // namespace mango::remote
