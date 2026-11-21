#include "FileRecord.hpp"
#include "../MediaInfo.hpp"
#include "../../file/File.hpp"
#include "../../configuration/Settings.hpp"

FileRecord::FileRecord(
    std::string hash, std::filesystem::path path, MediaInfo info, std::optional<std::string> bookmark)
    : fastHash(std::move(hash)),
      filePath(std::move(path)),
      mediaInfo(std::move(info)),
      securityBookmark(std::move(bookmark)),
      libraryName(mediaInfo.title.empty() ? filePath.stem().string() : mediaInfo.title)
{
}

std::filesystem::path FileRecord::getStreamFolder() const
{
    return File::getApplicationDataPath() /
           Settings::GetStringSetting("streamsFolder").value_or("streams") / fastHash;
}

bool FileRecord::deleteStreamFolder() const
{
       return File::RemoveDirectoryPath(getStreamFolder());
}

std::filesystem::path FileRecord::getMetadataFolder() const
{
    return File::getApplicationDataPath() /
           Settings::GetStringSetting("libraryMetadataFolder").value_or("library-metadata") / fastHash;
}

std::filesystem::path FileRecord::getMetadataFilePath() const
{
    return getMetadataFolder() / "metadata.json";
}

std::filesystem::path FileRecord::getSubtitleCacheFolder() const
{
    return getMetadataFolder() / "subtitles";
}

// 1. Поиск видео-трека
std::optional<VideoTrack> FileRecord::find_video_by_index(int target_index) const
{
    auto it = std::ranges::find_if(mediaInfo.videos,
        [target_index](const auto& track) { return track.index == target_index; });

    if (it != mediaInfo.videos.end()) return *it;
    return std::nullopt;
}

// 2. Поиск аудио-трека
std::optional<AudioTrack> FileRecord::find_audio_by_index(int target_index) const
{
    auto it = std::ranges::find_if(mediaInfo.audios,
        [target_index](const auto& track) { return track.index == target_index; });

    if (it != mediaInfo.audios.end()) return *it;

    if (const auto* external = FindExternalAudio(target_index)) return external->ToTrack();
    return std::nullopt;
}

// 3. Поиск трека субтитров
std::optional<SubtitleTrack> FileRecord::find_subtitle_by_index(int target_index) const
{
    auto it = std::ranges::find_if(mediaInfo.subtitles,
        [target_index](const auto& track) { return track.index == target_index; });

    if (it != mediaInfo.subtitles.end()) return *it;

    if (const auto* external = FindExternalSubtitle(target_index)) return external->ToTrack();
    return std::nullopt;
}

const ExternalSubtitle* FileRecord::FindExternalSubtitle(int index) const
{
    if (!IsExternalSubtitleIndex(index)) return nullptr;

    auto it = std::ranges::find(externalSubtitles, index, &ExternalSubtitle::index);
    return it != externalSubtitles.end() ? &*it : nullptr;
}

bool FileRecord::HasAnySubtitles() const
{
    return !mediaInfo.subtitles.empty() || !externalSubtitles.empty();
}

const ExternalAudio* FileRecord::FindExternalAudio(int index) const
{
    if (!IsExternalAudioIndex(index)) return nullptr;

    auto it = std::ranges::find(externalAudios, index, &ExternalAudio::index);
    return it != externalAudios.end() ? &*it : nullptr;
}

std::size_t FileRecord::AudioTrackCount() const
{
    return mediaInfo.audios.size() + externalAudios.size();
}
