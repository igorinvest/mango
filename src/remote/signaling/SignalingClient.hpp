#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "../../session/SessionThread.hpp"
#include "../identity/DeviceIdentity.hpp"

namespace rtc
{
class WebSocket;
}

namespace mango::remote
{

// The one WebSocket to the signaling service (signal/README.md):
// authenticates with the device key, keeps the socket alive with ping, and
// reconnects with backoff. Everything else it receives goes to onMessage.
//
// Lives on thread: every method is called there, and every handler runs
// there.
class SignalingClient
{
   public:
    enum class State
    {
        Stopped,
        Connecting,
        Ready,
    };

    struct Handlers
    {
        std::function<void()> onReady;
        // The socket closed (it reconnects by itself unless stopped).
        std::function<void()> onDisconnected;
        // type is the message's "type"; text the whole message.
        std::function<void(const std::string& type, const std::string& text)> onMessage;
    };

    SignalingClient(const DeviceIdentity& identity, SessionThread& thread, std::string url, Handlers handlers);
    ~SignalingClient();

    SignalingClient(const SignalingClient&) = delete;
    SignalingClient& operator=(const SignalingClient&) = delete;

    // Does nothing (stays Stopped) when the URL is empty.
    void Start();
    void Stop();

    [[nodiscard]] State GetState() const { return m_state; }

    // False when the socket isn't ready; the message is dropped then.
    bool Send(const std::string& json);

    // A fresh requestId for a message whose reply or error must be matched.
    [[nodiscard]] std::string NextRequestId();

   private:
    void Connect();
    void CloseSocket();
    void Watch(const std::shared_ptr<rtc::WebSocket>& socket);

    void OnText(const std::string& text);
    void OnClosed();
    void HandleChallenge(const std::string& text);
    void HandleReady(const std::string& text);

    void SchedulePing();
    void ScheduleReconnect();
    void CancelTimers();

    // Posts task to the thread, dropped if the socket was replaced since.
    void PostForSocket(std::uint64_t generation, std::function<void()> task);

    const DeviceIdentity& m_identity;
    SessionThread& m_thread;
    const std::string m_url;
    Handlers m_handlers;

    std::shared_ptr<rtc::WebSocket> m_socket;
    std::uint64_t m_generation = 0;
    State m_state = State::Stopped;
    bool m_wanted = false;

    std::chrono::seconds m_backoff{1};
    SessionThread::TimerId m_pingTimer = 0;
    SessionThread::TimerId m_reconnectTimer = 0;
    SessionThread::TimerId m_authTimer = 0;
    std::uint64_t m_nextRequestId = 1;
};

}  // namespace mango::remote
