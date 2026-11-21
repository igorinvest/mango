#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct VideoTrack
{
    int index;
    std::string codec;
    int width;
    int height;
    double fps;
    int64_t bitrate;
    std::string language;
    std::string title;
    // Display-matrix rotation in degrees (0, 90, 180 or 270) - phones often
    // store frames landscape with a "rotate 90" flag; width/height are the
    // stored size. std::nullopt only on records probed before this was read
    // (Library::LoadFileCache backfills it from metadata.json).
    std::optional<int> rotation;
    // Interlaced (field-coded, e.g. 1080i HDTV recordings) rather than
    // progressive. Cast devices can't play interlaced H.264, so such tracks
    // are always converted - and deinterlaced on the way. std::nullopt only
    // on records probed before this was read (backfilled like rotation).
    std::optional<bool> interlaced;
};

struct AudioTrack
{
    int index;
    std::string codec;
    int channels;
    int sampleRate;
    int64_t bitrate;
    std::string language;
    std::string title;
    // The file's "default" and "comment" dispositions (see TrackSelector).
    // std::nullopt only on records probed before they were read
    // (Library::LoadFileCache backfills them from metadata.json); external
    // files never have a default.
    std::optional<bool> isDefault;
    std::optional<bool> commentary;
};

struct SubtitleTrack
{
    int index;
    std::string codec;
    std::string language;
    std::string title;
    // The file's "default" and "forced" dispositions, like AudioTrack's.
    std::optional<bool> isDefault;
    std::optional<bool> forced;
};

// A file attached to the container (MKV attachments: fonts, cover art).
struct AttachmentTrack
{
    int index = 0;

    // Position among the container's attachments only, counting from 0 -
    // what a backend extracts an attachment by.
    int attachmentIndex = 0;

    std::string fileName;
    std::string mimeType;
};

// A video stream that is really a still image flagged as the file's cover
// (MP4/M4A-style "attached picture"), not a playable video.
struct AttachedPictureTrack
{
    int index = 0;
    std::string codec;
};

// Subtitle tracks found in files next to a video (Movie.en.srt,
// Subs/English.srt, ...) rather than inside it — see ExternalSubtitleFinder.
// They get indices from here upwards, a range ffprobe stream indices never
// reach, so every int-based track id (TrackInfo, PlaybackHistory, Cast track
// ids, "<index>.vtt") works for them unchanged.
inline constexpr int kExternalSubtitleIndexBase = 1000;

[[nodiscard]] inline bool IsExternalSubtitleIndex(int index)
{
    return index >= kExternalSubtitleIndexBase;
}

struct ExternalSubtitle
{
    // >= kExternalSubtitleIndexBase. Stays with its file across re-scans,
    // so a remembered subtitleTrackId keeps pointing at the same file.
    int index = kExternalSubtitleIndexBase;

    // Absolute path of the subtitle file.
    std::string path;

    // ISO 639-2 code parsed from the file name (see Languages::Normalize),
    // or empty when the name doesn't give one.
    std::string language;

    // File name relative to the video's folder ('/'-separated), e.g.
    // "Movie.en.srt" or "Subs/English.srt" — the picker label.
    std::string title;

    bool forced = false;
    bool hearingImpaired = false;

    // ffmpeg decoder name: "subrip", "webvtt", "ass" or "ssa".
    std::string codec;

    // The file's code page ("CP1251", ...), or empty for UTF-8. ffmpeg then
    // reads a UTF-8 copy instead (see SubtitleCharset::PrepareFfmpegInput).
    std::string charset;

    // What the file looked like when charset was detected, so a re-scan
    // skips files that haven't changed.
    std::uint64_t fileSizeBytes = 0;
    std::int64_t lastWriteUnixSeconds = 0;

    bool operator==(const ExternalSubtitle&) const = default;

    [[nodiscard]] SubtitleTrack ToTrack() const
    {
        return SubtitleTrack{index, codec, language, title, false, forced};
    }
};

// Audio tracks found in files next to a video (Movie.rus.ac3,
// Rus Sound/LostFilm.mka, ...) — see ExternalAudioFinder. Same scheme as
// external subtitles: the audio and subtitle index spaces are separate
// (audioTrackId vs subtitleTrackId), so both ranges start at 1000.
inline constexpr int kExternalAudioIndexBase = 1000;

[[nodiscard]] inline bool IsExternalAudioIndex(int index)
{
    return index >= kExternalAudioIndexBase;
}

struct ExternalAudio
{
    // >= kExternalAudioIndexBase. Stays with its file across re-scans, so a
    // remembered audioTrackId keeps pointing at the same file.
    int index = kExternalAudioIndexBase;

    // Absolute path of the audio file. Only its first audio stream is used.
    std::string path;

    // ISO 639-2 code parsed from the file or folder name, or empty.
    std::string language;

    // File name relative to the video's folder ('/'-separated), e.g.
    // "Movie.rus.ac3" or "Rus Sound/LostFilm.mka" — the picker label.
    std::string title;

    bool commentary = false;

    // Probed from the file's first audio stream.
    std::string codec;
    int channels = 0;
    int sampleRate = 0;
    std::int64_t bitrate = 0;
    double durationSeconds = 0.0;
    double startTimeSeconds = 0.0;

    // What the file looked like when it was probed, so a re-scan skips
    // files that haven't changed.
    std::uint64_t fileSizeBytes = 0;
    std::int64_t lastWriteUnixSeconds = 0;

    bool operator==(const ExternalAudio&) const = default;

    [[nodiscard]] AudioTrack ToTrack() const
    {
        return AudioTrack{index, codec, channels, sampleRate, bitrate, language, title, false, commentary};
    }
};

struct MediaInfo
{
    std::vector<VideoTrack> videos;
    std::vector<AudioTrack> audios;
    std::vector<SubtitleTrack> subtitles;

    // Cover candidates (see CoverExtractor). Empty in records probed before
    // they were read; covers are only extracted when a file is added, so
    // those records never need them.
    std::vector<AttachmentTrack> attachments;
    std::vector<AttachedPictureTrack> attachedPictures;

    int64_t durationSec;
    int64_t fileSizeBytes;
    std::string format;
    std::string title;
};

struct StreamTrackSelection
{
    std::optional<int> videoTrackId;
    std::optional<int> audioTrackId;
    std::optional<int> subtitleTrackId;
};

struct TrackList
{
    std::optional<VideoTrack> videoTrack;
    std::optional<AudioTrack> audioTrack;
    std::optional<SubtitleTrack> subtitleTrack;
};
