#include "RemoteLinkHandler.hpp"

#include <array>
#include <charconv>
#include <format>
#include <optional>
#include <string_view>

#include <httplib.h>

#include "../../log/Log.hpp"
#include "../../server/StreamActivity.hpp"
#include "ForwardingAdapter.hpp"
#include "ForwardingClient.hpp"

namespace mango::remote
{

namespace
{

// An error page from A is small; anything past this is dropped.
constexpr std::size_t kMaxErrorBody = 64 * 1024;

constexpr std::array kPassedHeaders{"Accept-Ranges", "Cache-Control", "Last-Modified", "ETag"};

std::optional<std::uint64_t> ParseNumber(std::string_view text)
{
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size() || text.empty())
    {
        return std::nullopt;
    }
    return value;
}

// Where A's body starts in the whole resource, and the resource's size
// (std::nullopt when A didn't say).
struct BodyRange
{
    std::uint64_t start = 0;
    std::optional<std::uint64_t> total;
};

// "bytes 100-199/1000".
std::optional<BodyRange> ParseContentRange(std::string_view value)
{
    constexpr std::string_view prefix = "bytes ";
    const auto dash = value.find('-');
    const auto slash = value.find('/');
    if (!value.starts_with(prefix) || dash == std::string_view::npos || slash == std::string_view::npos || dash > slash)
    {
        return std::nullopt;
    }

    const auto start = ParseNumber(value.substr(prefix.size(), dash - prefix.size()));
    const auto total = ParseNumber(value.substr(slash + 1));
    if (!start || !total)
    {
        return std::nullopt;
    }
    return BodyRange{.start = *start, .total = *total};
}

BodyRange RangeOf(const forward::HeadMessage& head)
{
    if (const auto range = ParseContentRange(forward::FindHeader(head.headers, "Content-Range")))
    {
        return *range;
    }
    return BodyRange{.start = 0, .total = ParseNumber(forward::FindHeader(head.headers, "Content-Length"))};
}

// A's body as httplib asks for it: one pass, front to back, from start.
// Skips up to the offset httplib wants (A sent the whole file for a Range
// it didn't apply), and can't go back.
class BodyStream
{
   public:
    BodyStream(std::shared_ptr<ForwardExchange> exchange, std::uint64_t start)
        : m_exchange(std::move(exchange)), m_position(start)
    {
    }

    // Known size: httplib's (offset, length) calls.
    bool WriteRange(std::uint64_t offset, std::uint64_t length, httplib::DataSink& sink)
    {
        if (offset < m_position || !Skip(offset - m_position))
        {
            return false;
        }
        while (length > 0)
        {
            const auto written = WriteSome(sink, length);
            if (written == 0)
            {
                return false;
            }
            length -= written;
        }
        return true;
    }

    // Unknown size: chunked, until A's end.
    bool WriteChunked(httplib::DataSink& sink)
    {
        if (!Fill())
        {
            if (m_ended)
            {
                sink.done();
                return true;
            }
            return false;
        }
        return WriteSome(sink, m_buffer.size() - m_used) > 0;
    }

    void Cancel() { m_exchange->Cancel(); }

   private:
    // Has unread bytes, reading A's next chunk if needed.
    bool Fill()
    {
        while (m_used == m_buffer.size())
        {
            m_used = 0;
            m_buffer.clear();
            const auto read = m_exchange->Next(m_buffer, RemoteLinkHandler::kChunkTimeout);
            if (read != ForwardExchange::Read::Data)
            {
                m_ended = read == ForwardExchange::Read::End;
                return false;
            }
        }
        return true;
    }

    bool Skip(std::uint64_t bytes)
    {
        while (bytes > 0)
        {
            if (!Fill())
            {
                return false;
            }
            const auto part = std::min<std::uint64_t>(bytes, m_buffer.size() - m_used);
            m_used += part;
            m_position += part;
            bytes -= part;
        }
        return true;
    }

    // Up to limit bytes; 0 when nothing could be written.
    std::uint64_t WriteSome(httplib::DataSink& sink, std::uint64_t limit)
    {
        if (!sink.is_writable() || !Fill())
        {
            return 0;
        }
        const auto part = std::min<std::uint64_t>(limit, m_buffer.size() - m_used);
        if (!sink.write(m_buffer.data() + m_used, part))
        {
            return 0;
        }
        m_used += part;
        m_position += part;
        m_exchange->NoteWritten(part);
        return part;
    }

    std::shared_ptr<ForwardExchange> m_exchange;
    std::uint64_t m_position;
    std::string m_buffer;
    std::size_t m_used = 0;
    bool m_ended = false;
};

forward::RequestMessage MakeRequest(const httplib::Request& request, const std::string& path)
{
    forward::RequestMessage message{.method = request.method == "HEAD" ? "HEAD" : "GET", .path = path, .headers = {}};
    for (const auto* name : {"Range", "If-Range"})
    {
        if (request.has_header(name))
        {
            message.headers.emplace_back(name, request.get_header_value(name));
        }
    }
    return message;
}

void Reject(httplib::Response& response, int status, std::string_view reason)
{
    response.status = status;
    response.set_content(std::format(R"({{"error":"{}"}})", reason), "application/json");
}

void CopyHeaders(const forward::HeadMessage& head, httplib::Response& response)
{
    response.set_header("Access-Control-Allow-Origin", "*");
    response.set_header("Access-Control-Expose-Headers", "Content-Length, Content-Range");
    for (const auto* name : kPassedHeaders)
    {
        if (auto value = forward::FindHeader(head.headers, name); !value.empty())
        {
            response.set_header(name, value);
        }
    }
}

// A's non-2xx answer, read whole: it's small.
void ServeError(ForwardExchange& exchange, const forward::HeadMessage& head, httplib::Response& response)
{
    std::string body;
    std::string chunk;
    while (body.size() < kMaxErrorBody &&
           exchange.Next(chunk, RemoteLinkHandler::kChunkTimeout) == ForwardExchange::Read::Data)
    {
        body += chunk;
    }
    response.status = head.status;
    response.set_content(body, forward::FindHeader(head.headers, "Content-Type"));
}

void ServeBody(std::shared_ptr<ForwardExchange> exchange, const forward::HeadMessage& head,
    httplib::Response& response)
{
    const auto range = RangeOf(head);
    const auto contentType = forward::FindHeader(head.headers, "Content-Type");
    auto stream = std::make_shared<BodyStream>(std::move(exchange), range.start);
    const auto release = [stream](bool) { stream->Cancel(); };

    if (range.total)
    {
        // Status left unset: httplib answers 200, or 206 with the TV's
        // range of total.
        response.set_content_provider(
            *range.total, contentType,
            [stream](std::size_t offset, std::size_t length, httplib::DataSink& sink) {
                return stream->WriteRange(offset, length, sink);
            },
            release);
        return;
    }

    response.status = head.status;
    response.set_chunked_content_provider(
        contentType, [stream](std::size_t, httplib::DataSink& sink) { return stream->WriteChunked(sink); }, release);
}

}  // namespace

RemoteLinkHandler::RemoteLinkHandler(ClientLookup lookup) : m_lookup(std::move(lookup)) {}

void RemoteLinkHandler::Handle(const httplib::Request& request, httplib::Response& response) const
{
    const std::string peerId = request.matches[1];
    const std::string path = request.matches[2];

    // What A's own HTTP server serves, nothing more: the library and its
    // covers are for this device, not for anyone on its network.
    if (!ForwardingAdapter::IsServerPath(path))
    {
        Reject(response, 404, "not found");
        return;
    }

    const auto client = m_lookup ? m_lookup(peerId) : nullptr;
    if (!client)
    {
        Log::Warning(LogCategory::Remote, "{} {}: {} isn't connected", request.method, path, peerId.substr(0, 8));
        Reject(response, 503, "peer not connected");
        return;
    }
    if (request.ranges.size() > 1)
    {
        Reject(response, 416, "one range only");
        return;
    }

    if (path.starts_with("/stream/"))
    {
        StreamActivity::NoteRequest();
    }

    auto exchange = client->Start(MakeRequest(request, path));
    const auto head = exchange ? exchange->WaitHead(kHeadTimeout)
                               : std::unexpected(forward::ErrorMessage{.status = 503, .message = "link closed"});
    if (!head)
    {
        Log::Warning(LogCategory::Remote, "{} {} from {}: {} {}", request.method, path, peerId.substr(0, 8),
            head.error().status, head.error().message);
        Reject(response, head.error().status, head.error().message);
        return;
    }

    CopyHeaders(*head, response);
    if (head->status < 200 || head->status >= 300)
    {
        ServeError(*exchange, *head, response);
        return;
    }
    ServeBody(std::move(exchange), *head, response);
}

}  // namespace mango::remote
