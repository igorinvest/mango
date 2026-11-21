#include "StreamTarget.hpp"

#include "../../../log/Log.hpp"

StreamTarget StreamTargets::Resolve(PlaylistType playlist, const std::string& deviceKey)
{
    StreamTarget target{.playlist = playlist,
        .profile = StreamProfiles::Get(playlist),
        .preset = EncodingPresetSetting::Get(),
        .mode = VideoTranscodeModeSetting::Get(),
        .encoding = std::nullopt};

    if (deviceKey.empty())
    {
        return target;
    }

    if (const auto device = DeviceProfiles::Get(deviceKey))
    {
        Apply(target, *device);
        Log::Info(LogCategory::Stream, "{} ({}): preset {}", deviceKey, device->name,
            EncodingPresetSetting::ToString(target.preset));
    }
    return target;
}

void StreamTargets::Apply(StreamTarget& target, const DeviceProfile& device)
{
    if (device.preset)
    {
        target.preset = EncodingPresetSetting::FromString(*device.preset).value_or(target.preset);
    }
    if (device.videoCapabilities)
    {
        target.profile.videoCapabilities = *device.videoCapabilities;
    }
    if (device.audioCapabilities)
    {
        target.profile.audioCapabilities = *device.audioCapabilities;
    }
    if (device.rotationSupported)
    {
        target.profile.rotationSupported = *device.rotationSupported;
    }
}
