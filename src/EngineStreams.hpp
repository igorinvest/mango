#pragma once

#include <string>
#include <vector>

#include "Engine.hpp"
#include "pipeline/stream/target/StreamTarget.hpp"

// Stream starts and what a receiver may be told about a stream, shared by
// Engine's own calls and remote streaming's host (see EngineRemoteHost).
namespace mango::EngineStreams
{

// Fills payload with the started (or reused) stream for target, or why
// none started, with the tracks it plays. payload.serverUrl is left as it
// is.
void Start(StreamInitiation& payload, const std::string& fastHash, const TrackRequest& tracks,
    const StreamTarget& target);

// Whether fastHash's subtitleTrackIndex can be declared to the receiver.
// The receiver eagerly fetches every declared subtitle track's contentUrl at
// LOAD time, and one 404 among them disables subtitles entirely — so only
// text tracks qualify: the server holds a request for one that isn't
// extracted yet until it is (see SubtitleExtractor::WaitUntilExtracted),
// while a bitmap track never gets a .vtt at all. May convert an external
// file first (well under a second).
[[nodiscard]] bool SubtitleTrackCanBeServed(const std::string& fastHash, int subtitleTrackIndex);

// Every subtitle track of fastHash (built in and external) that
// SubtitleTrackCanBeServed.
[[nodiscard]] std::vector<int> ServableSubtitleTracks(const std::string& fastHash);

// Whether fastHash has an extracted cover.
[[nodiscard]] bool HasCover(const std::string& fastHash);

// The URL of fastHash's extracted cover on the same server as contentUrl
// (".../stream/<streamId>/..." -> ".../cover/<streamId>"), or empty when the
// file has no cover or contentUrl isn't the current stream of fastHash.
[[nodiscard]] std::string CoverUrl(const std::string& contentUrl, const std::string& fastHash);

}  // namespace mango::EngineStreams
