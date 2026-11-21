#include "TrackPreferencesStore.hpp"

#include <algorithm>

#include "../log/Log.hpp"
#include "Languages.hpp"
#include "Settings.hpp"

namespace
{
constexpr auto kAudioLanguagesKey = "audioLanguages";
constexpr auto kSubtitleModeKey = "subtitleMode";
constexpr auto kSubtitleLanguagesKey = "subtitleLanguages";

SubtitleMode ReadSubtitleMode()
{
    const auto stored = Settings::GetStringSetting(kSubtitleModeKey);
    const auto mode = stored ? SubtitleModes::Parse(*stored) : std::nullopt;
    if (stored && !mode)
    {
        Log::Warning(LogCategory::Settings, "unknown subtitleMode \"{}\" in settings, using foreignAudioOnly", *stored);
    }
    return mode.value_or(SubtitleMode::foreignAudioOnly);
}
}  // namespace

std::vector<std::string> TrackPreferencesStore::Clean(const std::vector<std::string>& codes, const std::string& what)
{
    std::vector<std::string> clean;
    for (const auto& code : codes)
    {
        const auto canonical = Languages::Normalize(code);
        if (!canonical)
        {
            Log::Warning(LogCategory::Settings, "unknown language \"{}\" in {} dropped", code, what);
            continue;
        }
        if (!std::ranges::contains(clean, *canonical))
        {
            clean.push_back(*canonical);
        }
    }
    return clean;
}

TrackPreferences TrackPreferencesStore::Get()
{
    return TrackPreferences{
        .audioLanguages = Clean(Settings::GetStringListSetting(kAudioLanguagesKey), kAudioLanguagesKey),
        .subtitleMode = ReadSubtitleMode(),
        .subtitleLanguages = Clean(Settings::GetStringListSetting(kSubtitleLanguagesKey), kSubtitleLanguagesKey),
    };
}

void TrackPreferencesStore::Set(const TrackPreferences& preferences)
{
    SetAudioLanguages(preferences.audioLanguages);
    SetSubtitleMode(preferences.subtitleMode);
    SetSubtitleLanguages(preferences.subtitleLanguages);
}

void TrackPreferencesStore::SetAudioLanguages(const std::vector<std::string>& codes)
{
    const auto clean = Clean(codes, kAudioLanguagesKey);
    Log::Info(LogCategory::Settings, "audio languages: {}", clean);
    Settings::SetStringListSetting(kAudioLanguagesKey, clean);
}

void TrackPreferencesStore::SetSubtitleMode(SubtitleMode mode)
{
    Log::Info(LogCategory::Settings, "subtitle mode: {}", SubtitleModes::ToString(mode));
    Settings::SetStringSetting(kSubtitleModeKey, std::string(SubtitleModes::ToString(mode)));
}

void TrackPreferencesStore::SetSubtitleLanguages(const std::vector<std::string>& codes)
{
    const auto clean = Clean(codes, kSubtitleLanguagesKey);
    Log::Info(LogCategory::Settings, "subtitle languages: {}", clean);
    Settings::SetStringListSetting(kSubtitleLanguagesKey, clean);
}
