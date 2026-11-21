#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "../../pipeline/stream/target/StreamTarget.hpp"
#include "../link/HelloMessage.hpp"

// The transcoding target of a remote stream (see "Version compatibility" in
// docs/scope/hybrid_streaming_architecture.md and Part 2 of
// docs/scope/device-transcoding-profiles-scope.md). B decides it for its
// TV and fits it to A's example from hello; A reads it strictly and never
// fills a gap from its own settings.
namespace mango::remote::RemoteTarget
{

// B: its resolved target and encoding defaults as an initiate target, with
// the bitrates of the target's preset.
[[nodiscard]] InitiateTarget Describe(const StreamTarget& target, const EncodingDefaults& encoding);

// B: the target JSON to send, and what A couldn't take.
struct Fitted
{
    std::string json;
    // Left-out fields ("hdrSupported"), dropped codecs ("hevc") and
    // replaced values ("4k"), for the "Not applied" warning.
    std::vector<std::string> notApplied;
};

// B: mine fitted to A's example (its hello's initiateExample): every field
// of the example, with mine's value where B knows the field and A accepts
// the value. An example that isn't JSON gives mine as is.
[[nodiscard]] Fitted FitToExample(const InitiateTarget& mine, std::string_view exampleJson);

// A: limits on what it takes from another computer.
inline constexpr std::size_t kMaxCapabilities = 16;
inline constexpr int kMaxWidth = 7680;
inline constexpr int kMaxHeight = 4320;
inline constexpr int kMaxFps = 240;
inline constexpr int kMaxChannels = 8;
inline constexpr int kMaxSampleRate = 192000;
inline constexpr int kMaxBitrateKbps = 200000;

// A: the stream target for targetJson, from the built-in playlist profile
// only. The error names the field that's missing or invalid.
[[nodiscard]] std::expected<StreamTarget, std::string> Read(std::string_view targetJson);

}  // namespace mango::remote::RemoteTarget
