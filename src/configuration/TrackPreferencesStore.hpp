#pragma once

#include <string>
#include <vector>

#include "../pipeline/playback/TrackPreferences.hpp"

// Reads and writes TrackPreferences in settings.json: "audioLanguages"
// (array), "subtitleMode" (string) and "subtitleLanguages" (array). Codes
// are normalized (see Languages::Normalize); unknown ones are dropped and
// logged, duplicates removed. Only the user's own commands write them.
class TrackPreferencesStore
{
   public:
    [[nodiscard]] static TrackPreferences Get();
    static void Set(const TrackPreferences& preferences);

    static void SetAudioLanguages(const std::vector<std::string>& codes);
    static void SetSubtitleMode(SubtitleMode mode);
    static void SetSubtitleLanguages(const std::vector<std::string>& codes);

    // codes as canonical codes, best first: unknown ones dropped (and
    // logged as coming from what), duplicates removed.
    [[nodiscard]] static std::vector<std::string> Clean(const std::vector<std::string>& codes, const std::string& what);
};
