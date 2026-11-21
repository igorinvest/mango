#include "AutoplaySetting.hpp"

#include "Settings.hpp"
#include "../log/Log.hpp"

namespace
{
constexpr auto kSettingKey = "autoplayNextInFolder";
}

bool AutoplaySetting::Get() { return Settings::GetBoolSetting(kSettingKey).value_or(defaultValue); }

void AutoplaySetting::Set(bool enabled)
{
    Log::Info(LogCategory::Playlist, "autoplay next in folder {}", enabled ? "on" : "off");
    Settings::SetBoolSetting(kSettingKey, enabled);
}
