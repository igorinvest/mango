#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "../Engine.hpp"
#include "ServerWrapper.hpp"
#include "SessionPolicy.hpp"

// By default the HTTP port is open only while something streams: a stream
// start opens it on the current LAN address, and it closes when playback
// ends, after a long idle, or when the engine stops (see SessionPolicy for
// the rule). So nothing listens on a network the user isn't casting on.
// With "listen only while streaming" off (see ListenModeSetting) it stays
// open while enabled, and follows the LAN address when it changes.
class StreamingSession
{
public:
    ~StreamingSession();

    // Engine::Start: streams can be started from now on. Starts the
    // watchdog; listens right away only when listenOnlyWhileStreaming is
    // off.
    void Enable(bool listenOnlyWhileStreaming);

    // Engine::Stop: closes the listener and stops the watchdog.
    void Disable();

    // A stream is about to be handed out: listens on the current LAN address
    // (see NetworkUtils::GetLanIPAddress) and returns the
    // "http://<ip>:<port>" its URLs start with. std::nullopt when disabled
    // or the address can't be bound.
    std::optional<std::string> Open();

    // The setting changed. Turned off: listens now. Turned on: the session
    // closes like after a playback end — once nothing is fetched for a
    // while, so a TV playing right now isn't cut off.
    void SetListenOnlyWhileStreaming(bool enabled);

    // A player said this server's playback ended.
    void NotePlaybackEnded();

    // A TV status poll shows this server's current stream still playing,
    // paused or buffering.
    void NoteStillPlaying();

    mango::ServerStatus GetStatus() const;

    // See ServerWrapper::SetRemoteHandler. Before Enable.
    void SetRemoteHandler(ServerWrapper::RemoteHandler handler);

private:
    // How often the watchdog checks SessionPolicy::ShouldClose.
    static constexpr auto CheckInterval = std::chrono::seconds(5);

    void WatchUntilDisabled();
    void CloseIfDue();

    // Listens on the current LAN address. Called with m_mutex held.
    std::optional<std::string> ListenLocked();

    ServerWrapper m_server;

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    SessionPolicy m_policy;
    bool m_enabled = false;
    bool m_open = false;
    bool m_listenOnlyWhileStreaming = true;
    std::thread m_watchdog;
};
