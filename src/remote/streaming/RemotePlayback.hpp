#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../../session/SessionThread.hpp"
#include "../../pipeline/stream/profiles/PlaylistType.hpp"
#include "ControlMessages.hpp"

namespace mango::remote
{

// B's remote streams: asks a source peer to start one (initiate, with B's
// target for the TV fitted to the peer's hello), and keeps what each
// started stream needs later — which subtitles may be declared, whether it
// has a cover, how far it was watched, how far the peer's conversion of it
// has got. Progress goes to the peer, which keeps the watch history of its
// own library.
//
// Initiate blocks its caller; the messages are sent and read on the remote
// thread. Everything else is safe from any thread.
class RemotePlayback
{
   public:
    // How long A gets to start a stream (it may extract subtitles first).
    static constexpr std::chrono::seconds kInitiateTimeout{90};

    struct Stream
    {
        std::string peerId;
        std::string fastHash;  // on the peer
        std::string streamId;
        std::vector<int> subtitleTracks;
        bool hasCover = false;
        std::optional<double> lastPosition;
        bool stoppedByUser = false;
        bool endSent = false;
        // The peer's latest transcodeStatus, fastHash set to the entry's
        // id; std::nullopt until the first and while the link is down.
        std::optional<TranscodeStatus> transcode;
    };

    struct Callbacks
    {
        // The peer's hello example (initiateExample JSON); std::nullopt
        // while it isn't connected. Remote thread.
        std::function<std::optional<std::string>(const std::string& peerId)> example;

        // Sends text to the peer; false when its link isn't up. Remote
        // thread.
        std::function<bool(const std::string& peerId, const std::string& text)> send;

        // Fields or values the peer didn't take, once per connection.
        // Remote thread.
        std::function<void(const std::string& peerId, const std::vector<std::string>& items)> onNotApplied;

        // A stream of the peer ended and the peer was told: its library
        // has this play's tracks and resume point now. Remote thread.
        std::function<void(const std::string& peerId)> onEnded;
    };

    RemotePlayback(SessionThread& thread, Callbacks callbacks);
    ~RemotePlayback();

    RemotePlayback(const RemotePlayback&) = delete;
    RemotePlayback& operator=(const RemotePlayback&) = delete;

    // Starts entryId ("remote:<peer>:<fastHash>") on its peer for a TV with
    // tvDeviceKey (empty: B's defaults). Blocks until the peer answers.
    // error: the peer's StreamInitiation error, or "peerOffline",
    // "peerTimeout", "peerLinkLost". tracks goes to the peer as is: only
    // what the user chose, the peer picks the rest.
    [[nodiscard]] InitiateResult Initiate(
        const std::string& entryId, const TrackRequest& tracks, PlaylistType playlist, const std::string& tvDeviceKey);

    // Remote thread.
    void HandleResult(const std::string& peerId, const std::string& text);
    void HandleTranscodeStatus(const std::string& peerId, const std::string& text);
    void OnLinkUp(const std::string& peerId);
    void OnLinkDown(const std::string& peerId);
    void FailAll();

    [[nodiscard]] std::optional<Stream> Find(const std::string& entryId) const;

    // The entry whose stream on peerId is streamId (from a TV's content
    // URL); std::nullopt when it isn't one this device started.
    [[nodiscard]] std::optional<std::string> FindByStream(const std::string& peerId, const std::string& streamId) const;

    // The TV was loaded with entryId from startSeconds.
    void Started(const std::string& entryId, double startSeconds);
    void ReportProgress(const std::string& entryId, double positionSeconds, int audioTrackId, int subtitleTrackId);
    void NoteStoppedByUser(const std::string& entryId);

    // The TV finished or stopped entryId: tells the peer once per start,
    // then Callbacks::onEnded.
    void Ended(const std::string& entryId);

   private:
    struct Pending
    {
        std::string peerId;
        std::string entryId;
        std::shared_ptr<std::promise<InitiateResult>> result;
    };

    void Send(const std::string& entryId, const Pending& pending, InitiateRequest request, PlaylistType playlist,
        const std::string& tvDeviceKey);
    void Fail(std::uint32_t requestId, const std::string& error);
    void ForgetTranscodes(const std::string& peerId);
    void Remember(const std::string& entryId, const std::string& peerId, const InitiateResult& result);
    void SendToPeer(const std::string& peerId, std::string text);

    SessionThread& m_thread;
    Callbacks m_callbacks;

    // Remote thread only.
    std::map<std::uint32_t, Pending> m_pending;
    std::uint32_t m_nextId = 1;
    std::set<std::string> m_warned;

    mutable std::mutex m_mutex;
    std::map<std::string, Stream> m_streams;

    // Posted tasks hold it weakly: dropped once this is gone.
    std::shared_ptr<int> m_lifetime = std::make_shared<int>(0);
};

}  // namespace mango::remote
