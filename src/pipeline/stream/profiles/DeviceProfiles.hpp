#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "StreamProfile.hpp"

// One TV's own settings in device-profiles.json. Every setting is optional:
// one that's left out comes from the playlist's profile in
// stream-profiles.json, or the global preset. An entry with only a name -
// what discovery adds - uses the defaults.
struct DeviceProfile
{
    // The TV's name when discovery last saw it, so the user can find it in
    // the file. Not used for streaming.
    std::string name;

    // "720p", "1080p" or "4k" (see EncodingPresetSetting::ToString).
    std::optional<std::string> preset;

    std::optional<std::vector<VideoCodecConfig>> videoCapabilities;
    std::optional<std::vector<AudioCodecConfig>> audioCapabilities;
    std::optional<bool> rotationSupported;

    bool operator==(const DeviceProfile&) const = default;
};

// Keyed by DeviceProfiles::ChromecastKey. std::map keeps the file in a
// stable order.
using DeviceProfileMap = std::map<std::string, DeviceProfile>;

// A TV that discovery found.
struct SeenDevice
{
    std::string key;
    std::string name;
};

// Per-TV settings, edited by hand in device-profiles.json (under
// File::getApplicationDataPath()). Like StreamProfiles, the file is read on
// every call, so an edit applies to the next stream that starts. A missing
// file means no TV has its own settings. An invalid one is renamed to
// "device-profiles.json.invalid" and treated as empty.
class DeviceProfiles
{
   public:
    // "chromecast:<id>", or empty when the TV has no TXT id - such a TV
    // always uses the defaults.
    [[nodiscard]] static std::string ChromecastKey(const std::string& castId);

    // The TV's entry, if the file has one.
    [[nodiscard]] static std::optional<DeviceProfile> Get(const std::string& key);

    // Adds a name-only entry for each TV the file doesn't have yet, and
    // updates the name of one seen under a new name. Writes the file only
    // when something changed.
    static void Remember(const std::vector<SeenDevice>& devices);

    // The pure part of Remember: true when profiles changed.
    static bool RememberIn(DeviceProfileMap& profiles, const SeenDevice& device);

    // Whether every entry's values can be used: a known preset and positive
    // sizes, frame rates, channel counts and sample rates.
    [[nodiscard]] static bool IsValid(const DeviceProfileMap& profiles);

   private:
    // Guards every read and write, so a stream start never reads a file
    // that discovery is halfway through saving.
    static std::mutex s_mutex;

    [[nodiscard]] static DeviceProfileMap LoadLocked();
    static void SaveLocked(const DeviceProfileMap& profiles);
};
