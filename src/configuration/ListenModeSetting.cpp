#include "ListenModeSetting.hpp"

#include "Settings.hpp"
#include "../log/Log.hpp"

namespace
{
constexpr auto kSettingKey = "listenOnlyWhileStreaming";
}

bool ListenModeSetting::GetListenOnlyWhileStreaming()
{
    return Settings::GetBoolSetting(kSettingKey).value_or(defaultValue);
}

void ListenModeSetting::SetListenOnlyWhileStreaming(bool enabled)
{
    Log::Info(LogCategory::Server, "listen only while streaming {}", enabled ? "on" : "off");
    Settings::SetBoolSetting(kSettingKey, enabled);
}
