#include "ForwardingClient.hpp"

namespace mango::remote
{

// ---- ForwardExchange --------------------------------------------------------

ForwardExchange::ForwardExchange(std::weak_ptr<ForwardingClient> client, std::uint32_t exchangeId)
    : m_client(std::move(client)), m_exchangeId(exchangeId)
{
}

ForwardExchange::~ForwardExchange()
{
    Cancel();
    if (const auto client = m_client.lock())
    {
        client->Forget(m_exchangeId);
    }
}

std::expected<forward::HeadMessage, forward::ErrorMessage> ForwardExchange::WaitHead(
    std::chrono::milliseconds timeout)
{
    std::unique_lock lock(m_mutex);
    if (!m_changed.wait_for(lock, timeout, [this] { return m_head || m_error; }))
    {
        return std::unexpected(forward::ErrorMessage{.status = 504, .message = "no answer"});
    }
    if (m_error)
    {
        return std::unexpected(*m_error);
    }
    return *m_head;
}

ForwardExchange::Read ForwardExchange::Next(std::string& out, std::chrono::milliseconds timeout)
{
    std::unique_lock lock(m_mutex);
    if (!m_changed.wait_for(lock, timeout, [this] { return !m_chunks.empty() || m_ended || m_error; }))
    {
        return Read::Failed;
    }
    if (!m_chunks.empty())
    {
        out = std::move(m_chunks.front());
        m_chunks.pop_front();
        return Read::Data;
    }
    return m_error ? Read::Failed : Read::End;
}

void ForwardExchange::NoteWritten(std::size_t bytes)
{
    std::uint64_t ack = 0;
    {
        std::lock_guard lock(m_mutex);
        m_written += bytes;
        if (m_written - m_lastAck < ForwardingClient::kAckEvery || m_ended)
        {
            return;
        }
        m_lastAck = m_written;
        ack = m_written;
    }

    if (const auto client = m_client.lock())
    {
        client->Send(forward::EncodeAck(m_exchangeId, ack));
    }
}

void ForwardExchange::Cancel()
{
    {
        std::lock_guard lock(m_mutex);
        if (m_ended || m_error || m_cancelled)
        {
            return;
        }
        m_cancelled = true;
        m_error = forward::ErrorMessage{.status = 499, .message = "cancelled"};
    }
    m_changed.notify_all();

    if (const auto client = m_client.lock())
    {
        client->Send(forward::Encode(forward::FrameType::Cancel, m_exchangeId));
    }
}

void ForwardExchange::Deliver(const forward::Frame& frame)
{
    {
        std::lock_guard lock(m_mutex);
        if (m_ended || m_error)
        {
            return;
        }

        switch (frame.type)
        {
            case forward::FrameType::Head:
                m_head = forward::ReadHead(frame);
                if (!m_head)
                {
                    m_error = forward::ErrorMessage{.status = 502, .message = "bad head"};
                }
                break;
            case forward::FrameType::Chunk: m_chunks.push_back(frame.body); break;
            case forward::FrameType::End: m_ended = true; break;
            case forward::FrameType::Error:
                m_error = forward::ReadError(frame).value_or(forward::ErrorMessage{});
                break;
            default: return;
        }
    }
    m_changed.notify_all();
}

void ForwardExchange::Fail(const std::string& message)
{
    {
        std::lock_guard lock(m_mutex);
        if (m_ended || m_error)
        {
            return;
        }
        m_error = forward::ErrorMessage{.status = 502, .message = message};
    }
    m_changed.notify_all();
}

// ---- ForwardingClient -------------------------------------------------------

ForwardingClient::ForwardingClient(std::shared_ptr<LinkTransport> transport)
    : m_transport(std::move(transport))
{
}

ForwardingClient::~ForwardingClient() { FailAll(); }

std::shared_ptr<ForwardExchange> ForwardingClient::Start(const forward::RequestMessage& request)
{
    std::shared_ptr<ForwardExchange> exchange;
    {
        std::lock_guard lock(m_mutex);
        if (m_failed || !m_transport->IsOpen())
        {
            return nullptr;
        }
        const auto exchangeId = m_nextExchangeId++;
        exchange = std::make_shared<ForwardExchange>(weak_from_this(), exchangeId);
        m_exchanges.emplace(exchangeId, exchange);
    }

    if (!m_transport->Send(forward::EncodeRequest(exchange->ExchangeId(), request)))
    {
        exchange->Fail("link closed");
    }
    return exchange;
}

void ForwardingClient::OnFrame(const forward::Frame& frame)
{
    std::shared_ptr<ForwardExchange> exchange;
    {
        std::lock_guard lock(m_mutex);
        const auto found = m_exchanges.find(frame.exchangeId);
        if (found == m_exchanges.end())
        {
            return;  // Cancelled and gone: A's late frames are dropped.
        }
        exchange = found->second.lock();
    }

    if (exchange)
    {
        exchange->Deliver(frame);
    }
}

void ForwardingClient::FailAll()
{
    std::map<std::uint32_t, std::weak_ptr<ForwardExchange>> exchanges;
    {
        std::lock_guard lock(m_mutex);
        m_failed = true;
        exchanges = std::move(m_exchanges);
    }

    for (auto& [exchangeId, weak] : exchanges)
    {
        if (const auto exchange = weak.lock())
        {
            exchange->Fail("link closed");
        }
    }
}

void ForwardingClient::Send(std::string frame) { m_transport->Send(std::move(frame)); }

void ForwardingClient::Forget(std::uint32_t exchangeId)
{
    std::lock_guard lock(m_mutex);
    m_exchanges.erase(exchangeId);
}

// ---- FetchWhole -------------------------------------------------------------

std::optional<WholeResponse> FetchWhole(
    ForwardingClient& client, const std::string& path, std::size_t maxBytes, std::chrono::milliseconds timeout)
{
    const auto exchange = client.Start({.method = "GET", .path = path, .headers = {}});
    if (!exchange)
    {
        return std::nullopt;
    }
    auto head = exchange->WaitHead(timeout);
    if (!head || head->status < 200 || head->status >= 300)
    {
        return std::nullopt;
    }

    WholeResponse response{.head = std::move(*head), .body = {}};
    std::string chunk;
    while (true)
    {
        const auto read = exchange->Next(chunk, timeout);
        if (read == ForwardExchange::Read::End)
        {
            return response;
        }
        if (read == ForwardExchange::Read::Failed || response.body.size() + chunk.size() > maxBytes)
        {
            return std::nullopt;  // Dropping the exchange cancels it.
        }
        response.body += chunk;
        exchange->NoteWritten(chunk.size());
    }
}

}  // namespace mango::remote
