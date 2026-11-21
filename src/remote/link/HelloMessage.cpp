#include "HelloMessage.hpp"

#include "../../configuration/EncodingPreset.hpp"
#include "../../configuration/VideoTranscodeMode.hpp"
#include "../../pipeline/stream/profiles/SupportedFormats.hpp"
#include "../signaling/SignalingMessages.hpp"

namespace mango::remote::Hello
{

namespace
{

constexpr auto kDefaultPreset = EncodingPresetId::FullHD1080;

InitiateTarget BuiltInTarget()
{
    const auto& profile = StreamProfiles::BuiltInProfile(PlaylistType::DASH);
    const auto& encoding = StreamProfiles::BuiltInEncodingDefaults();
    const auto& preset = encoding.Preset(kDefaultPreset);

    return {.playlist = "dash",
        .preset = EncodingPresetSetting::ToString(kDefaultPreset),
        .mode = VideoTranscodeModeSetting::ToString(VideoTranscodeMode::Automatic),
        .videoCapabilities = profile.videoCapabilities,
        .audioCapabilities = profile.audioCapabilities,
        .rotationSupported = profile.rotationSupported,
        .encoding = {.videoBitrate = preset.videoBitrate,
            .audioBitrate = preset.audioBitrate,
            .videoPixFmt = encoding.videoPixFmt,
            .audioCodec = encoding.audioCodec}};
}

InitiateAccepts BuiltInAccepts()
{
    return {.playlist = {"dash", "hls"},
        .preset = {EncodingPresetSetting::ToString(EncodingPresetId::HD720),
            EncodingPresetSetting::ToString(EncodingPresetId::FullHD1080),
            EncodingPresetSetting::ToString(EncodingPresetId::UHD4K)},
        .mode = {VideoTranscodeModeSetting::ToString(VideoTranscodeMode::Automatic),
            VideoTranscodeModeSetting::ToString(VideoTranscodeMode::ForceCopy),
            VideoTranscodeModeSetting::ToString(VideoTranscodeMode::ForceDecode)},
        .videoCodecs = SupportedFormats::kVideoCodecs,
        .audioCodecs = SupportedFormats::kAudioCodecs,
        .videoPixFmt = {"yuv420p", "yuv420p10le"},
        .audioCodec = {"aac"}};
}

}  // namespace

InitiateExample BuiltInInitiateExample() { return {BuiltInTarget(), BuiltInAccepts()}; }

std::string Make(const std::string& appVersion)
{
    return signaling::Write(HelloMessage{.appVersion = appVersion, .initiateExample = BuiltInInitiateExample()});
}

std::optional<PeerHello> Read(const std::string& text)
{
    const auto type = signaling::Read<signaling::Envelope>(text);
    if (!type || type->type != "hello")
    {
        return std::nullopt;
    }
    return signaling::Read<PeerHello>(text);
}

Compatibility Check(int peerProtocol)
{
    if (peerProtocol < kProtocolVersion)
    {
        return Compatibility::PeerNeedsUpdate;
    }
    return peerProtocol > kProtocolVersion ? Compatibility::UpdateRequired : Compatibility::Compatible;
}

}  // namespace mango::remote::Hello
