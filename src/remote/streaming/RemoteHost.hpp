#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "../../Engine.hpp"
#include "../../pipeline/stream/target/StreamTarget.hpp"

namespace mango::remote
{

// A started (or reused) stream for a remote peer, with what B needs to
// hand it to the TV.
struct HostedStream
{
    StreamInitiation initiation;
    // The text subtitle tracks this side can serve: the ones B may declare.
    std::vector<int> subtitleTracks;
    bool hasCover = false;
};

// What remote streaming needs from the engine on A, the library side.
// Engine implements it (see EngineRemoteHost); tests use a fake. Calls
// marked blocking are only made off the remote thread.
class RemoteHost
{
   public:
    virtual ~RemoteHost() = default;

    // The library, as a paired peer may see it: no local paths.
    virtual std::vector<LibraryEntry> SharedLibrary() = 0;

    // fastHash's cover image file, if it has one.
    virtual std::optional<std::filesystem::path> CoverFile(const std::string& fastHash) = 0;

    // Starts fastHash with exactly this target, never this side's own
    // settings, and opens the HTTP server it's fetched from. tracks holds
    // what the peer's user chose; this side picks the rest, from its own
    // preferences and memory. Blocking.
    virtual HostedStream StartStream(
        const std::string& fastHash, const TrackRequest& tracks, const StreamTarget& target) = 0;

    // "http://<ip>:<port>" while the HTTP server listens.
    virtual std::optional<std::string> ServerUrl() = 0;

    // The library file the current stream plays; empty when none.
    virtual std::string CurrentStreamFastHash() = 0;

    // How far this side's current conversion has got (see
    // Engine::GetTranscodeStatusJson).
    virtual TranscodeStatus CurrentTranscode() = 0;
    virtual std::string Title(const std::string& fastHash) = 0;

    virtual void PlaybackStarted(const std::string& fastHash, double startSeconds) = 0;

    // Every status poll on the peer's side, playing or paused: also keeps
    // this side's HTTP server open.
    virtual void ReportPlaybackProgress(
        const std::string& fastHash, double positionSeconds, int audioTrackId, int subtitleTrackId) = 0;

    // The peer's TV finished or stopped fastHash.
    virtual void PlaybackEnded(const std::string& fastHash) = 0;
};

}  // namespace mango::remote
