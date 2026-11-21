#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>

#include "../../session/CastWorker.hpp"
#include "../../session/SessionThread.hpp"
#include "../forwarding/ForwardingAdapter.hpp"
#include "ControlMessages.hpp"
#include "RemoteHost.hpp"

namespace mango::remote
{

// A's side of remote playback: answers its relay peers' control messages
// (initiate, playbackStart, playbackProgress, playbackEnded) by calling the engine
// through RemoteHost, and serves /library and /library-cover/<fastHash> to
// their ForwardingAdapter. The channel's pairing is the only authorization:
// only messages from relay peers get here.
//
// Lives on the remote thread; a stream start runs on its own worker.
class RemoteSource
{
   public:
    struct Callbacks
    {
        // Sends text to peerId's link.
        std::function<void(const std::string& peerId, const std::string& text)> send;

        // "remoteStreamStarted" or "streamStoppedByPeer", with the file's
        // title.
        std::function<void(const std::string& code, const std::string& peerId, const std::string& file)> onNotice;
    };

    RemoteSource(SessionThread& thread, Callbacks callbacks);
    ~RemoteSource();

    RemoteSource(const RemoteSource&) = delete;
    RemoteSource& operator=(const RemoteSource&) = delete;

    // Before the first message. Any thread.
    void SetHost(std::shared_ptr<RemoteHost> host);

    // A message of this type from a relay peer; false when it isn't one
    // this class handles.
    bool HandleMessage(const std::string& peerId, const std::string& type, const std::string& text);

    // For each relay peer's ForwardingAdapter. Safe from any thread.
    [[nodiscard]] ForwardingAdapter::Host AdapterHost();

   private:
    void Initiate(const std::string& peerId, const InitiateRequest& request);
    [[nodiscard]] InitiateResult Start(RemoteHost& host, const InitiateRequest& request);
    void OnStarted(const std::string& peerId, const std::string& fastHash, const std::string& previous,
        const InitiateResult& result);

    // On any thread (the adapter's).
    [[nodiscard]] std::optional<LocalResponse> LocalRoute(const std::string& path);
    [[nodiscard]] LocalResponse LibraryResponse(RemoteHost& host);
    [[nodiscard]] std::optional<LocalResponse> CoverResponse(RemoteHost& host, const std::string& fastHash);

    [[nodiscard]] std::shared_ptr<RemoteHost> Host() const;

    SessionThread& m_thread;
    Callbacks m_callbacks;

    mutable std::mutex m_hostMutex;
    std::shared_ptr<RemoteHost> m_host;

    // Files a peer started: replacing one of them isn't "stopped by peer".
    std::set<std::string> m_remoteFiles;

    // Posted tasks hold it weakly: dropped once this is gone.
    std::shared_ptr<int> m_lifetime = std::make_shared<int>(0);

    CastWorker m_worker{"remote stream start"};
};

}  // namespace mango::remote
