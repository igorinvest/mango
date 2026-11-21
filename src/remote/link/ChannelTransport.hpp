#pragma once

#include <memory>
#include <mutex>

#include "LinkTransport.hpp"

namespace rtc
{
class DataChannel;
}

namespace mango::remote
{

// Binary messages over a PeerLink's data channel, from any thread. The
// link attaches its channel once the handshake is done and detaches it when
// it closes; from then on every Send fails, so threads still holding this
// stop on their own.
class ChannelTransport final : public LinkTransport
{
   public:
    explicit ChannelTransport(std::shared_ptr<rtc::DataChannel> channel);

    bool Send(std::string frame) override;
    [[nodiscard]] std::size_t BufferedAmount() const override;
    [[nodiscard]] bool IsOpen() const override;

    void Detach();

   private:
    mutable std::mutex m_mutex;
    std::shared_ptr<rtc::DataChannel> m_channel;
};

}  // namespace mango::remote
