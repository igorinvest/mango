#include "ExternalAudioFinder.hpp"

#include <algorithm>

#include "../../../file/File.hpp"
#include "../../../media/MediaBackend.hpp"
#include "../../../log/Log.hpp"

namespace fs = std::filesystem;

std::mutex ExternalAudioFinder::s_unreadableMutex;
std::unordered_map<std::string, ExternalFileStamp> ExternalAudioFinder::s_unreadable;

const ExternalFileKind& ExternalAudioFinder::Kind()
{
    static const ExternalFileKind kind{
        .extensions = {".mka", ".ac3", ".eac3", ".dts", ".aac", ".m4a", ".flac", ".mp3", ".opus"},
        .isKindFolder = &IsAudioFolder};
    return kind;
}

std::vector<ExternalFileCandidate> ExternalAudioFinder::FindCandidates(
    const DirectorySnapshot& snapshot, const std::vector<fs::path>& videoPaths)
{
    return ExternalFileFinder::FindCandidates(Kind(), snapshot, videoPaths);
}

bool ExternalAudioFinder::MergeInto(FileRecord& record,
    const std::vector<ExternalFileCandidate>& candidates, const DirectorySnapshot& snapshot)
{
    std::vector<ExternalAudio> merged;

    for (const auto& candidate : candidates)
    {
        if (auto audio = MergeCandidate(record, candidate))
        {
            merged.push_back(std::move(*audio));
        }
    }

    for (const auto& known : record.externalAudios)
    {
        if (!ExternalFileFinder::IsCovered(snapshot, known.path) &&
            !std::ranges::contains(merged, known.path, &ExternalAudio::path))
        {
            merged.push_back(known);
        }
    }

    std::ranges::sort(merged, {}, &ExternalAudio::index);

    if (merged == record.externalAudios)
    {
        return false;
    }

    record.externalAudios = std::move(merged);
    return true;
}

std::optional<ExternalAudio> ExternalAudioFinder::MergeCandidate(
    FileRecord& record, const ExternalFileCandidate& candidate)
{
    const auto path = File::ToUtf8(candidate.path);
    const auto known = std::ranges::find(record.externalAudios, path, &ExternalAudio::path);

    if (known != record.externalAudios.end() &&
        ExternalFileFinder::IsUnchanged(
            known->path, {known->fileSizeBytes, known->lastWriteUnixSeconds}))
    {
        auto unchanged = *known;
        unchanged.title = candidate.title;
        return unchanged;
    }

    if (IsKnownUnreadable(candidate.path))
    {
        return std::nullopt;
    }

    // A known file keeps its index; a new one takes the next, but only
    // once it turns out readable, so a broken file doesn't use one up.
    const bool isNew = known == record.externalAudios.end();
    auto audio = Describe(candidate, isNew ? NextIndex(record) : known->index);
    if (!audio)
    {
        RememberUnreadable(candidate.path);
        return std::nullopt;
    }

    if (isNew)
    {
        record.nextExternalAudioIndex = audio->index + 1;
    }
    return audio;
}

bool ExternalAudioFinder::IsAudioFolder(std::string_view lowerFolderName)
{
    return lowerFolderName == "audio" || lowerFolderName == "sound" ||
        lowerFolderName == "sounds" || lowerFolderName.ends_with(" sound") ||
        lowerFolderName.ends_with(" sounds");
}

std::optional<ExternalAudio> ExternalAudioFinder::Describe(
    const ExternalFileCandidate& candidate, int index)
{
    const auto stamp = ExternalFileFinder::Stamp(candidate.path);
    const auto probed = stamp ? MediaBackends::Get().ProbeAudioFile(candidate.path) : std::nullopt;
    if (!probed)
    {
        Log::Warning(LogCategory::Library, "can't read external audio file {}", File::ToUtf8(candidate.path));
        return std::nullopt;
    }

    const auto tags = ExternalFileFinder::CandidateTags(candidate);

    ExternalAudio audio;
    audio.index = index;
    audio.path = File::ToUtf8(candidate.path);
    audio.language = tags.language;
    audio.title = candidate.title;
    audio.commentary = tags.commentary;
    audio.codec = probed->codec;
    audio.channels = probed->channels;
    audio.sampleRate = probed->sampleRate;
    audio.bitrate = probed->bitrate;
    audio.durationSeconds = probed->durationSeconds;
    audio.startTimeSeconds = probed->startTimeSeconds;
    audio.fileSizeBytes = stamp->fileSizeBytes;
    audio.lastWriteUnixSeconds = stamp->lastWriteUnixSeconds;
    return audio;
}

int ExternalAudioFinder::NextIndex(const FileRecord& record)
{
    int next = std::max(record.nextExternalAudioIndex, kExternalAudioIndexBase);
    for (const auto& audio : record.externalAudios)
    {
        next = std::max(next, audio.index + 1);
    }
    return next;
}

bool ExternalAudioFinder::IsKnownUnreadable(const fs::path& path)
{
    std::lock_guard lock(s_unreadableMutex);

    const auto it = s_unreadable.find(File::ToUtf8(path));
    return it != s_unreadable.end() && ExternalFileFinder::IsUnchanged(it->first, it->second);
}

void ExternalAudioFinder::RememberUnreadable(const fs::path& path)
{
    const auto stamp = ExternalFileFinder::Stamp(path);
    if (!stamp)
    {
        return;
    }

    std::lock_guard lock(s_unreadableMutex);
    s_unreadable.insert_or_assign(File::ToUtf8(path), *stamp);
}
