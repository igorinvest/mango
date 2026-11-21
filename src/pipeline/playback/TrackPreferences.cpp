#include "TrackPreferences.hpp"

#include <ranges>

std::vector<std::string> ParseLanguageList(std::string_view commaSeparated)
{
    std::vector<std::string> codes;
    for (const auto part : std::views::split(commaSeparated, ','))
    {
        if (!part.empty())
        {
            codes.emplace_back(part.begin(), part.end());
        }
    }
    return codes;
}

namespace SubtitleModes
{

std::string_view ToString(SubtitleMode mode)
{
    switch (mode)
    {
        case SubtitleMode::off: return "off";
        case SubtitleMode::foreignAudioOnly: return "foreignAudioOnly";
        case SubtitleMode::always: return "always";
        case SubtitleMode::forcedOnly: return "forcedOnly";
    }
    return "foreignAudioOnly";
}

std::optional<SubtitleMode> Parse(std::string_view name)
{
    if (name == "off") return SubtitleMode::off;
    if (name == "foreignAudioOnly" || name == "foreign") return SubtitleMode::foreignAudioOnly;
    if (name == "always") return SubtitleMode::always;
    if (name == "forcedOnly" || name == "forced") return SubtitleMode::forcedOnly;
    return std::nullopt;
}

}  // namespace SubtitleModes
