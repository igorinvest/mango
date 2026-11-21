#include "ChannelTransport.hpp"

#include <rtc/rtc.hpp>

#include "../../log/Log.hpp"

namespace mango::remote
{

ChannelTransport::ChannelTransport(std::shared_ptr<rtc::DataChannel> channel) : m_channel(std::move(channel)) {}

bool ChannelTransport::Send(std::string frame)
{
    std::lock_guard lock(m_mutex);
    if (!m_channel || !m_channel->isOpen())
    {
        return false;
    }

    try
    {
        const auto* bytes = reinterpret_cast<const std::byte*>(frame.data());
        return m_channel->send(rtc::binary(bytes, bytes + frame.size())) || m_channel->isOpen();
    }
    catch (const std::exception& error)
    {
        Log::Warning(LogCategory::Remote, "forwarding send failed: {}", error.what());
        return false;
    }
}

std::size_t ChannelTransport::BufferedAmount() const
{
    std::lock_guard lock(m_mutex);
    return m_channel ? m_channel->bufferedAmount() : 0;
}

bool ChannelTransport::IsOpen() const
{
    std::lock_guard lock(m_mutex);
    return m_channel && m_channel->isOpen();
}

void ChannelTransport::Detach()
{
    std::lock_guard lock(m_mutex);
    m_channel.reset();
}

}  // namespace mango::remote
