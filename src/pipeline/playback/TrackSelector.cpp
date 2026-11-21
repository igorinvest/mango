#include "TrackSelector.hpp"

#include <algorithm>

#include "../../configuration/Languages.hpp"
#include "../stream/subtitles/SubtitleExtractor.hpp"

StreamTrackSelection TrackSelector::SelectTracks(
    const FileRecord& fileRecord, const TrackRequest& request, const TrackContext& context)
{
    const auto audio = PickAudio(fileRecord, request, context);
    const auto subtitle = PickSubtitle(fileRecord, request, context, AudioLanguage(fileRecord, audio));
    return StreamTrackSelection{.videoTrackId = SelectVideoTrack(fileRecord.mediaInfo),
        .audioTrackId = audio,
        .subtitleTrackId = subtitle == kOff ? std::nullopt : std::optional(subtitle)};
}

std::optional<int> TrackSelector::SelectBuiltInAudioTrack(
    const MediaInfo& mediaInfo, const TrackPreferences& preferences)
{
    return PickAudioByPreferences(BuiltInAudioCandidates(mediaInfo), preferences.audioLanguages);
}

// ---- Video -----------------------------------------------------------------

std::optional<int> TrackSelector::SelectVideoTrack(const MediaInfo& mediaInfo)
{
    if (mediaInfo.videos.empty()) return std::nullopt;

    auto best =
        std::ranges::max_element(mediaInfo.videos, [](const VideoTrack& a, const VideoTrack& b)
            { return ScoreVideoTrack(a) < ScoreVideoTrack(b); });

    return best->index;
}

int TrackSelector::ScoreVideoTrack(const VideoTrack& track)
{
    int score = 0;
    if (track.codec == "h264" || track.codec == "hevc" || track.codec == "av1")
        score += 1000;
    else if (track.codec == "mjpeg")
        score -= 1000;

    score += (track.fps >= 15.0 && track.fps <= 120.0) ? 500 : -500;
    score += (track.width * track.height) / 10000;
    if (track.bitrate > 0) score += static_cast<int>(track.bitrate / 100000);

    return score;
}

// ---- Candidates ------------------------------------------------------------

TrackSelector::Candidates TrackSelector::BuiltInAudioCandidates(const MediaInfo& mediaInfo)
{
    Candidates candidates;
    for (const auto& track : mediaInfo.audios)
    {
        candidates.push_back(
            {track.index, track.language, track.isDefault.value_or(false), track.commentary.value_or(false)});
    }
    return candidates;
}

TrackSelector::Candidates TrackSelector::AudioCandidates(const FileRecord& fileRecord)
{
    auto candidates = BuiltInAudioCandidates(fileRecord.mediaInfo);
    for (const auto& external : fileRecord.externalAudios)
    {
        candidates.push_back({external.index, external.language, false, external.commentary});
    }
    return candidates;
}

TrackSelector::Candidates TrackSelector::SubtitleCandidates(const FileRecord& fileRecord)
{
    Candidates candidates;

    // Bitmap tracks (PGS etc.) can't be shown, so they're never picked.
    for (const auto& track : fileRecord.mediaInfo.subtitles)
    {
        if (SubtitleExtractor::IsTextCodec(track.codec))
        {
            candidates.push_back(
                {track.index, track.language, track.isDefault.value_or(false), track.forced.value_or(false)});
        }
    }
    for (const auto& external : fileRecord.externalSubtitles)
    {
        candidates.push_back({external.index, external.language, false, external.forced});
    }
    return candidates;
}

// ---- Audio -----------------------------------------------------------------

std::optional<int> TrackSelector::PickAudio(
    const FileRecord& fileRecord, const TrackRequest& request, const TrackContext& context)
{
    if (request.audioTrackId)
    {
        return request.audioTrackId;
    }

    const auto candidates = AudioCandidates(fileRecord);
    if (context.played && Contains(candidates, context.played->audioTrackId))
    {
        return context.played->audioTrackId;
    }
    if (context.folder && context.folder->audioLanguage)
    {
        if (const auto match = InLanguage(candidates, *context.folder->audioLanguage)) return match;
    }
    return PickAudioByPreferences(candidates, context.preferences.audioLanguages);
}

std::optional<int> TrackSelector::PickAudioByPreferences(
    const Candidates& candidates, const std::vector<std::string>& languages)
{
    if (const auto match = InFirstLanguage(candidates, languages)) return match;
    return DefaultOrFirst(candidates);
}

std::string TrackSelector::AudioLanguage(const FileRecord& fileRecord, std::optional<int> audioTrackId)
{
    const auto track = audioTrackId ? fileRecord.find_audio_by_index(*audioTrackId) : std::nullopt;
    return track ? track->language : std::string();
}

// ---- Subtitles -------------------------------------------------------------

int TrackSelector::PickSubtitle(const FileRecord& fileRecord, const TrackRequest& request,
    const TrackContext& context, const std::string& audioLanguage)
{
    if (request.subtitleTrackId)
    {
        return *request.subtitleTrackId < 0 ? kOff : *request.subtitleTrackId;
    }

    const auto candidates = SubtitleCandidates(fileRecord);
    if (const auto remembered = SubtitleFromHistory(candidates, context)) return *remembered;
    if (const auto remembered = SubtitleFromFolder(candidates, context)) return *remembered;
    return SubtitleFromPreferences(candidates, context.preferences, audioLanguage);
}

std::optional<int> TrackSelector::SubtitleFromHistory(const Candidates& candidates, const TrackContext& context)
{
    if (!context.played)
    {
        return std::nullopt;
    }
    if (context.played->subtitleTrackId < 0)
    {
        return kOff;
    }
    return Contains(candidates, context.played->subtitleTrackId) ? std::optional(context.played->subtitleTrackId)
                                                                  : std::nullopt;
}

std::optional<int> TrackSelector::SubtitleFromFolder(const Candidates& candidates, const TrackContext& context)
{
    if (!context.folder)
    {
        return std::nullopt;
    }
    if (context.folder->subtitlesOff)
    {
        return kOff;
    }
    return context.folder->subtitleLanguage ? InLanguage(candidates, *context.folder->subtitleLanguage)
                                            : std::nullopt;
}

int TrackSelector::SubtitleFromPreferences(
    const Candidates& candidates, const TrackPreferences& preferences, const std::string& audioLanguage)
{
    if (preferences.subtitleMode == SubtitleMode::off)
    {
        return kOff;
    }
    if (ShouldShowSubtitles(preferences, audioLanguage))
    {
        if (const auto full = PickFullSubtitle(candidates, preferences)) return *full;
    }
    return PickForced(candidates, audioLanguage).value_or(kOff);
}

bool TrackSelector::ShouldShowSubtitles(const TrackPreferences& preferences, const std::string& audioLanguage)
{
    switch (preferences.subtitleMode)
    {
        case SubtitleMode::always: return true;
        case SubtitleMode::off:
        case SubtitleMode::forcedOnly: return false;
        case SubtitleMode::foreignAudioOnly: break;
    }

    // With no languages of their own there's nothing to call foreign.
    const auto& languages = preferences.EffectiveSubtitleLanguages();
    return !languages.empty() && std::ranges::none_of(languages,
                                     [&](const std::string& language)
                                     { return Languages::SameLanguage(language, audioLanguage); });
}

std::optional<int> TrackSelector::PickFullSubtitle(const Candidates& candidates, const TrackPreferences& preferences)
{
    if (const auto match = InFirstLanguage(candidates, preferences.EffectiveSubtitleLanguages())) return match;
    return DefaultOrFirst(candidates);
}

std::optional<int> TrackSelector::PickForced(const Candidates& candidates, const std::string& audioLanguage)
{
    for (const auto& candidate : candidates)
    {
        if (candidate.secondary && Languages::SameLanguage(candidate.language, audioLanguage))
        {
            return candidate.index;
        }
    }
    return std::nullopt;
}

// ---- Helpers ---------------------------------------------------------------

std::optional<int> TrackSelector::Best(
    const Candidates& candidates, const std::function<bool(const Candidate&)>& matches)
{
    std::optional<int> secondaryMatch;
    for (const auto& candidate : candidates)
    {
        if (!matches(candidate)) continue;
        if (!candidate.secondary) return candidate.index;
        if (!secondaryMatch) secondaryMatch = candidate.index;
    }
    return secondaryMatch;
}

std::optional<int> TrackSelector::InLanguage(const Candidates& candidates, const std::string& language)
{
    return Best(candidates,
        [&](const Candidate& candidate) { return Languages::SameLanguage(candidate.language, language); });
}

std::optional<int> TrackSelector::InFirstLanguage(
    const Candidates& candidates, const std::vector<std::string>& languages)
{
    for (const auto& language : languages)
    {
        if (const auto match = InLanguage(candidates, language)) return match;
    }
    return std::nullopt;
}

std::optional<int> TrackSelector::DefaultOrFirst(const Candidates& candidates)
{
    if (const auto match = Best(candidates, [](const Candidate& candidate) { return candidate.isDefault; }))
    {
        return match;
    }
    return Best(candidates, [](const Candidate&) { return true; });
}

bool TrackSelector::Contains(const Candidates& candidates, int index)
{
    return std::ranges::contains(candidates, index, &Candidate::index);
}
