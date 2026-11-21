#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "../Engine.hpp"
#include "httplib.h"

// The media server renderers (Chromecast, AirPlay, later DLNA) fetch streams
// and covers from. GET-only, no control routes: the apps drive the engine
// in-process, and remote control goes through the paired peer channel.
// /remote/<peerId>/... forwards to a paired peer's server (see
// remote/forwarding/RemoteLinkHandler.hpp).
class ServerWrapper
{
public:
    using RemoteHandler = std::function<void(const httplib::Request &, httplib::Response &)>;

    ~ServerWrapper();

    // Handles /remote/<peerId>/...; set before the first Listen. Without
    // one, the route answers 404.
    void SetRemoteHandler(RemoteHandler handler);

    // Listens on ip, at the first free port from the configured one up, on
    // a fresh httplib::Server and its own thread. Already listening on ip:
    // nothing changes. On another address: closes that listener first.
    // Returns "http://<ip>:<port>", or std::nullopt when it can't bind.
    std::optional<std::string> Listen(const std::string &ip);

    // Stops listening and waits for the accept loop to end. No-op when not
    // listening.
    void Close();

    // ip/port are the last Listen's; isRunning is left to the caller
    // (see StreamingSession).
    mango::ServerStatus GetStatus() const;

private:
    void setupRoutes();

    void setupLimits();
    void setupRequestGuard();
    void setupExceptionHandler();
    void setupLogger();
    void setupCorsRoutes();
    void setupStreamRoutes();
    void setupCoverRoute();
    void setupRemoteRoute();

    // Binds m_server and starts its accept loop. Called with m_mutex held.
    bool startListeningLocked(const std::string &ip, int port);
    void closeLocked();

    mutable std::mutex m_mutex;
    std::unique_ptr<httplib::Server> m_server;
    std::thread m_listenThread;
    int m_port = 0;
    std::string m_ip;
    RemoteHandler m_remoteHandler;
};
