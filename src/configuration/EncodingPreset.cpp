#include "EncodingPreset.hpp"

#include "Settings.hpp"
#include "../log/Log.hpp"

namespace
{
constexpr auto kSettingKey = "encodingPreset";
constexpr auto kDefault = EncodingPresetId::FullHD1080;
}  // namespace

EncodingPresetId EncodingPresetSetting::Get()
{
    const auto setting = Settings::GetStringSetting(kSettingKey);
    if (!setting)
    {
        return kDefault;
    }

    return FromString(*setting).value_or(kDefault);
}

void EncodingPresetSetting::Set(EncodingPresetId preset)
{
    // Logged here so every caller is covered alike.
    Log::Info(LogCategory::Settings, "encoding preset {} -> {}", ToString(Get()), ToString(preset));
    Settings::SetStringSetting(kSettingKey, ToString(preset));
}

std::string EncodingPresetSetting::ToString(EncodingPresetId preset)
{
    switch (preset)
    {
        case EncodingPresetId::HD720:
            return "720p";
        case EncodingPresetId::UHD4K:
            return "4k";
        case EncodingPresetId::FullHD1080:
        default:
            return "1080p";
    }
}

std::optional<EncodingPresetId> EncodingPresetSetting::FromString(const std::string& value)
{
    for (const auto preset : kAll)
    {
        if (value == ToString(preset))
        {
            return preset;
        }
    }
    return std::nullopt;
}
