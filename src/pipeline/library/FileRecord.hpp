#pragma once

#include <glaze/glaze.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../MediaInfo.hpp"

// One place a file was found by scanning an added LibraryFolder (see
// Library::ScanFolder). fileSizeBytes/lastWriteUnixSeconds are what that
// file looked like at the last scan, so a re-scan can skip re-hashing a file
// that hasn't changed.
struct FolderLocation
{
    std::string folderId;

    // From the folder's root to the file, '/'-separated on every platform.
    std::string relativePath;

    std::uint64_t fileSizeBytes = 0;
    std::int64_t lastWriteUnixSeconds = 0;
};

class FileRecord
{
   public:
    std::string fastHash;
    std::filesystem::path filePath;
    MediaInfo mediaInfo;

    // Base64-encoded macOS security-scoped bookmark for filePath, if it was
    // added through a sandboxed native file picker. Persisted so read access
    // can be re-activated on every app launch, not just the one it was
    // picked in — NSOpenPanel's own grant doesn't survive a restart.
    std::optional<std::string> securityBookmark;

    // Display name: mediaInfo.title if present, otherwise derived from the
    // file name. Not editable yet, but kept as its own field so it can
    // become user-editable later without changing how it's stored.
    std::string libraryName;

    // Filename (e.g. "cover.jpg") of this file's embedded poster/cover art,
    // extracted by CoverExtractor into getMetadataFolder() when the file
    // was added to the library. std::nullopt if the source file has no
    // embedded cover - most files don't.
    std::optional<std::string> coverFileName;

    // True only for the current server run's own additions (see
    // Library::SelectFile) — Library::LoadFileCache always clears this back
    // to false right after reading library.json, regardless of whatever
    // value happened to be persisted, so a restart never carries a "new"
    // marker over from a previous run. Drives the UIs' "New" badge.
    bool addedThisSession = false;

    // When this file was first added to the library, as Unix seconds —
    // drives the UIs' "Date Added" sort. Kept across re-adding the same
    // file (see Library::SelectFile). 0 only for a record whose date
    // couldn't be recovered (see Library::LoadFileCache's backfill).
    std::int64_t addedAtUnixSeconds = 0;

    // Whether this file was added with Add Movie (see Library::SelectFile),
    // as opposed to only being found by a folder scan. Defaults to true so
    // records from library.json files written before folders existed load
    // as manual without a migration. A record stays in the library while
    // it's manual or still has at least one folderLocation.
    bool addedManually = true;

    // Every added folder this file was found in (see Library::ScanFolder).
    // The same file can live in several folders, and also be manual.
    std::vector<FolderLocation> folderLocations;

    // Subtitle files found next to this file (see ExternalSubtitleFinder),
    // sorted by index. Missing in older library.json files, which reads as
    // empty.
    std::vector<ExternalSubtitle> externalSubtitles;

    // The index the next newly found external subtitle gets, so an index
    // is never handed to a different file while this record exists — not
    // even after the file that had it is deleted. 0 until the first one.
    int nextExternalSubtitleIndex = 0;

    // Audio files found next to this file (see ExternalAudioFinder), sorted
    // by index. Missing in older library.json files, which reads as empty.
    std::vector<ExternalAudio> externalAudios;

    // Like nextExternalSubtitleIndex, for externalAudios.
    int nextExternalAudioIndex = 0;

    FileRecord(std::string hash, std::filesystem::path path, MediaInfo info,
        std::optional<std::string> bookmark = std::nullopt);

    // Конструктор по умолчанию генерируется автоматически
    FileRecord() = default;

    std::filesystem::path getStreamFolder() const;

    bool deleteStreamFolder() const;

    // This file's persistent per-file storage folder - holds metadata.json
    // (see getMetadataFilePath) and, if extracted, this file's cover image
    // (see coverFileName). Unlike getStreamFolder(), never deleted when a
    // new stream is initiated (see StreamManager).
    std::filesystem::path getMetadataFolder() const;

    // Path to this file's raw `ffprobe -show_format -show_streams` JSON
    // dump within getMetadataFolder() (see FFmpegWrapper::Read, which
    // writes it out every time this file is (re-)added to the library,
    // right before MediaInfo/FileRecord are built from it) - callers that
    // need ffprobe's raw per-stream fields (e.g. CoverExtractor, which
    // needs `disposition`/`tags` that MediaInfo doesn't model) can read
    // this instead of re-invoking ffprobe.
    std::filesystem::path getMetadataFilePath() const;

    // Where UTF-8 copies of this file's non-UTF-8 external subtitle files
    // live (see SubtitleCharset::PrepareFfmpegInput), inside
    // getMetadataFolder().
    std::filesystem::path getSubtitleCacheFolder() const;

    //Find tracks
    std::optional<VideoTrack> find_video_by_index(int target_index = 0) const;
    // Built-in tracks and external audio files alike.
    std::optional<AudioTrack> find_audio_by_index(int target_index = 0) const;
    // Built-in tracks and external subtitle files alike.
    std::optional<SubtitleTrack> find_subtitle_by_index(int target_index = 0) const;

    [[nodiscard]] const ExternalSubtitle* FindExternalSubtitle(int index) const;

    // Whether this file has any subtitle track to choose, built-in or
    // external.
    [[nodiscard]] bool HasAnySubtitles() const;

    [[nodiscard]] const ExternalAudio* FindExternalAudio(int index) const;

    // Built-in and external audio tracks together — more than one means
    // the apps show an audio picker.
    [[nodiscard]] std::size_t AudioTrackCount() const;
};
