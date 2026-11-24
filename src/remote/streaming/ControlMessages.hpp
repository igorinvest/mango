#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glaze/glaze.hpp>

#include "../../Engine.hpp"

// The control messages B and A exchange over the data channel after hello
// (step 8 of docs/scope/hybrid_streaming_architecture.md). Requests to A
// are built from what A's hello describes; A's answers are read ignoring
// unknown keys, so a newer A can add fields. Every key is spelled out in
// glz::meta at the end of this file: fields are only ever added. A request's
// number is "requestId": "id" is a string in every envelope (see
// signaling::Envelope), which reads each message first.
namespace mango::remote
{

// B → A: start (or reuse) a stream. target is built from A's example (see
// RemoteTarget::FitToExample); A rejects a request without one. tvDeviceKey
// is the TV's key on B ("chromecast:<id>", or empty), for A's log and
// later use: A never picks settings by it. audioTrackId/subtitleTrackId are
// only the tracks B's user chose (subtitles -1: off); an absent one A picks
// itself, from its own preferences and memory (see TrackRequest).
struct InitiateRequest
{
    std::string type = "initiate";
    std::uint32_t requestId = 0;
    std::string fastHash;
    std::optional<int> audioTrackId;
    std::optional<int> subtitleTrackId;
    std::string tvDeviceKey;
    glz::raw_json target;
};

// A → B: the answer to initiate requestId. Addresses are relative to A's
// /stream/, as in StreamInitiation. subtitleTracks are the text tracks A
// can serve, the ones B may declare to the TV; hasCover tells whether
// /cover/<streamId> has an image. audioTrackId/subtitleTrackId are the
// tracks the stream plays (-1: none), chosen or picked by A.
struct InitiateResult
{
    std::string type = "initiateResult";
    std::uint32_t requestId = 0;
    bool success = false;
    std::string error;
    std::string streamId;
    std::string streamAddress;
    std::string subtitleAddress;
    bool startedNewTranscode = false;
    std::vector<int> subtitleTracks;
    bool hasCover = false;
    int audioTrackId = -1;
    int subtitleTrackId = -1;
};

// B → A: the TV was told to play fastHash from startSeconds.
struct PlaybackStart
{
    std::string type = "playbackStart";
    std::string fastHash;
    double startSeconds = 0.0;
};

// B → A: how far the TV got, every status poll.
struct PlaybackProgress
{
    std::string type = "playbackProgress";
    std::string fastHash;
    double positionSeconds = 0.0;
    int audioTrackId = -1;
    int subtitleTrackId = -1;
};

// B → A: the TV finished or stopped fastHash: A's server may close.
struct PlaybackEnded
{
    std::string type = "playbackEnded";
    std::string fastHash;
};

// A → B: how far A's conversion of the stream B started has got, right
// after initiateResult and then every second while that file is A's
// current conversion — also across a reconnect, so B never asks. It goes
// on after playbackEnded until the conversion finishes or fails. percent
// -1 with error (StreamInitiation's values) when it failed.
struct TranscodeStatusMessage
{
    std::string type = "transcodeStatus";
    std::string fastHash;
    double percent = -1.0;
    std::string error;
};

// A's /library, fetched over the forwarding protocol: its entries without
// local paths, and the fastHashes with a cover at /library-cover/<fastHash>.
struct RemoteLibrary
{
    std::vector<LibraryEntry> entries;
    std::vector<std::string> covers;
};

}  // namespace mango::remote

template <>
struct glz::meta<mango::remote::InitiateRequest>
{
    using T = mango::remote::InitiateRequest;
    static constexpr auto value = object("type", &T::type, "requestId", &T::requestId, "fastHash", &T::fastHash, "audioTrackId",
        &T::audioTrackId, "subtitleTrackId", &T::subtitleTrackId, "tvDeviceKey", &T::tvDeviceKey, "target",
        &T::target);
};

template <>
struct glz::meta<mango::remote::InitiateResult>
{
    using T = mango::remote::InitiateResult;
    static constexpr auto value = object("type", &T::type, "requestId", &T::requestId, "success", &T::success, "error",
        &T::error, "streamId", &T::streamId, "streamAddress", &T::streamAddress, "subtitleAddress",
        &T::subtitleAddress, "startedNewTranscode", &T::startedNewTranscode, "subtitleTracks", &T::subtitleTracks,
        "hasCover", &T::hasCover, "audioTrackId", &T::audioTrackId, "subtitleTrackId", &T::subtitleTrackId);
};

template <>
struct glz::meta<mango::remote::PlaybackStart>
{
    using T = mango::remote::PlaybackStart;
    static constexpr auto value =
        object("type", &T::type, "fastHash", &T::fastHash, "startSeconds", &T::startSeconds);
};

template <>
struct glz::meta<mango::remote::PlaybackProgress>
{
    using T = mango::remote::PlaybackProgress;
    static constexpr auto value = object("type", &T::type, "fastHash", &T::fastHash, "positionSeconds",
        &T::positionSeconds, "audioTrackId", &T::audioTrackId, "subtitleTrackId", &T::subtitleTrackId);
};

template <>
struct glz::meta<mango::remote::PlaybackEnded>
{
    using T = mango::remote::PlaybackEnded;
    static constexpr auto value = object("type", &T::type, "fastHash", &T::fastHash);
};

template <>
struct glz::meta<mango::remote::TranscodeStatusMessage>
{
    using T = mango::remote::TranscodeStatusMessage;
    static constexpr auto value =
        object("type", &T::type, "fastHash", &T::fastHash, "percent", &T::percent, "error", &T::error);
};

template <>
struct glz::meta<mango::remote::RemoteLibrary>
{
    using T = mango::remote::RemoteLibrary;
    static constexpr auto value = object("entries", &T::entries, "covers", &T::covers);
};
