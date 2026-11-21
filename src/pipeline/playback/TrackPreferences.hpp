#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// When subtitles are shown for a track the user didn't pick (see
// TrackSelector).
enum class SubtitleMode
{
    off,               // never, not even forced
    foreignAudioOnly,  // when the chosen audio isn't in a preferred language
    always,            // always, in a preferred language if there is one
    forcedOnly,        // only forced tracks
};

// The user's language rules, set in Settings. Playback never changes them
// (see TrackPreferencesStore).
struct TrackPreferences
{
    std::vector<std::string> audioLanguages;     // canonical codes, best first; empty = the file's default
    SubtitleMode subtitleMode = SubtitleMode::foreignAudioOnly;
    std::vector<std::string> subtitleLanguages;  // canonical codes, best first; empty = audioLanguages

    bool operator==(const TrackPreferences&) const = default;

    // subtitleLanguages, or audioLanguages when it's empty: the languages
    // the user reads.
    [[nodiscard]] const std::vector<std::string>& EffectiveSubtitleLanguages() const
    {
        return subtitleLanguages.empty() ? audioLanguages : subtitleLanguages;
    }
};

// "eng,jpn" → {"eng", "jpn"}: how the native bridges and the CLI pass a
// language list. Empty parts are skipped, so "" is the empty list.
[[nodiscard]] std::vector<std::string> ParseLanguageList(std::string_view commaSeparated);

namespace SubtitleModes
{

// "off", "foreignAudioOnly", "always", "forcedOnly".
[[nodiscard]] std::string_view ToString(SubtitleMode mode);

// The names ToString gives, plus the CLI's short "foreign" and "forced".
// std::nullopt for anything else.
[[nodiscard]] std::optional<SubtitleMode> Parse(std::string_view name);

}  // namespace SubtitleModes
