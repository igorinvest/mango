#include "StreamProfile.hpp"

#include <filesystem>
#include <stdexcept>

#include "../../../file/File.hpp"
#include "ProfileFiles.hpp"

// The user-facing shape of stream-profiles.json - a plain "dash"/"hls"
// object rather than a PlaylistType-keyed map, so it's easy to hand-edit and
// doesn't need PlaylistType to have its own JSON representation.
struct StreamProfilesFile
{
    StreamProfile dash;
    StreamProfile hls;
};

// Внутреннее хранилище в виде unordered_map для удобной конвертации в JSON
static const std::unordered_map<PlaylistType, StreamProfile> ProfilesRegistry = {
    {PlaylistType::DASH,
        {.playlist = PlaylistType::DASH,
            // Split because some Chromecasts can't decode 4K60 H.264.
            .videoCapabilities = {{.codec = "h264",
                                      .maxWidth = 3840,
                                      .maxHeight = 2160,
                                      .maxFps = 30,
                                      .hdrSupported = false},
                {.codec = "h264",
                    .maxWidth = 1920,
                    .maxHeight = 1080,
                    .maxFps = 60,
                    .hdrSupported = false},
                // Older 1080p Chromecasts (before Chromecast with Google TV)
                // can't play HEVC - their users set it to Not supported.
                {.codec = "hevc",
                    .maxWidth = 3840,
                    .maxHeight = 2160,
                    .maxFps = 60,
                    .hdrSupported = true},
                {.codec = "vp9",
                    .maxWidth = 3840,
                    .maxHeight = 2160,
                    .maxFps = 60,
                    .hdrSupported = true}},
            .audioCapabilities = {{.codec = "aac", .maxChannels = 6, .maxSampleRate = 48000},
                {.codec = "opus", .maxChannels = 6, .maxSampleRate = 48000}},
            .subtitleCapabilities = {{.codec = "webvtt"}},
            .defaultFileName = "index",
            .defaultExtension = ".mpd",
            .encryptionRequired = false,
            .segmentDurationSec = 4,
            .maxBufferLengthSec = 30.0,
            .sidecarSubtitles = true}},

    {PlaylistType::HLS,
        {.playlist = PlaylistType::HLS,
            .videoCapabilities = {{.codec = "h264",
                                      .maxWidth = 1920,
                                      .maxHeight = 1080,
                                      .maxFps = 60,
                                      .hdrSupported = false},
                {.codec = "hevc",
                    .maxWidth = 3840,
                    .maxHeight = 2160,
                    .maxFps = 60,
                    .hdrSupported = true}},
            .audioCapabilities = {{.codec = "aac", .maxChannels = 2, .maxSampleRate = 48000}},
            .subtitleCapabilities = {{.codec = "webvtt"}},
            .defaultFileName = "index",
            .defaultExtension = ".m3u8",
            .encryptionRequired = true,
            .segmentDurationSec = 6,
            .maxBufferLengthSec = 45.0}}};

static const EncodingDefaults DefaultEncoding = {
    .presets = {{"720p",
                    {.maxWidth = 1280,
                        .maxHeight = 720,
                        .videoBitrate = "3000k",
                        .audioBitrate = "192k"}},
        {"1080p",
            {.maxWidth = 1920, .maxHeight = 1080, .videoBitrate = "6000k", .audioBitrate = "256k"}},
        {"4k",
            {.maxWidth = 3840,
                .maxHeight = 2160,
                .videoBitrate = "16000k",
                .audioBitrate = "256k"}}},
    .videoPixFmt = "yuv420p",
    .audioCodec = "aac"};

std::mutex StreamProfiles::s_mutex;

namespace
{

std::filesystem::path ProfilesPath()
{
    return File::getApplicationDataPath() / "stream-profiles.json";
}

std::filesystem::path EncodingDefaultsPath()
{
    return File::getApplicationDataPath() / "encoding-defaults.json";
}

bool HasEveryPreset(const EncodingDefaults& defaults)
{
    for (const auto presetId : EncodingPresetSetting::kAll)
    {
        const auto it = defaults.presets.find(EncodingPresetSetting::ToString(presetId));
        if (it == defaults.presets.end() || it->second.maxWidth <= 0 || it->second.maxHeight <= 0)
        {
            return false;
        }
    }
    return true;
}

std::vector<VideoCodecConfig> BuiltInVideoEntries(const std::string& codec, EncodingPresetId preset)
{
    const auto& size = DefaultEncoding.Preset(preset);
    const bool hdrSupported = codec != "h264";

    if (codec == "h264" && preset == EncodingPresetId::UHD4K)
    {
        const auto& fullHd = DefaultEncoding.Preset(EncodingPresetId::FullHD1080);
        return {{.codec = codec,
                    .maxWidth = size.maxWidth,
                    .maxHeight = size.maxHeight,
                    .maxFps = 30,
                    .hdrSupported = hdrSupported},
            {.codec = codec,
                .maxWidth = fullHd.maxWidth,
                .maxHeight = fullHd.maxHeight,
                .maxFps = 60,
                .hdrSupported = hdrSupported}};
    }

    return {{.codec = codec,
        .maxWidth = size.maxWidth,
        .maxHeight = size.maxHeight,
        .maxFps = 60,
        .hdrSupported = hdrSupported}};
}

}  // namespace

const EncodingPreset& EncodingDefaults::Preset(EncodingPresetId presetId) const
{
    return presets.at(EncodingPresetSetting::ToString(presetId));
}

StreamProfiles::ProfileMap StreamProfiles::LoadProfilesLocked()
{
    if (auto file = ProfileFiles::ReadValid<StreamProfilesFile>(
            ProfilesPath(), [](const StreamProfilesFile&) { return true; }))
    {
        file->dash.playlist = PlaylistType::DASH;
        file->hls.playlist = PlaylistType::HLS;
        return {{PlaylistType::DASH, file->dash}, {PlaylistType::HLS, file->hls}};
    }

    SaveProfilesLocked(ProfilesRegistry);
    return ProfilesRegistry;
}

void StreamProfiles::SaveProfilesLocked(const ProfileMap& profiles)
{
    ProfileFiles::Write(ProfilesPath(),
        StreamProfilesFile{
            .dash = profiles.at(PlaylistType::DASH), .hls = profiles.at(PlaylistType::HLS)});
}

template <typename Edit>
void StreamProfiles::EditProfileLocked(PlaylistType type, Edit&& edit)
{
    auto profiles = LoadProfilesLocked();
    edit(profiles.at(type));
    SaveProfilesLocked(profiles);
}

StreamProfile StreamProfiles::Get(PlaylistType type)
{
    auto profiles = GetAll();
    if (auto it = profiles.find(type); it != profiles.end())
    {
        return std::move(it->second);
    }
    throw std::out_of_range("Requested stream profile type not found.");
}

std::unordered_map<PlaylistType, StreamProfile> StreamProfiles::GetAll()
{
    std::scoped_lock lock(s_mutex);
    return LoadProfilesLocked();
}

EncodingDefaults StreamProfiles::GetEncodingDefaults()
{
    std::scoped_lock lock(s_mutex);

    if (auto file = ProfileFiles::ReadValid<EncodingDefaults>(EncodingDefaultsPath(), HasEveryPreset))
    {
        return std::move(*file);
    }

    ProfileFiles::Write(EncodingDefaultsPath(), DefaultEncoding);
    return DefaultEncoding;
}

const EncodingDefaults& StreamProfiles::BuiltInEncodingDefaults()
{
    return DefaultEncoding;
}

void StreamProfiles::SetVideoCodecMaxResolution(
    PlaylistType type, const std::string& codec, std::optional<EncodingPresetId> preset)
{
    std::scoped_lock lock(s_mutex);
    EditProfileLocked(type,
        [&](StreamProfile& profile)
        {
            std::erase_if(profile.videoCapabilities,
                [&](const VideoCodecConfig& entry) { return entry.codec == codec; });

            if (preset)
            {
                std::ranges::move(
                    BuiltInVideoEntries(codec, *preset), std::back_inserter(profile.videoCapabilities));
            }
        });
}

void StreamProfiles::SetAudioCodecSupported(
    PlaylistType type, const std::string& codec, bool supported)
{
    std::scoped_lock lock(s_mutex);
    EditProfileLocked(type,
        [&](StreamProfile& profile)
        {
            std::erase_if(profile.audioCapabilities,
                [&](const AudioCodecConfig& entry) { return entry.codec == codec; });

            if (supported)
            {
                profile.audioCapabilities.push_back(
                    {.codec = codec, .maxChannels = 6, .maxSampleRate = 48000});
            }
        });
}

void StreamProfiles::SetRotationSupported(PlaylistType type, bool supported)
{
    std::scoped_lock lock(s_mutex);
    EditProfileLocked(type, [&](StreamProfile& profile) { profile.rotationSupported = supported; });
}

const StreamProfile& StreamProfiles::BuiltInProfile(PlaylistType type) { return ProfilesRegistry.at(type); }

void StreamProfiles::RestoreDefaults(PlaylistType type)
{
    std::scoped_lock lock(s_mutex);
    EditProfileLocked(type, [&](StreamProfile& profile) { profile = ProfilesRegistry.at(type); });
}
