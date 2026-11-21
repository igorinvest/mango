#include "SubtitleExtractor.hpp"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <iterator>
#include <memory>
#include <mutex>
#include <set>
#include <string_view>


#include "../../../file/File.hpp"
#include "../../../media/MediaBackend.hpp"
#include "SubtitleCharset.hpp"
#include "../../../log/Log.hpp"

namespace
{
constexpr auto kTempSuffix = ".tmp";

// Serializes starting and stopping extractions. Held across MediaJob::
// Stop(), which waits for the exit callback — so the callback itself only
// ever takes stateMutex below, never this one.
std::mutex controlMutex;

// Guards the fields below; signalled through finished whenever an
// extraction exits.
std::mutex stateMutex;
std::condition_variable finished;
bool running = false;
std::string runningFastHash;

// Only ever replaced or stopped under controlMutex, and never touched from
// the process's own exit callback (destroying it there would join its own
// monitor thread).
std::shared_ptr<MediaJob> process;

// External file conversions (see ConvertExternal). conversionMutex makes
// them run one at a time; externalMutex guards the rest and is never held
// while waiting on ffmpeg.
std::mutex conversionMutex;
std::mutex externalMutex;
std::shared_ptr<MediaJob> externalProcess;
// Bumped by every Cancel(), so a conversion can tell being stopped from
// failing.
std::uint64_t cancelGeneration = 0;
// "<path>|<size>|<mtime>" of every file that failed to convert — an edited
// file gets another chance.
std::set<std::string> brokenFiles;

std::string BrokenKey(const ExternalSubtitle& subtitle)
{
    return subtitle.path + "|" + std::to_string(subtitle.fileSizeBytes) + "|" +
        std::to_string(subtitle.lastWriteUnixSeconds);
}
}  // namespace

void SubtitleExtractor::ExtractAll(const FileRecord& fileRecord)
{
    ExtractBuiltInTracks(fileRecord);
    ExtractExternalTracks(fileRecord);
}

void SubtitleExtractor::ExtractBuiltInTracks(const FileRecord& fileRecord)
{
    if (!EnsureExtractionRunning(fileRecord))
    {
        return;
    }

    std::unique_lock lock(stateMutex);
    finished.wait(lock, [&] { return !running || runningFastHash != fileRecord.fastHash; });
}

void SubtitleExtractor::ExtractAllInBackground(const FileRecord& fileRecord)
{
    EnsureExtractionRunning(fileRecord);
}

void SubtitleExtractor::ExtractExternalTracks(const FileRecord& fileRecord)
{
    for (const auto& subtitle : fileRecord.externalSubtitles)
    {
        if (CanExtractExternal(subtitle))
        {
            ConvertExternal(fileRecord, subtitle);
        }
    }
}

void SubtitleExtractor::Cancel()
{
    CancelExternal();

    std::lock_guard controlLock(controlMutex);
    CancelLocked();
}

bool SubtitleExtractor::IsExtracted(const FileRecord& fileRecord, int subtitleTrackIndex)
{
    return File::Exists(OutputPath(fileRecord.getStreamFolder(), subtitleTrackIndex));
}

bool SubtitleExtractor::CanExtract(const FileRecord& fileRecord, int subtitleTrackIndex)
{
    if (const auto* external = fileRecord.FindExternalSubtitle(subtitleTrackIndex))
    {
        return CanExtractExternal(*external);
    }

    return std::ranges::any_of(fileRecord.mediaInfo.subtitles,
        [subtitleTrackIndex](const SubtitleTrack& track)
        { return track.index == subtitleTrackIndex && IsTextCodec(track.codec); });
}

bool SubtitleExtractor::WaitUntilExtracted(const FileRecord& fileRecord, int subtitleTrackIndex)
{
    if (IsExtracted(fileRecord, subtitleTrackIndex))
    {
        return true;
    }
    if (!CanExtract(fileRecord, subtitleTrackIndex))
    {
        return false;
    }

    if (const auto* external = fileRecord.FindExternalSubtitle(subtitleTrackIndex))
    {
        return ConvertExternal(fileRecord, *external);
    }

    ExtractBuiltInTracks(fileRecord);
    return IsExtracted(fileRecord, subtitleTrackIndex);
}

bool SubtitleExtractor::ConvertExternal(
    const FileRecord& fileRecord, const ExternalSubtitle& subtitle)
{
    std::lock_guard serial(conversionMutex);

    // Another request may have converted it while this one waited.
    if (IsExtracted(fileRecord, subtitle.index))
    {
        return true;
    }

    const auto streamFolder = fileRecord.getStreamFolder();
    if (IsBroken(subtitle) || !File::CreateDirectoryPath(streamFolder))
    {
        return false;
    }

    const auto inputPath =
        SubtitleCharset::PrepareFfmpegInput(subtitle, fileRecord.getSubtitleCacheFolder());
    if (!inputPath)
    {
        return false;
    }

    auto tempPath = OutputPath(streamFolder, subtitle.index);
    tempPath += kTempSuffix;

    const auto result = RunExternalConversion(*inputPath, tempPath);
    PublishOutputs(streamFolder, {subtitle.index}, result.completed);

    if (!result.completed && !result.cancelled)
    {
        Log::Warning(LogCategory::Subtitles, "can't convert external subtitle {}", subtitle.path);
        MarkBroken(subtitle);
    }

    return IsExtracted(fileRecord, subtitle.index);
}

SubtitleExtractor::ExternalRunResult SubtitleExtractor::RunExternalConversion(
    const std::filesystem::path& inputPath, const std::filesystem::path& tempPath)
{
    auto exited = std::make_shared<std::promise<bool>>();
    auto completed = exited->get_future();

    std::shared_ptr<MediaJob> running;
    std::uint64_t generation = 0;
    {
        std::lock_guard lock(externalMutex);
        generation = cancelGeneration;
        running = MediaBackends::Get().StartSubtitleFileConversion(inputPath, tempPath,
            [exited](bool completedSuccessfully) { exited->set_value(completedSuccessfully); });
        externalProcess = running;
    }

    if (!running)
    {
        return {};
    }

    ExternalRunResult result;
    result.completed = completed.get();

    std::lock_guard lock(externalMutex);
    result.cancelled = generation != cancelGeneration;
    if (externalProcess == running)
    {
        externalProcess.reset();
    }
    return result;
}

void SubtitleExtractor::CancelExternal()
{
    std::shared_ptr<MediaJob> running;
    {
        std::lock_guard lock(externalMutex);
        ++cancelGeneration;
        running = externalProcess;
    }

    // Outside the lock: Stop() waits for the exit callback, and the
    // converting thread needs the lock to finish up.
    if (running)
    {
        running->Stop();
    }
}

bool SubtitleExtractor::IsBroken(const ExternalSubtitle& subtitle)
{
    std::lock_guard lock(externalMutex);
    return brokenFiles.contains(BrokenKey(subtitle));
}

void SubtitleExtractor::MarkBroken(const ExternalSubtitle& subtitle)
{
    std::lock_guard lock(externalMutex);
    brokenFiles.insert(BrokenKey(subtitle));
}

bool SubtitleExtractor::CanExtractExternal(const ExternalSubtitle& subtitle)
{
    return !IsBroken(subtitle) && File::Exists(File::FromUtf8(subtitle.path));
}

bool SubtitleExtractor::EnsureExtractionRunning(const FileRecord& fileRecord)
{
    std::lock_guard controlLock(controlMutex);

    {
        std::lock_guard lock(stateMutex);
        if (running && runningFastHash == fileRecord.fastHash)
        {
            return true;
        }
    }

    CancelLocked();

    const auto tracks = MissingTextTracks(fileRecord);
    const auto streamFolder = fileRecord.getStreamFolder();
    if (tracks.empty() || !File::CreateDirectoryPath(streamFolder))
    {
        return false;
    }

    std::vector<int> trackIndices;
    std::ranges::transform(tracks, std::back_inserter(trackIndices),
        [](const SubtitleTrack& track) { return track.index; });
    const auto outputs = TempOutputs(trackIndices, streamFolder);

    // Marked running before the launch: a fast-failing ffmpeg can fire its
    // exit callback before Start() even returns, and that callback is what
    // clears the flag again.
    {
        std::lock_guard lock(stateMutex);
        running = true;
        runningFastHash = fileRecord.fastHash;
    }

    process = MediaBackends::Get().StartSubtitleExtraction(fileRecord, outputs,
        [streamFolder, trackIndices](bool completedSuccessfully)
        {
            PublishOutputs(streamFolder, trackIndices, completedSuccessfully);

            std::lock_guard lock(stateMutex);
            running = false;
            finished.notify_all();
        });

    if (!process)
    {
        std::lock_guard lock(stateMutex);
        running = false;
        finished.notify_all();
        return false;
    }

    return true;
}

void SubtitleExtractor::CancelLocked()
{
    if (process)
    {
        // Blocks until the exit callback has run, so the partial outputs
        // are already discarded once this returns.
        process->Stop();
        process.reset();
    }
}

bool SubtitleExtractor::IsTextCodec(const std::string& codec)
{
    static constexpr std::array<std::string_view, 5> kBitmapCodecs = {
        "hdmv_pgs_subtitle", "dvd_subtitle", "dvb_subtitle", "dvb_teletext", "xsub"};

    return std::ranges::find(kBitmapCodecs, codec) == kBitmapCodecs.end();
}

std::vector<SubtitleTrack> SubtitleExtractor::MissingTextTracks(const FileRecord& fileRecord)
{
    std::vector<SubtitleTrack> tracks;
    std::ranges::copy_if(fileRecord.mediaInfo.subtitles, std::back_inserter(tracks),
        [&fileRecord](const SubtitleTrack& track)
        { return IsTextCodec(track.codec) && !IsExtracted(fileRecord, track.index); });
    return tracks;
}

void SubtitleExtractor::PublishOutputs(const std::filesystem::path& streamFolder,
    const std::vector<int>& trackIndices, bool completedSuccessfully)
{
    for (const int index : trackIndices)
    {
        const auto finalPath = OutputPath(streamFolder, index);
        auto tempPath = finalPath;
        tempPath += kTempSuffix;

        std::error_code error;
        if (completedSuccessfully)
        {
            std::filesystem::rename(tempPath, finalPath, error);
        }
        else
        {
            std::filesystem::remove(tempPath, error);
        }
    }
}

std::filesystem::path SubtitleExtractor::OutputPath(
    const std::filesystem::path& streamFolder, int subtitleTrackIndex)
{
    return streamFolder / (std::to_string(subtitleTrackIndex) + ".vtt");
}

std::vector<SubtitleOutput> SubtitleExtractor::TempOutputs(
    const std::vector<int>& trackIndices, const std::filesystem::path& streamFolder)
{
    std::vector<SubtitleOutput> outputs;
    for (const int index : trackIndices)
    {
        auto tempPath = OutputPath(streamFolder, index);
        tempPath += kTempSuffix;
        outputs.push_back({.trackIndex = index, .path = std::move(tempPath)});
    }
    return outputs;
}
