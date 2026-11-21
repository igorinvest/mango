#pragma once
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../../configuration/EncodingPreset.hpp"
#include "PlaylistType.hpp"

struct VideoCodecConfig
{
    std::string codec;
    int maxWidth{0};
    int maxHeight{0};
    int maxFps{0};
    bool hdrSupported{false};  // Новый параметр: поддержка HDR

    bool operator==(const VideoCodecConfig&) const = default;
};

struct AudioCodecConfig
{
    std::string codec;
    int maxChannels{0};
    int maxSampleRate{0};

    bool operator==(const AudioCodecConfig&) const = default;
};

struct SubtitleCodecConfig
{
    std::string codec;

    bool operator==(const SubtitleCodecConfig&) const = default;
};

struct StreamProfile
{
    PlaylistType playlist{PlaylistType::DASH};
    std::vector<VideoCodecConfig> videoCapabilities;
    std::vector<AudioCodecConfig> audioCapabilities;
    std::vector<SubtitleCodecConfig> subtitleCapabilities;

    // Новые затребованные параметры
    std::string defaultFileName{"stream"};
    std::string defaultExtension{".mp4"};

    // Дополнительные параметры для расширения функционала JSON
    bool encryptionRequired{false};   // Требуется ли DRM/шифрование
    int segmentDurationSec{0};        // Длина сегмента в секундах
    double maxBufferLengthSec{30.0};  // Максимальный буфер плеера

    // DASH cannot mux subtitles into the manifest correctly, so subtitles for
    // this profile are extracted into a standalone sidecar file instead of
    // being embedded into the main conversion output.
    bool sidecarSubtitles{false};

    // Whether the TV shows a copied video upright when the source carries a
    // display-matrix rotation (phone recordings, see VideoTrack::rotation).
    // Off by default: with it off, Automatic converts rotated video instead
    // of copying it, since a sideways picture is worse than a converted one.
    bool rotationSupported{false};

    bool operator==(const StreamProfile&) const = default;
};

// One rung of the bitrate ladder (see encoding-defaults.json's "presets"):
// the largest output it allows, compared long side to long side (so a
// portrait source uses the box turned upright), and the bitrates an output
// of that size is encoded at.
struct EncodingPreset
{
    int maxWidth{0};
    int maxHeight{0};
    std::string videoBitrate;
    std::string audioBitrate;

    bool operator==(const EncodingPreset&) const = default;
};

// ffmpeg encode parameters StreamTargetResolver applies to a track it
// decided must be transcoded rather than copied (see
// StreamTargetResolver::CanCopyVideo/CanCopyAudio). The encoder itself (e.g.
// which hardware H.264 encoder to use) is still probed dynamically by
// the MediaBackend (see MediaBackend::VideoEncoder) and isn't part of this
// user-configurable set.
struct EncodingDefaults
{
    // Keyed by EncodingPresetSetting::ToString ("720p", "1080p", "4k");
    // every one of those keys is present (StreamProfiles rejects a file
    // missing one).
    std::unordered_map<std::string, EncodingPreset> presets;
    std::string videoPixFmt{"yuv420p"};
    std::string audioCodec{"aac"};

    [[nodiscard]] const EncodingPreset& Preset(EncodingPresetId presetId) const;

    bool operator==(const EncodingDefaults&) const = default;
};

// Both StreamProfile (per-playlist-type capabilities) and EncodingDefaults
// are user-configurable, in stream-profiles.json/encoding-defaults.json
// (under File::getApplicationDataPath()). They're read from disk on every
// call and never cached, so a change - from the writers below or a hand
// edit - applies to the next stream that starts, without a restart.
//
// A missing file is created from the in-code defaults. An invalid one (bad
// syntax, an unknown key, an old format) is renamed to "<name>.invalid" -
// so the user's edits survive - and replaced with the defaults.
class StreamProfiles
{
   public:
    [[nodiscard]] static StreamProfile Get(PlaylistType type);
    [[nodiscard]] static std::unordered_map<PlaylistType, StreamProfile> GetAll();
    [[nodiscard]] static EncodingDefaults GetEncodingDefaults();

    // The in-code encoding-defaults.json - its preset sizes are what the
    // Supported Formats writers below use for "720p"/"1080p"/"4k".
    [[nodiscard]] static const EncodingDefaults& BuiltInEncodingDefaults();

    // The in-code profile for type, never the user's file - what remote
    // streaming's hello describes (see remote/link/HelloMessage.hpp).
    [[nodiscard]] static const StreamProfile& BuiltInProfile(PlaylistType type);

    // Replaces every videoCapabilities entry for codec with its built-in
    // entries for that resolution - std::nullopt ("Not supported") just
    // removes them. Hand-edited fps limits for that codec are lost.
    static void SetVideoCodecMaxResolution(
        PlaylistType type, const std::string& codec, std::optional<EncodingPresetId> preset);

    // Adds codec (6 channels, 48 kHz) to audioCapabilities, or removes it.
    static void SetAudioCodecSupported(PlaylistType type, const std::string& codec, bool supported);

    static void SetRotationSupported(PlaylistType type, bool supported);

    // Writes the built-in profile for type over the user's one.
    static void RestoreDefaults(PlaylistType type);

   private:
    using ProfileMap = std::unordered_map<PlaylistType, StreamProfile>;

    // Guards every read and write of both files, so a stream start never
    // reads a file the UI is halfway through saving (and resets it as
    // invalid).
    static std::mutex s_mutex;

    [[nodiscard]] static ProfileMap LoadProfilesLocked();
    static void SaveProfilesLocked(const ProfileMap& profiles);

    template <typename Edit>
    static void EditProfileLocked(PlaylistType type, Edit&& edit);
};
