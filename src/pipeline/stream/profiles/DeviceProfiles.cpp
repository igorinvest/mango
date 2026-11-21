#include "DeviceProfiles.hpp"

#include <algorithm>
#include <filesystem>

#include "../../../configuration/EncodingPreset.hpp"
#include "../../../file/File.hpp"
#include "../../../log/Log.hpp"
#include "ProfileFiles.hpp"

std::mutex DeviceProfiles::s_mutex;

namespace
{

std::filesystem::path DeviceProfilesPath() { return File::getApplicationDataPath() / "device-profiles.json"; }

bool IsValidVideo(const VideoCodecConfig& entry)
{
    return !entry.codec.empty() && entry.maxWidth > 0 && entry.maxHeight > 0 && entry.maxFps > 0;
}

bool IsValidAudio(const AudioCodecConfig& entry)
{
    return !entry.codec.empty() && entry.maxChannels > 0 && entry.maxSampleRate > 0;
}

bool IsValidProfile(const DeviceProfile& profile)
{
    if (profile.preset && !EncodingPresetSetting::FromString(*profile.preset))
    {
        return false;
    }
    if (profile.videoCapabilities && !std::ranges::all_of(*profile.videoCapabilities, IsValidVideo))
    {
        return false;
    }
    return !profile.audioCapabilities || std::ranges::all_of(*profile.audioCapabilities, IsValidAudio);
}

}  // namespace

std::string DeviceProfiles::ChromecastKey(const std::string& castId) { return castId.empty() ? "" : "chromecast:" + castId; }

std::optional<DeviceProfile> DeviceProfiles::Get(const std::string& key)
{
    std::scoped_lock lock(s_mutex);
    auto profiles = LoadLocked();
    const auto it = profiles.find(key);
    return it == profiles.end() ? std::nullopt : std::optional(std::move(it->second));
}

void DeviceProfiles::Remember(const std::vector<SeenDevice>& devices)
{
    std::scoped_lock lock(s_mutex);
    auto profiles = LoadLocked();

    bool changed = false;
    for (const auto& device : devices)
    {
        changed = RememberIn(profiles, device) || changed;
    }

    if (changed)
    {
        SaveLocked(profiles);
    }
}

bool DeviceProfiles::RememberIn(DeviceProfileMap& profiles, const SeenDevice& device)
{
    if (device.key.empty())
    {
        return false;
    }

    const auto [it, added] = profiles.try_emplace(device.key);
    if (!added && it->second.name == device.name)
    {
        return false;
    }

    Log::Info(LogCategory::Settings, "device-profiles.json: {} is {}", device.key, device.name);
    it->second.name = device.name;
    return true;
}

bool DeviceProfiles::IsValid(const DeviceProfileMap& profiles)
{
    return std::ranges::all_of(profiles, [](const auto& entry) { return IsValidProfile(entry.second); });
}

DeviceProfileMap DeviceProfiles::LoadLocked()
{
    return ProfileFiles::ReadValid<DeviceProfileMap>(DeviceProfilesPath(), IsValid).value_or(DeviceProfileMap{});
}

void DeviceProfiles::SaveLocked(const DeviceProfileMap& profiles)
{
    ProfileFiles::Write<glz::opts{.prettify = true}>(DeviceProfilesPath(), profiles);
}
