#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <expected>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "../link/LinkTransport.hpp"
#include "ForwardMessages.hpp"

namespace mango::remote
{

class ForwardingClient;

// One request B forwarded to A, read by the HTTP handler that serves the
// TV. Made by ForwardingClient::Start; dropping it before the end cancels
// the request on A.
class ForwardExchange
{
   public:
    enum class Read
    {
        Data,
        End,
        Failed,
    };

    ForwardExchange(std::weak_ptr<ForwardingClient> client, std::uint32_t exchangeId);
    ~ForwardExchange();

    ForwardExchange(const ForwardExchange&) = delete;
    ForwardExchange& operator=(const ForwardExchange&) = delete;

    // Blocks for A's status and headers. The error when A answered with
    // one, the link dropped, or nothing came within timeout.
    [[nodiscard]] std::expected<forward::HeadMessage, forward::ErrorMessage> WaitHead(
        std::chrono::milliseconds timeout);

    // Blocks for the next body bytes, put in out. End once the whole body
    // was read; Failed when A reported an error, the link dropped, or
    // nothing came within timeout.
    [[nodiscard]] Read Next(std::string& out, std::chrono::milliseconds timeout);

    // bytes more were written to the TV: lets A send more.
    void NoteWritten(std::size_t bytes);

    // Tells A to stop, unless the response is already complete.
    void Cancel();

    [[nodiscard]] std::uint32_t ExchangeId() const { return m_exchangeId; }

   private:
    friend class ForwardingClient;

    // From ForwardingClient, any thread.
    void Deliver(const forward::Frame& frame);
    void Fail(const std::string& message);

    const std::weak_ptr<ForwardingClient> m_client;
    const std::uint32_t m_exchangeId;

    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::optional<forward::HeadMessage> m_head;
    std::optional<forward::ErrorMessage> m_error;
    std::deque<std::string> m_chunks;
    bool m_ended = false;
    bool m_cancelled = false;

    std::uint64_t m_written = 0;
    std::uint64_t m_lastAck = 0;
};

// B's end of HTTP forwarding to one peer: sends requests over the link and
// hands each response's frames to its ForwardExchange. Thread-safe.
class ForwardingClient : public std::enable_shared_from_this<ForwardingClient>
{
   public:
    // B acks after this many bytes were written to the TV.
    static constexpr std::uint64_t kAckEvery = 256 * 1024;

    explicit ForwardingClient(std::shared_ptr<LinkTransport> transport);
    ~ForwardingClient();

    ForwardingClient(const ForwardingClient&) = delete;
    ForwardingClient& operator=(const ForwardingClient&) = delete;

    // Sends request; nullptr when the link is gone.
    [[nodiscard]] std::shared_ptr<ForwardExchange> Start(const forward::RequestMessage& request);

    // A frame from A.
    void OnFrame(const forward::Frame& frame);

    // The link dropped: every open exchange fails.
    void FailAll();

   private:
    friend class ForwardExchange;

    void Send(std::string frame);
    void Forget(std::uint32_t exchangeId);

    const std::shared_ptr<LinkTransport> m_transport;

    std::mutex m_mutex;
    std::map<std::uint32_t, std::weak_ptr<ForwardExchange>> m_exchanges;
    std::uint32_t m_nextExchangeId = 1;
    bool m_failed = false;
};

// A whole forwarded response, read into memory.
struct WholeResponse
{
    forward::HeadMessage head;
    std::string body;
};

// GETs path from the peer and reads the whole body; std::nullopt when the
// link fails, A answers with an error, or the body is over maxBytes.
[[nodiscard]] std::optional<WholeResponse> FetchWhole(
    ForwardingClient& client, const std::string& path, std::size_t maxBytes, std::chrono::milliseconds timeout);

}  // namespace mango::remote
