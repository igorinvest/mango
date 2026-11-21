#include "StreamManager.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <print>
#include <vector>

#include "../../file/Json.hpp"
#include "../../file/File.hpp"
#include "../../log/Log.hpp"
#include "../../media/MediaBackend.hpp"
#include "../../media/StreamOutputLayout.hpp"
#include "target/StreamTargetResolver.hpp"
#include "subtitles/SubtitleExtractor.hpp"
#include "StreamDescription.hpp"
#include "../StreamId.hpp"
#include "../MediaInfo.hpp"

namespace
{
const std::string streamStateFileName = "stream_state.json";

// Advisory only — the master playlist only ever has one video/audio
// variant, so no ABR selection depends on it being exact.
constexpr int64_t kAdvisoryBandwidth = 5'000'000;

std::string SubtitleRenditionLine(const SubtitleTrack& subtitleTrack)
{
    const std::string name = !subtitleTrack.title.empty() ? subtitleTrack.title
        : !subtitleTrack.language.empty()                ? subtitleTrack.language
                                                         : "Subtitles";
    const std::string language = subtitleTrack.language.empty() ? "und" : subtitleTrack.language;

    return std::format(
        R"(#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="subs",NAME="{}",LANGUAGE="{}",DEFAULT=YES,AUTOSELECT=YES,URI="{}")"
        "\n",
        name, language, StreamOutputLayout::kHlsSubtitlePlaylistName);
}
}  // namespace

// Must have external linkage (not in the anonymous namespace above) — Glaze's
// reflection mangles the type name, which requires linkage.
struct StreamManagerState
{
    std::optional<FileRecord> currentFileRecord;
    // Empty in state files written before stream IDs existed.
    std::string currentStreamId;
    std::optional<TrackList> currentTrackList;
    std::optional<StreamTarget> currentTarget;
    bool transcodeCompleted = false;
};

StreamManager& StreamManager::GetInstance()
{
    // Guaranteed to be destroyed and instantiated on first use
    static StreamManager instance;
    return instance;
}

StreamManager::StreamManager() { LoadState(); }

void StreamManager::LoadState()
{
    const auto statePath = File::getApplicationDataPath() / streamStateFileName;

    // Lenient for the same reason as Library::LoadFileCache — the saved
    // FileRecord may predate PlaybackHistory.
    auto state = Json::ReadJsonFromFileLenient<StreamManagerState>(statePath);
    if (!state || !state->currentFileRecord)
    {
        return;
    }

    // A state file from before targets were saved can't be compared with a
    // new request, so it's never reused: the next Play restarts anyway.
    if (!state->transcodeCompleted || !state->currentTarget)
    {
        // The previous ffmpeg conversion can't survive a restart. If it hadn't
        // finished by the time the app quit, the stream folder only has
        // partial segments that can never be completed — discard rather than
        // resume into a stream that will never play through.
        state->currentFileRecord->deleteStreamFolder();
        return;
    }

    // The transcode fully finished before the app quit: the segments on disk
    // are complete and still playable without relaunching ffmpeg.
    m_currentFileRecord = state->currentFileRecord;
    // Kept, so a TV still playing it from before the relaunch keeps loading
    // segments under the same URL.
    m_currentStreamId = state->currentStreamId.empty() ? StreamId::Generate() : state->currentStreamId;
    m_currentTrackList = state->currentTrackList;
    m_currentTarget = state->currentTarget;

    // The profile's playlist isn't serialized (see glz::meta<StreamProfile>).
    m_currentTarget->profile.playlist = m_currentTarget->playlist;
    m_transcodeCompleted = true;

    // Kept appending to the log it had before the relaunch.
    Log::OpenStreamLog(m_currentFileRecord->getStreamFolder(),
        StreamDescription::Stream(*m_currentFileRecord, m_currentTrackList.value_or(TrackList{}), *m_currentTarget) +
            " (restored after relaunch)");
    Log::Info(LogCategory::Stream, "restored finished stream {} after relaunch", m_currentFileRecord->fastHash);
}

void StreamManager::SaveState()
{
    auto applicationFolder = File::getApplicationDataPath();
    std::filesystem::create_directories(applicationFolder);

    StreamManagerState state{
        m_currentFileRecord, m_currentStreamId, m_currentTrackList, m_currentTarget, m_transcodeCompleted};
    Json::SaveJsonToFile(applicationFolder / streamStateFileName, state);
}

std::optional<FileRecord> StreamManager::GetCurrentFileRecord()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentFileRecord;
}
std::optional<std::string> StreamManager::GetCurrentStreamId()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentFileRecord ? std::optional(m_currentStreamId) : std::nullopt;
}

std::optional<FileRecord> StreamManager::FindStream(const std::string& streamId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_currentFileRecord || streamId.empty() || streamId != m_currentStreamId)
    {
        return std::nullopt;
    }
    return m_currentFileRecord;
}

std::optional<TrackList> StreamManager::GetCurrentTrackList()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentTrackList;
}
std::optional<StreamTarget> StreamManager::GetCurrentTarget()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentTarget;
}

std::optional<double> StreamManager::GetTranscodeProgressPercent()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // A dead conversion reports as "nothing streaming", so the native apps
    // drop its stuck progress instead of showing it forever.
    if (!m_currentFileRecord || m_transcodeFailed)
    {
        return std::nullopt;
    }

    // 100 regardless of the last progress report, which can lag the source
    // duration slightly.
    if (m_transcodeCompleted)
    {
        return 100.0;
    }

    const auto durationSec = m_currentFileRecord->mediaInfo.durationSec;
    const auto progressSeconds =
        m_currentConversion ? m_currentConversion->ProgressSeconds() : std::nullopt;
    if (durationSec <= 0 || !progressSeconds)
    {
        return 0.0;
    }

    const double percent = std::clamp((*progressSeconds / static_cast<double>(durationSec)) * 100.0, 0.0, 100.0);
    LogProgressLocked(percent);
    return percent;
}

void StreamManager::LogProgressLocked(double percent)
{
    // Progress is only known when someone asks (the apps poll it), so the
    // 10% steps are logged from here.
    const int decile = static_cast<int>(percent / 10.0);
    if (decile <= m_loggedProgressDecile)
    {
        return;
    }

    m_loggedProgressDecile = decile;
    Log::Debug(LogCategory::Transcode, "progress {:.0f}% of {}", percent,
        m_currentFileRecord ? m_currentFileRecord->fastHash : std::string("?"));
}

bool StreamManager::IsCurrentTranscodeCompleted()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentFileRecord && !m_transcodeFailed && m_transcodeCompleted;
}

std::optional<ConversionError> StreamManager::GetCurrentConversionError()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_conversionError;
}

std::expected<FileRecord, ConversionError> StreamManager::StartStream(
    const FileRecord& fileRecord, const TrackList& trackList, const StreamTarget& target)
{
    std::lock_guard lock(m_mutex);

    // Checked against the live conversion, not m_currentFileRecord: after a
    // restart the file record can be restored from disk while no conversion
    // is actually running, and that must not block starting a new stream.
    if (m_currentConversion)
    {
        Log::Error(LogCategory::Stream, "StartStream refused for {}: a conversion is still running",
            fileRecord.fastHash);
        return std::unexpected(ConversionError::Unknown);
    }

    const auto streamFolder = fileRecord.getStreamFolder();
    if (!File::CreateDirectoryPath(streamFolder))
    {
        Log::Error(LogCategory::Stream, "StartStream: can't create {}", File::ToUtf8(streamFolder));
        return std::unexpected(ConversionError::OutputFailed);
    }

    // The folder exists from here on, so this stream's lines go into it.
    Log::OpenStreamLog(streamFolder, StreamDescription::Stream(fileRecord, trackList, target));

    const auto muxedTrackList = MuxedTracks(trackList, target.profile);

    // Written before the conversion starts: stopping a started one here
    // would wait for its exit callback, which needs m_mutex.
    if (target.playlist == PlaylistType::HLS &&
        !WriteHlsMasterPlaylist(streamFolder, target.profile, muxedTrackList.subtitleTrack))
    {
        Log::Error(LogCategory::Stream, "StartStream: failed to write the HLS master playlist");
        return std::unexpected(ConversionError::OutputFailed);
    }

    if (auto started = StartConversionLocked(fileRecord, muxedTrackList, target); !started)
    {
        return std::unexpected(started.error());
    }

    m_currentFileRecord = fileRecord;
    m_currentStreamId = StreamId::Generate();
    m_currentTrackList = trackList;
    m_currentTarget = target;
    m_transcodeCompleted = false;
    m_transcodeFailed = false;
    m_conversionError.reset();
    m_loggedProgressDecile = -1;

    SaveState();

    return *m_currentFileRecord;
}

TrackList StreamManager::MuxedTracks(const TrackList& trackList, const StreamProfile& profile)
{
    // The DASH muxer cannot embed subtitles correctly (they end up muxed as a
    // broken audio-like stream), so for sidecar-subtitle profiles the main
    // conversion never sees the subtitle track at all — it's extracted
    // separately by SubtitleExtractor.
    auto muxed = trackList;
    if (profile.sidecarSubtitles)
    {
        muxed.subtitleTrack = std::nullopt;
    }
    return muxed;
}

std::expected<void, ConversionError> StreamManager::StartConversionLocked(
    const FileRecord& fileRecord, const TrackList& muxedTrackList, const StreamTarget& target)
{
    const auto targetSpecs = StreamTargetResolver::Resolve(muxedTrackList.videoTrack,
        muxedTrackList.audioTrack, muxedTrackList.subtitleTrack, target);

    const ConversionRequest request{.file = fileRecord,
        .tracks = muxedTrackList,
        .targets = targetSpecs,
        .profile = target.profile,
        .outputFolder = fileRecord.getStreamFolder()};

    auto conversion = MediaBackends::Get().StartConversion(
        request, [this](ConversionResult result) { OnConversionExit(result); });
    if (!conversion)
    {
        Log::Error(LogCategory::Transcode, "conversion not started: {}",
            ConversionErrors::ToString(conversion.error()));
        return std::unexpected(conversion.error());
    }

    Log::Info(LogCategory::Transcode, "started {}: {} ({})", fileRecord.fastHash,
        StreamDescription::Specs(targetSpecs), StreamDescription::Target(target));

    m_currentConversion = std::move(*conversion);
    return {};
}

void StreamManager::StopCurrentStream()
{
    // Still writing sidecars into the folder about to be deleted below.
    SubtitleExtractor::Cancel();

    if (m_currentConversion)
    {
        m_currentConversion->Stop();
        m_currentConversion.reset();
    }

    // Locked only from here: stopping the conversion above waits for its
    // exit callback, which takes m_mutex. Server threads read the stream ID
    // on every request (see FindStream).
    std::lock_guard lock(m_mutex);

    if (m_currentFileRecord)
    {
        Log::Info(LogCategory::Stream, "stopped {}, deleting its stream folder", m_currentFileRecord->fastHash);

        // Closed first: the folder can't be deleted while the file is open
        // (Windows), and the log goes with it.
        Log::CloseStreamLog();
        m_currentFileRecord->deleteStreamFolder();
    }

    m_currentFileRecord.reset();
    m_currentStreamId.clear();
    m_currentTrackList.reset();
    m_currentTarget.reset();
    m_transcodeCompleted = false;
    m_transcodeFailed = false;
    m_conversionError.reset();

    SaveState();
}

bool StreamManager::HasCurrentTranscodeFailed()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_transcodeFailed;
}

void StreamManager::OnConversionExit(ConversionResult result)
{
    std::lock_guard lock(m_mutex);
    m_transcodeCompleted = result.has_value();

    // Also set when StopCurrentStream stops it on purpose — harmless, as
    // StopCurrentStream clears it again right after.
    m_transcodeFailed = !result.has_value();
    m_conversionError = result ? std::nullopt : std::optional(result.error());
    LogConversionExitLocked(result);

    SaveState();
}

void StreamManager::LogConversionExitLocked(const ConversionResult& result) const
{
    const auto fastHash = m_currentFileRecord ? m_currentFileRecord->fastHash : std::string("?");

    if (result)
    {
        Log::Info(LogCategory::Transcode, "finished {} (duration {} s)", fastHash,
            m_currentFileRecord ? m_currentFileRecord->mediaInfo.durationSec : 0);
        return;
    }

    if (result.error() == ConversionError::Cancelled)
    {
        Log::Info(LogCategory::Transcode, "cancelled {}", fastHash);
        return;
    }

    Log::Error(LogCategory::Transcode, "failed {}: {} - see ffmpeg.log next to this log", fastHash,
        ConversionErrors::ToString(result.error()));
}

bool StreamManager::WriteHlsMasterPlaylist(const std::filesystem::path& streamFolder,
    const StreamProfile& profile, const std::optional<SubtitleTrack>& subtitleTrack)
{
    std::string playlist = "#EXTM3U\n#EXT-X-VERSION:6\n";

    // The media playlist has no #EXT-X-ENDLIST until the conversion
    // finishes, and without a start point a player (AVPlayer, and the TV
    // over AirPlay) begins such a playlist near its live edge — wherever
    // the conversion currently is — instead of at 0. Resuming further in is
    // still the client's seek.
    playlist += "#EXT-X-START:TIME-OFFSET=0,PRECISE=YES\n";

    if (subtitleTrack)
    {
        playlist += SubtitleRenditionLine(*subtitleTrack);
    }

    playlist += "#EXT-X-STREAM-INF:BANDWIDTH=" + std::to_string(kAdvisoryBandwidth);
    if (subtitleTrack)
    {
        playlist += R"(,SUBTITLES="subs")";
    }
    playlist += "\n" + std::string(StreamOutputLayout::kHlsMediaPlaylistName) + "\n";

    std::ofstream file(streamFolder / (profile.defaultFileName + profile.defaultExtension),
        std::ios::binary | std::ios::trunc);
    if (!file)
    {
        return false;
    }

    file << playlist;
    return static_cast<bool>(file);
}
