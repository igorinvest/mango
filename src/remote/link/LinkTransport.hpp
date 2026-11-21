#pragma once

#include <cstddef>
#include <string>

namespace mango::remote
{

// Binary messages to one connected peer: the data channel of a PeerLink (see
// ChannelTransport), or an in-process loopback in tests. The forwarding
// protocol (see forwarding/ForwardMessages.hpp) sends its frames through
// it. Every method is safe from any thread.
class LinkTransport
{
   public:
    virtual ~LinkTransport() = default;

    // False once the link is gone: the message was dropped.
    virtual bool Send(std::string message) = 0;

    // Bytes handed to Send that the channel hasn't sent yet.
    [[nodiscard]] virtual std::size_t BufferedAmount() const = 0;

    [[nodiscard]] virtual bool IsOpen() const = 0;
};

}  // namespace mango::remote
