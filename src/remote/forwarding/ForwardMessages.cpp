#include "ForwardMessages.hpp"

#include <algorithm>
#include <cctype>

#include "../signaling/SignalingMessages.hpp"

namespace mango::remote::forward
{

namespace
{

void AppendBigEndian(std::string& out, std::uint64_t value, int bytes)
{
    for (int shift = (bytes - 1) * 8; shift >= 0; shift -= 8)
    {
        out.push_back(static_cast<char>((value >> shift) & 0xff));
    }
}

std::uint64_t ReadBigEndian(std::string_view bytes)
{
    std::uint64_t value = 0;
    for (const char byte : bytes)
    {
        value = (value << 8) | static_cast<unsigned char>(byte);
    }
    return value;
}

bool IsKnownType(std::uint8_t type)
{
    return type >= static_cast<std::uint8_t>(FrameType::Request) && type <= static_cast<std::uint8_t>(FrameType::Ack);
}

template <typename T>
std::optional<T> ReadJson(const Frame& frame, FrameType expected)
{
    if (frame.type != expected)
    {
        return std::nullopt;
    }
    return signaling::Read<T>(frame.body);
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b)
{
    return std::ranges::equal(a, b, [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

}  // namespace

std::string Encode(FrameType type, std::uint32_t exchangeId, std::string_view body)
{
    std::string frame;
    frame.reserve(kHeaderSize + body.size());
    frame.push_back(static_cast<char>(type));
    AppendBigEndian(frame, exchangeId, 4);
    frame.append(body);
    return frame;
}

std::optional<Frame> Decode(std::string_view bytes)
{
    if (bytes.size() < kHeaderSize || !IsKnownType(static_cast<std::uint8_t>(bytes[0])))
    {
        return std::nullopt;
    }
    return Frame{.type = static_cast<FrameType>(bytes[0]),
        .exchangeId = static_cast<std::uint32_t>(ReadBigEndian(bytes.substr(1, 4))),
        .body = std::string(bytes.substr(kHeaderSize))};
}

std::string EncodeRequest(std::uint32_t exchangeId, const RequestMessage& request)
{
    return Encode(FrameType::Request, exchangeId, signaling::Write(request));
}

std::string EncodeHead(std::uint32_t exchangeId, const HeadMessage& head)
{
    return Encode(FrameType::Head, exchangeId, signaling::Write(head));
}

std::string EncodeError(std::uint32_t exchangeId, const ErrorMessage& error)
{
    return Encode(FrameType::Error, exchangeId, signaling::Write(error));
}

std::string EncodeAck(std::uint32_t exchangeId, std::uint64_t bytesWritten)
{
    std::string body;
    AppendBigEndian(body, bytesWritten, 8);
    return Encode(FrameType::Ack, exchangeId, body);
}

std::optional<RequestMessage> ReadRequest(const Frame& frame)
{
    return ReadJson<RequestMessage>(frame, FrameType::Request);
}

std::optional<HeadMessage> ReadHead(const Frame& frame) { return ReadJson<HeadMessage>(frame, FrameType::Head); }

std::optional<ErrorMessage> ReadError(const Frame& frame) { return ReadJson<ErrorMessage>(frame, FrameType::Error); }

std::optional<std::uint64_t> ReadAck(const Frame& frame)
{
    if (frame.type != FrameType::Ack || frame.body.size() != 8)
    {
        return std::nullopt;
    }
    return ReadBigEndian(frame.body);
}

std::string FindHeader(const Headers& headers, std::string_view name)
{
    const auto found = std::ranges::find_if(headers, [name](const auto& header) {
        return EqualsIgnoreCase(header.first, name);
    });
    return found != headers.end() ? found->second : std::string();
}

}  // namespace mango::remote::forward
