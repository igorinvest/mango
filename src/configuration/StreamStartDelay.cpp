#include "StreamStartDelay.hpp"

#include <algorithm>

#include "Settings.hpp"

namespace
{
constexpr auto kSettingKey = "streamStartDelayMs";
constexpr auto kReadyPercentKey = "streamStartReadyPercent";
}

std::chrono::milliseconds StreamStartDelaySetting::Get()
{
    const auto setting = Settings::GetIntSetting(kSettingKey);
    if (!setting)
    {
        return defaultDelay;
    }

    return std::clamp(std::chrono::milliseconds(*setting), std::chrono::milliseconds(0), maxDelay);
}

int StreamStartDelaySetting::ReadyPercent()
{
    const auto setting = Settings::GetIntSetting(kReadyPercentKey);
    if (!setting)
    {
        return defaultReadyPercent;
    }

    return std::clamp(*setting, 0, 100);
}
