#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glaze/glaze.hpp>

// The HTTP forwarding protocol between B (the TV side) and A (the library
// side), step 5 of docs/scope/hybrid_streaming_architecture.md. Every frame
// is one binary data channel message:
//
//   [type: 1 byte][exchange ID: 4 bytes, big endian][body]
//
// req, head and error bodies are JSON; a chunk's body is the raw bytes, an
// ack's the number of body bytes B has written to the TV so far (8 bytes,
// big endian). end and cancel have no body. Several requests share the
// channel, told apart by their exchange ID.
namespace mango::remote::forward
{

enum class FrameType : std::uint8_t
{
    Request = 1,  // B → A
    Head = 2,     // A → B: status and headers, before any chunk
    Chunk = 3,    // A → B: body bytes
    End = 4,      // A → B: the body is complete
    Cancel = 5,   // B → A: the TV went away
    Error = 6,    // A → B: no (complete) response
    Ack = 7,      // B → A: body bytes written to the TV
};

inline constexpr std::size_t kHeaderSize = 5;

// Body bytes per chunk: well under the data channel's message size limit.
inline constexpr std::size_t kChunkSize = 32 * 1024;

using Headers = std::vector<std::pair<std::string, std::string>>;

struct RequestMessage
{
    std::string method;
    std::string path;
    Headers headers;
};

struct HeadMessage
{
    int status = 0;
    Headers headers;
};

struct ErrorMessage
{
    // What B answers the TV with: 502 when A's server couldn't be reached,
    // 503 when A is too busy, 404 for a route A doesn't forward.
    int status = 502;
    std::string message;
};

struct Frame
{
    FrameType type = FrameType::Request;
    std::uint32_t exchangeId = 0;
    std::string body;
};

[[nodiscard]] std::string Encode(FrameType type, std::uint32_t exchangeId, std::string_view body = {});

// std::nullopt when bytes is too short or of an unknown type.
[[nodiscard]] std::optional<Frame> Decode(std::string_view bytes);

[[nodiscard]] std::string EncodeRequest(std::uint32_t exchangeId, const RequestMessage& request);
[[nodiscard]] std::string EncodeHead(std::uint32_t exchangeId, const HeadMessage& head);
[[nodiscard]] std::string EncodeError(std::uint32_t exchangeId, const ErrorMessage& error);
[[nodiscard]] std::string EncodeAck(std::uint32_t exchangeId, std::uint64_t bytesWritten);

[[nodiscard]] std::optional<RequestMessage> ReadRequest(const Frame& frame);
[[nodiscard]] std::optional<HeadMessage> ReadHead(const Frame& frame);
[[nodiscard]] std::optional<ErrorMessage> ReadError(const Frame& frame);
[[nodiscard]] std::optional<std::uint64_t> ReadAck(const Frame& frame);

// The value of name in headers, ignoring case; empty when it isn't there.
[[nodiscard]] std::string FindHeader(const Headers& headers, std::string_view name);

}  // namespace mango::remote::forward

template <>
struct glz::meta<mango::remote::forward::RequestMessage>
{
    using T = mango::remote::forward::RequestMessage;
    static constexpr auto value = object("method", &T::method, "path", &T::path, "headers", &T::headers);
};

template <>
struct glz::meta<mango::remote::forward::HeadMessage>
{
    using T = mango::remote::forward::HeadMessage;
    static constexpr auto value = object("status", &T::status, "headers", &T::headers);
};

template <>
struct glz::meta<mango::remote::forward::ErrorMessage>
{
    using T = mango::remote::forward::ErrorMessage;
    static constexpr auto value = object("status", &T::status, "message", &T::message);
};
