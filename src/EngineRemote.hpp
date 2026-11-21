#pragma once

#include <string>
#include <vector>

#include "Engine.hpp"
#include "chromecast/ChromecastControl.hpp"
#include "pipeline/playback/PlaybackEndDetector.hpp"

class StreamingSession;

namespace mango
{

class RemoteStreaming;

// Engine's calls for a video in a paired peer's library (B, the TV side;
// see LibraryEntry::remotePeerId): the same calls as for a local one, but
// the stream runs on the peer and the TV fetches it through this server's
// /remote/<peerId>/... route.
class EngineRemote
{
   public:
    EngineRemote(StreamingSession& session, RemoteStreaming& remote);

    // fastHash names a remote entry ("remote:...").
    [[nodiscard]] static bool Is(const std::string& fastHash);

    // Starts the stream on the peer; serverUrl ends in /remote/<peerId>, so
    // "<serverUrl>/stream/<streamAddress>" is what the TV plays. tracks
    // holds only what the user chose: the peer picks the rest. Blocking.
    [[nodiscard]] StreamInitiation Initiate(
        const std::string& entryId, const TrackRequest& tracks, PlaylistType playlist, const std::string& deviceKey);

    // Before a LOAD: keeps the subtitle tracks the peer can serve (active
    // -1 when it can't serve that one), tells the peer where playback
    // starts, and returns the cover's URL ("" for none).
    [[nodiscard]] std::string PrepareLoad(const std::string& entryId, const std::string& contentUrl,
        std::vector<CastSubtitleTrackRef>& tracks, int& activeTrack, double startSeconds);

    [[nodiscard]] bool CanServeSubtitle(const std::string& entryId, int subtitleTrackIndex) const;

    void ReportProgress(const std::string& entryId, double positionSeconds, int audioTrackId, int subtitleTrackId);

    // The end-of-playback rule with the entry's duration and the last
    // reported position; an end closes this server's session.
    [[nodiscard]] PlaybackEndKind Classify(const std::string& entryId, const PlayerEnd& end);

    void NoteStoppedByUser(const std::string& entryId);

    // The entry a TV plays from contentUrl (".../remote/<peerId>/stream/
    // <streamId>/..."), "" for anything else.
    [[nodiscard]] std::string EntryForUrl(const std::string& contentUrl) const;

   private:
    StreamingSession& m_session;
    RemoteStreaming& m_remote;
};

}  // namespace mango
