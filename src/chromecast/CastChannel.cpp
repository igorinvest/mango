#include "CastChannel.hpp"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <glaze/glaze.hpp>
#include <vector>

#include "../log/Log.hpp"

namespace mango
{

// JSON shapes for the two CastV2 payloads read here. Both carry many more
// fields than modeled (icons, volume, media metadata, ...); reads use
// error_on_unknown_keys = false so those are simply ignored.
//
// Glaze's reflection needs these to have external linkage, so they sit
// outside the anonymous namespace below (matching ServerWrapper.cpp's DTOs).
struct CastApplicationDto
{
    std::string appId;
    std::string displayName;
    bool isIdleScreen = false;
    std::string transportId;
};

struct ReceiverStatusStatusDto
{
    std::vector<CastApplicationDto> applications;
};

struct ReceiverStatusMessageDto
{
    std::string type;
    ReceiverStatusStatusDto status;
};

struct MediaMetadataDto
{
    std::string title;
};

struct MediaInfoDto
{
    std::string contentId;  // a URL for browser/Default Media Receiver casts
    double duration = 0.0;
    MediaMetadataDto metadata;
};

struct MediaStatusEntryDto
{
    std::string playerState;  // "PLAYING", "PAUSED", "BUFFERING", or "IDLE"
    std::string idleReason;   // with "IDLE": "FINISHED", "CANCELLED", "INTERRUPTED" or "ERROR"
    int mediaSessionId = 0;
    double currentTime = 0.0;
    MediaInfoDto media;
};

struct MediaStatusMessageDto
{
    std::string type;
    std::vector<MediaStatusEntryDto> status;
};

namespace
{

constexpr const char* kSenderId = "sender-0";
constexpr const char* kReceiverId = "receiver-0";
constexpr const char* kConnectionNamespace = "urn:x-cast:com.google.cast.tp.connection";
constexpr const char* kReceiverNamespace = "urn:x-cast:com.google.cast.receiver";
constexpr const char* kMediaNamespace = "urn:x-cast:com.google.cast.media";

template <typename T>
std::optional<T> ParseJsonLenient(const std::string& json)
{
    T value;
    constexpr glz::opts lenientOpts{.error_on_unknown_keys = false};

    if (glz::read<lenientOpts>(value, json))
    {
        return std::nullopt;
    }

    return value;
}

// --- CastMessage wire format -------------------------------------------
//
// Every CastV2 message is a small, fixed-shape protobuf message (see
// Chromium's cast_channel.proto) framed on the wire as a 4-byte big-endian
// length followed by that many bytes of protobuf. The schema is stable and
// tiny enough (7 fields, all string/varint) to encode and decode by hand
// rather than pulling in a full protobuf dependency:
//
//   1: protocol_version (varint)   4: namespace       (string)
//   2: source_id        (string)   5: payload_type    (varint)
//   3: destination_id   (string)   6: payload_utf8    (string)

void AppendVarint(std::vector<uint8_t>& out, uint64_t value)
{
    while (value >= 0x80)
    {
        out.push_back(static_cast<uint8_t>(value) | 0x80);
        value >>= 7;
    }

    out.push_back(static_cast<uint8_t>(value));
}

void AppendVarintField(std::vector<uint8_t>& out, uint32_t fieldNumber, uint64_t value)
{
    AppendVarint(out, (static_cast<uint64_t>(fieldNumber) << 3) | 0);
    AppendVarint(out, value);
}

void AppendStringField(std::vector<uint8_t>& out, uint32_t fieldNumber, const std::string& value)
{
    AppendVarint(out, (static_cast<uint64_t>(fieldNumber) << 3) | 2);
    AppendVarint(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
}

std::vector<uint8_t> EncodeCastMessage(
    const std::string& sourceId, const std::string& destinationId, const std::string& ns, const std::string& payloadUtf8)
{
    std::vector<uint8_t> body;
    AppendVarintField(body, 1, 0);  // protocol_version = CASTV2_1_0
    AppendStringField(body, 2, sourceId);
    AppendStringField(body, 3, destinationId);
    AppendStringField(body, 4, ns);
    AppendVarintField(body, 5, 0);  // payload_type = STRING
    AppendStringField(body, 6, payloadUtf8);

    std::vector<uint8_t> framed = {
        static_cast<uint8_t>((body.size() >> 24) & 0xFF),
        static_cast<uint8_t>((body.size() >> 16) & 0xFF),
        static_cast<uint8_t>((body.size() >> 8) & 0xFF),
        static_cast<uint8_t>(body.size() & 0xFF),
    };
    framed.insert(framed.end(), body.begin(), body.end());

    return framed;
}

struct DecodedCastMessage
{
    std::string ns;
    std::string payloadUtf8;
};

bool ReadVarint(const uint8_t* data, size_t size, size_t& offset, uint64_t& value)
{
    value = 0;
    int shift = 0;

    while (true)
    {
        if (offset >= size || shift > 63)
        {
            return false;
        }

        const uint8_t byte = data[offset++];
        value |= static_cast<uint64_t>(byte & 0x7F) << shift;

        if (!(byte & 0x80))
        {
            return true;
        }

        shift += 7;
    }
}

std::optional<DecodedCastMessage> DecodeCastMessage(const uint8_t* data, size_t size)
{
    DecodedCastMessage message;
    size_t offset = 0;

    while (offset < size)
    {
        uint64_t tag = 0;
        if (!ReadVarint(data, size, offset, tag))
        {
            return std::nullopt;
        }

        const uint32_t fieldNumber = static_cast<uint32_t>(tag >> 3);
        const uint32_t wireType = static_cast<uint32_t>(tag & 0x7);

        if (wireType == 0)
        {
            uint64_t discard = 0;
            if (!ReadVarint(data, size, offset, discard))
            {
                return std::nullopt;
            }
        }
        else if (wireType == 2)
        {
            uint64_t length = 0;
            if (!ReadVarint(data, size, offset, length) || offset + length > size)
            {
                return std::nullopt;
            }

            std::string value(reinterpret_cast<const char*>(data + offset), static_cast<size_t>(length));
            offset += static_cast<size_t>(length);

            if (fieldNumber == 4)
            {
                message.ns = std::move(value);
            }
            else if (fieldNumber == 6)
            {
                message.payloadUtf8 = std::move(value);
            }
        }
        else if (wireType == 1)
        {
            if (offset + 8 > size)
            {
                return std::nullopt;
            }
            offset += 8;
        }
        else if (wireType == 5)
        {
            if (offset + 4 > size)
            {
                return std::nullopt;
            }
            offset += 4;
        }
        else
        {
            return std::nullopt;
        }
    }

    return message;
}

// --- TLS transport -------------------------------------------------------
//
// Chromecast's control channel is TLS from the first byte. Its certificate
// is self-signed and device-specific with no public CA trust anchor, so —
// like every Cast sender — verification is disabled; the goal is talking to
// whatever answered on the LAN, not authenticating it.
class TlsConnection
{
   public:
    TlsConnection()
    {
        mbedtls_net_init(&m_netContext);
        mbedtls_ssl_init(&m_ssl);
        mbedtls_ssl_config_init(&m_config);
        mbedtls_entropy_init(&m_entropy);
        mbedtls_ctr_drbg_init(&m_drbg);
    }

    ~TlsConnection()
    {
        mbedtls_ssl_free(&m_ssl);
        mbedtls_ssl_config_free(&m_config);
        mbedtls_ctr_drbg_free(&m_drbg);
        mbedtls_entropy_free(&m_entropy);
        mbedtls_net_free(&m_netContext);
    }

    TlsConnection(const TlsConnection&) = delete;
    TlsConnection& operator=(const TlsConnection&) = delete;

    bool Connect(const std::string& ipAddress, int port, int timeoutMs)
    {
        if (mbedtls_ctr_drbg_seed(&m_drbg, mbedtls_entropy_func, &m_entropy, nullptr, 0) != 0)
        {
            return false;
        }

        if (mbedtls_net_connect(
                &m_netContext, ipAddress.c_str(), std::to_string(port).c_str(), MBEDTLS_NET_PROTO_TCP) != 0)
        {
            return false;
        }

        if (mbedtls_ssl_config_defaults(
                &m_config, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0)
        {
            return false;
        }

        mbedtls_ssl_conf_authmode(&m_config, MBEDTLS_SSL_VERIFY_NONE);
        mbedtls_ssl_conf_rng(&m_config, mbedtls_ctr_drbg_random, &m_drbg);
        mbedtls_ssl_conf_read_timeout(&m_config, static_cast<uint32_t>(timeoutMs));

        if (mbedtls_ssl_setup(&m_ssl, &m_config) != 0)
        {
            return false;
        }

        // Blocking I/O with a read timeout: pass f_recv_timeout only, per
        // mbedtls_ssl_set_bio's documented "blocking I/O" mode.
        mbedtls_ssl_set_bio(&m_ssl, &m_netContext, mbedtls_net_send, nullptr, mbedtls_net_recv_timeout);

        int ret = 0;
        while ((ret = mbedtls_ssl_handshake(&m_ssl)) != 0)
        {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                return false;
            }
        }

        return true;
    }

    bool Send(const std::vector<uint8_t>& bytes)
    {
        size_t sent = 0;

        while (sent < bytes.size())
        {
            const int ret = mbedtls_ssl_write(&m_ssl, bytes.data() + sent, bytes.size() - sent);

            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                continue;
            }

            if (ret < 0)
            {
                return false;
            }

            sent += static_cast<size_t>(ret);
        }

        return true;
    }

    bool ReadExact(uint8_t* buffer, size_t length)
    {
        size_t received = 0;

        while (received < length)
        {
            const int ret = mbedtls_ssl_read(&m_ssl, buffer + received, length - received);

            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                continue;
            }

            if (ret <= 0)
            {
                return false;
            }

            received += static_cast<size_t>(ret);
        }

        return true;
    }

   private:
    mbedtls_net_context m_netContext;
    mbedtls_entropy_context m_entropy;
    mbedtls_ctr_drbg_context m_drbg;
    mbedtls_ssl_context m_ssl;
    mbedtls_ssl_config m_config;
};

// Reads framed messages until one arrives on `expectedNamespace`, ignoring
// anything else (e.g. a heartbeat PING). Bounded by `deadline` so a run of
// unrelated messages can't stall well past the caller's timeout budget.
std::optional<DecodedCastMessage> ReceiveOnNamespace(
    TlsConnection& connection, const std::string& expectedNamespace, std::chrono::steady_clock::time_point deadline)
{
    while (std::chrono::steady_clock::now() < deadline)
    {
        uint8_t lengthPrefix[4];
        if (!connection.ReadExact(lengthPrefix, sizeof(lengthPrefix)))
        {
            return std::nullopt;
        }

        const uint32_t bodyLength = (static_cast<uint32_t>(lengthPrefix[0]) << 24) |
                                     (static_cast<uint32_t>(lengthPrefix[1]) << 16) |
                                     (static_cast<uint32_t>(lengthPrefix[2]) << 8) | static_cast<uint32_t>(lengthPrefix[3]);

        if (bodyLength == 0 || bodyLength > (1u << 20))
        {
            return std::nullopt;
        }

        std::vector<uint8_t> body(bodyLength);
        if (!connection.ReadExact(body.data(), body.size()))
        {
            return std::nullopt;
        }

        auto message = DecodeCastMessage(body.data(), body.size());
        if (message && message->ns == expectedNamespace)
        {
            return message;
        }
    }

    return std::nullopt;
}

}  // namespace

namespace
{

// The CastV2 message type ("LOAD", "GET_STATUS", ...) of a JSON payload.
std::string_view MessageType(std::string_view payload)
{
    constexpr std::string_view key = R"("type":")";
    const auto start = payload.find(key);
    if (start == std::string_view::npos)
    {
        return "?";
    }
    const auto valueStart = start + key.size();
    return payload.substr(valueStart, payload.find('"', valueStart) - valueStart);
}

// CONNECT and GET_STATUS go out on every status poll: Debug only.
// Everything else is something the app asked the TV to do.
bool IsRoutineMessage(std::string_view type) { return type == "CONNECT" || type == "GET_STATUS"; }

}  // namespace

struct CastChannel::Impl
{
    TlsConnection connection;
    std::string device;

    // Every message sent to the receiver goes through here, so all of
    // them are in the log.
    bool Send(const std::string& destination, const std::string& ns, const std::string& payload)
    {
        const auto type = MessageType(payload);
        Log::Format(IsRoutineMessage(type) ? LogLevel::Debug : LogLevel::Info, LogCategory::Cast,
            "-> {} [{} {}] {}", device, destination, ns, payload);

        if (!connection.Send(EncodeCastMessage(kSenderId, destination, ns, payload)))
        {
            Log::Warning(LogCategory::Cast, "-> {}: sending {} failed", device, type);
            return false;
        }
        return true;
    }
};

CastChannel::CastChannel() : impl(std::make_unique<Impl>()) {}

CastChannel::~CastChannel() = default;

bool CastChannel::Connect(const std::string& ipAddress, int port, int timeoutMs)
{
    impl->device = std::format("{}:{}", ipAddress, port);
    if (!impl->connection.Connect(ipAddress, port, timeoutMs))
    {
        // Debug: a TV that's off fails every poll.
        Log::Debug(LogCategory::Cast, "can't connect to {}", impl->device);
        return false;
    }
    return true;
}

std::optional<ActiveMediaSession> CastChannel::ResolveActiveMedia(std::chrono::steady_clock::time_point deadline)
{
    if (!impl->Send(kReceiverId, kConnectionNamespace, R"({"type":"CONNECT"})") ||
        !impl->Send(kReceiverId, kReceiverNamespace, R"({"type":"GET_STATUS","requestId":1})"))
    {
        return std::nullopt;
    }

    auto receiverReply = ReceiveOnNamespace(impl->connection, kReceiverNamespace, deadline);
    if (!receiverReply)
    {
        return std::nullopt;
    }

    auto receiverStatus = ParseJsonLenient<ReceiverStatusMessageDto>(receiverReply->payloadUtf8);
    if (!receiverStatus)
    {
        return std::nullopt;
    }

    ActiveMediaSession session;

    const auto activeApp = std::find_if(receiverStatus->status.applications.begin(),
        receiverStatus->status.applications.end(), [](const CastApplicationDto& app) { return !app.isIdleScreen; });

    if (activeApp == receiverStatus->status.applications.end())
    {
        return session;  // hasApp == false: receiver is idle.
    }

    session.hasApp = true;
    session.transportId = activeApp->transportId;
    session.appDisplayName = activeApp->displayName;

    if (session.transportId.empty())
    {
        return session;
    }

    if (!impl->Send(session.transportId, kConnectionNamespace, R"({"type":"CONNECT"})") ||
        !impl->Send(session.transportId, kMediaNamespace, R"({"type":"GET_STATUS","requestId":2})"))
    {
        return session;
    }

    auto mediaReply = ReceiveOnNamespace(impl->connection, kMediaNamespace, deadline);
    if (!mediaReply)
    {
        return session;  // Active, but no media status came back in time.
    }

    auto mediaStatus = ParseJsonLenient<MediaStatusMessageDto>(mediaReply->payloadUtf8);
    if (!mediaStatus || mediaStatus->status.empty())
    {
        return session;
    }

    const auto& mediaEntry = mediaStatus->status.front();
    session.hasMedia = true;
    session.mediaSessionId = mediaEntry.mediaSessionId;
    session.playerState = mediaEntry.playerState;
    session.idleReason = mediaEntry.idleReason;
    session.mediaTitle = mediaEntry.media.metadata.title;
    session.contentUrl = mediaEntry.media.contentId;
    session.currentTime = mediaEntry.currentTime;
    session.duration = mediaEntry.media.duration;

    return session;
}

std::optional<std::string> CastChannel::LaunchApp(
    const std::string& appId, std::chrono::steady_clock::time_point deadline)
{
    if (!impl->Send(kReceiverId, kConnectionNamespace, R"({"type":"CONNECT"})"))
    {
        return std::nullopt;
    }

    const std::string launchPayload =
        std::format(R"({{"type":"LAUNCH","appId":"{}","requestId":1}})", appId);

    if (!impl->Send(kReceiverId, kReceiverNamespace, launchPayload))
    {
        return std::nullopt;
    }

    // A cold app launch can take a few seconds, and the first RECEIVER_STATUS
    // reply may land before the app actually shows up in it - keep reading
    // until one includes it (or the deadline passes), rather than trusting
    // the first reply.
    std::string transportId;

    while (std::chrono::steady_clock::now() < deadline)
    {
        auto receiverReply = ReceiveOnNamespace(impl->connection, kReceiverNamespace, deadline);
        if (!receiverReply)
        {
            return std::nullopt;
        }

        auto receiverStatus = ParseJsonLenient<ReceiverStatusMessageDto>(receiverReply->payloadUtf8);
        if (!receiverStatus)
        {
            continue;
        }

        const auto app = std::find_if(receiverStatus->status.applications.begin(),
            receiverStatus->status.applications.end(), [&](const CastApplicationDto& a)
            { return a.appId == appId && !a.transportId.empty(); });

        if (app != receiverStatus->status.applications.end())
        {
            transportId = app->transportId;
            break;
        }
    }

    if (transportId.empty())
    {
        Log::Warning(LogCategory::Cast, "<- {}: {} didn't start in time", impl->device, appId);
        return std::nullopt;
    }

    if (!impl->Send(transportId, kConnectionNamespace, R"({"type":"CONNECT"})"))
    {
        return std::nullopt;
    }

    return transportId;
}

std::optional<std::string> CastChannel::SendMediaCommand(
    const std::string& transportId, const std::string& jsonPayload, std::chrono::steady_clock::time_point deadline)
{
    if (!impl->Send(transportId, kMediaNamespace, jsonPayload))
    {
        return std::nullopt;
    }

    auto reply = ReceiveOnNamespace(impl->connection, kMediaNamespace, deadline);
    if (!reply)
    {
        Log::Warning(LogCategory::Cast, "<- {}: no reply to {}", impl->device, MessageType(jsonPayload));
        return std::nullopt;
    }

    Log::Debug(LogCategory::Cast, "<- {}: {}", impl->device, reply->payloadUtf8);
    return reply->payloadUtf8;
}

}  // namespace mango
