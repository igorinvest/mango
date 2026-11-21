#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../library/FileRecord.hpp"
#include "TrackContext.hpp"
#include "../TrackRequest.hpp"
#include "../MediaInfo.hpp"

// Picks a stream's tracks (see docs/scope/track-selection-scope.md,
// section 4). For each of audio and subtitles the first rule that gives a
// track wins:
//   1. chosen: the id the request carries (subtitles -1: off);
//   2. per-file memory: the track last played in this file, if it exists;
//   3. per-folder memory: a track in the remembered language, or off;
//   4. preferences: the first preferred language with a track (subtitles:
//      the mode decides first whether to show any);
//   5. the file's "default" disposition;
//   6. the first track.
// At every step a normal track beats a commentary (audio) or forced
// (subtitles) one, and a built-in track beats an external one. Bitmap
// subtitles are never auto-picked. Subtitles are picked after audio, since
// the mode and forced tracks depend on the audio's language. Pure: reads
// nothing but its arguments.
class TrackSelector
{
   public:
    [[nodiscard]] static StreamTrackSelection SelectTracks(
        const FileRecord& fileRecord, const TrackRequest& request, const TrackContext& context);

    // Among the built-in audio tracks only, by rules 4–6 — what playback
    // falls back to when a picked external audio file has gone missing.
    [[nodiscard]] static std::optional<int> SelectBuiltInAudioTrack(
        const MediaInfo& mediaInfo, const TrackPreferences& preferences);

   private:
    // A track the selector may pick, built-in or external. secondary: a
    // commentary audio or forced subtitle track.
    struct Candidate
    {
        int index = 0;
        std::string language;
        bool isDefault = false;
        bool secondary = false;
    };
    using Candidates = std::vector<Candidate>;

    // A subtitle decision: a track index, or kOff. std::nullopt from a
    // rule: it doesn't decide, the next one does.
    static constexpr int kOff = -1;

    static std::optional<int> SelectVideoTrack(const MediaInfo& mediaInfo);
    static int ScoreVideoTrack(const VideoTrack& track);

    // Built-in tracks first, then external files.
    static Candidates AudioCandidates(const FileRecord& fileRecord);
    static Candidates BuiltInAudioCandidates(const MediaInfo& mediaInfo);
    static Candidates SubtitleCandidates(const FileRecord& fileRecord);

    static std::optional<int> PickAudio(
        const FileRecord& fileRecord, const TrackRequest& request, const TrackContext& context);
    // Rules 4–6 for audio.
    static std::optional<int> PickAudioByPreferences(
        const Candidates& candidates, const std::vector<std::string>& languages);

    // kOff or a track index.
    static int PickSubtitle(const FileRecord& fileRecord, const TrackRequest& request, const TrackContext& context,
        const std::string& audioLanguage);
    static std::optional<int> SubtitleFromHistory(const Candidates& candidates, const TrackContext& context);
    static std::optional<int> SubtitleFromFolder(const Candidates& candidates, const TrackContext& context);
    static int SubtitleFromPreferences(
        const Candidates& candidates, const TrackPreferences& preferences, const std::string& audioLanguage);

    // Whether the mode shows full (not only forced) subtitles with audio in
    // audioLanguage.
    static bool ShouldShowSubtitles(const TrackPreferences& preferences, const std::string& audioLanguage);
    // Rules 4–6 for a full subtitle track.
    static std::optional<int> PickFullSubtitle(const Candidates& candidates, const TrackPreferences& preferences);
    // A forced track in the audio's language.
    static std::optional<int> PickForced(const Candidates& candidates, const std::string& audioLanguage);

    // The first candidate matching, preferring a normal one to a secondary.
    static std::optional<int> Best(const Candidates& candidates, const std::function<bool(const Candidate&)>& matches);
    static std::optional<int> InLanguage(const Candidates& candidates, const std::string& language);
    static std::optional<int> InFirstLanguage(const Candidates& candidates, const std::vector<std::string>& languages);
    static std::optional<int> DefaultOrFirst(const Candidates& candidates);
    static bool Contains(const Candidates& candidates, int index);

    static std::string AudioLanguage(const FileRecord& fileRecord, std::optional<int> audioTrackId);
};
