#include "VideoTranscodeMode.hpp"

#include "Settings.hpp"
#include "../log/Log.hpp"

namespace
{
constexpr auto kSettingKey = "videoTranscodeMode";
}

VideoTranscodeMode VideoTranscodeModeSetting::Get()
{
    const auto setting = Settings::GetStringSetting(kSettingKey);
    if (!setting)
    {
        return VideoTranscodeMode::Automatic;
    }

    return FromString(*setting).value_or(VideoTranscodeMode::Automatic);
}

void VideoTranscodeModeSetting::Set(VideoTranscodeMode mode)
{
    // Logged here so every caller is covered alike.
    Log::Info(LogCategory::Settings, "video transcode mode {} -> {}", ToString(Get()), ToString(mode));
    Settings::SetStringSetting(kSettingKey, ToString(mode));
}

std::string VideoTranscodeModeSetting::ToString(VideoTranscodeMode mode)
{
    switch (mode)
    {
        case VideoTranscodeMode::ForceCopy:
            return "forceCopy";
        case VideoTranscodeMode::ForceDecode:
            return "forceDecode";
        case VideoTranscodeMode::Automatic:
        default:
            return "automatic";
    }
}

std::optional<VideoTranscodeMode> VideoTranscodeModeSetting::FromString(const std::string& value)
{
    if (value == "forceCopy")
    {
        return VideoTranscodeMode::ForceCopy;
    }
    if (value == "forceDecode")
    {
        return VideoTranscodeMode::ForceDecode;
    }
    if (value == "automatic")
    {
        return VideoTranscodeMode::Automatic;
    }
    return std::nullopt;
}
