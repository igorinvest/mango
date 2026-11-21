#pragma once

#include "../../../configuration/EncodingPreset.hpp"
#include "../../../configuration/VideoTranscodeMode.hpp"
#include <optional>
#include <string>

#include "../profiles/DeviceProfiles.hpp"
#include "../profiles/StreamProfile.hpp"

// Everything a stream start decides from, resolved once at its start (see
// StreamPipeline::InitiateStream) so a settings change halfway through a
// start can't mix old and new values. Encoding defaults (bitrates, pixel
// format) are part of it only for a remote stream, where the peer that
// owns the TV sends them (see remote/streaming/RemoteTarget.hpp); a local stream
// leaves encoding empty and StreamTargetResolver reads this Mango's own.
//
// Also what stream reuse compares: a request with a different target
// restarts the conversion, like a different file does.
struct StreamTarget
{
    PlaylistType playlist{PlaylistType::DASH};

    // Per-playlist settings plus the TV's capabilities.
    StreamProfile profile;

    EncodingPresetId preset{EncodingPresetId::FullHD1080};
    VideoTranscodeMode mode{VideoTranscodeMode::Automatic};

    // Set for a remote stream: used instead of encoding-defaults.json.
    std::optional<EncodingDefaults> encoding;

    bool operator==(const StreamTarget&) const = default;
};

class StreamTargets
{
   public:
    // The playlist's profile plus the global preset and transcode mode,
    // with the TV's own settings from device-profiles.json on top when
    // deviceKey (see DeviceProfiles::ChromecastKey) has an entry. An empty
    // key, or one without an entry, uses the defaults.
    [[nodiscard]] static StreamTarget Resolve(PlaylistType playlist, const std::string& deviceKey = {});

    // Overrides target with every setting device sets. Mode stays global.
    static void Apply(StreamTarget& target, const DeviceProfile& device);
};
