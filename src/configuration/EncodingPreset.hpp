#pragma once

#include <array>
#include <optional>
#include <string>

// Target quality for converted video: caps the output size (and what
// Automatic mode is allowed to copy) and picks the bitrates - see the
// "presets" in encoding-defaults.json (EncodingDefaults) and
// StreamTargetResolver. Global, like VideoTranscodeMode.
enum class EncodingPresetId
{
    HD720,
    FullHD1080,
    UHD4K
};

class EncodingPresetSetting
{
   public:
    // Every preset, smallest first.
    static constexpr std::array<EncodingPresetId, 3> kAll = {
        EncodingPresetId::HD720, EncodingPresetId::FullHD1080, EncodingPresetId::UHD4K};

    static EncodingPresetId Get();
    static void Set(EncodingPresetId preset);

    // "720p", "1080p" or "4k" - also the preset's key in encoding-defaults.json.
    static std::string ToString(EncodingPresetId preset);
    static std::optional<EncodingPresetId> FromString(const std::string& value);
};
