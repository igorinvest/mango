#include "PlaybackHistory.hpp"

#include <cmath>
#include <filesystem>
#include <format>

#include "../../file/File.hpp"
#include "../../file/Json.hpp"
#include "../library/Library.hpp"
#include "../../log/Log.hpp"

// The parts of a library.json FileRecord that held playback history before
// playback_history.json existed — read leniently, everything else ignored.
// Named namespace, not anonymous: glaze's reflection needs types with
// linkage.
namespace legacy_library
{

struct MediaInfo
{
    std::int64_t durationSec = 0;
};

struct PlaybackFields
{
    MediaInfo mediaInfo;
    double lastPositionSeconds = 0.0;
    int lastAudioTrackId = -1;
    int lastSubtitleTrackId = -1;
    bool hasPlaybackHistory = false;
};

}  // namespace legacy_library

namespace
{

const std::string playbackHistoryFileName = "playback_history.json";

// library.json's name, for the one-time import below.
const std::string legacyLibraryFileName = "library.json";


std::int64_t NowUnixSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

PlaybackHistory::PlaybackHistory() { Load(); }

PlaybackHistory::~PlaybackHistory() { Flush(); }

void PlaybackHistory::Load()
{
    const auto applicationFolder = File::getApplicationDataPath();

    if (auto records = Json::ReadJsonFromFile<std::unordered_map<std::string, PlaybackRecord>>(
            applicationFolder / playbackHistoryFileName))
    {
        m_records = std::move(*records);
        return;
    }

    // Before playback_history.json existed, these lived on each FileRecord
    // in library.json. Read leniently, keeping only those fields — and only
    // if the file isn't there at all, so a corrupt history file isn't
    // silently replaced by stale positions.
    if (std::filesystem::exists(applicationFolder / playbackHistoryFileName))
    {
        Log::Error(LogCategory::History, "could not read {}", playbackHistoryFileName);
        return;
    }

    const auto legacy =
        Json::ReadJsonFromFileLenient<std::unordered_map<std::string, legacy_library::PlaybackFields>>(
            applicationFolder / legacyLibraryFileName);
    if (!legacy)
    {
        return;
    }

    for (const auto& [fastHash, fields] : *legacy)
    {
        if (!fields.hasPlaybackHistory)
        {
            continue;
        }

        PlaybackRecord record;
        record.positionSeconds = fields.lastPositionSeconds;
        record.audioTrackId = fields.lastAudioTrackId;
        record.subtitleTrackId = fields.lastSubtitleTrackId;
        const auto durationSeconds = static_cast<double>(fields.mediaInfo.durationSec);
        record.completed =
            durationSeconds > 0 && fields.lastPositionSeconds / durationSeconds >= completedFraction;
        m_records.emplace(fastHash, record);
    }

    std::lock_guard lock(m_mutex);
    PersistLocked();
}

std::optional<PlaybackRecord> PlaybackHistory::Get(const std::string& fastHash) const
{
    std::lock_guard lock(m_mutex);

    const auto it = m_records.find(fastHash);
    if (it == m_records.end())
    {
        return std::nullopt;
    }
    return it->second;
}

std::unordered_map<std::string, PlaybackRecord> PlaybackHistory::GetAll() const
{
    std::lock_guard lock(m_mutex);
    return m_records;
}

void PlaybackHistory::Report(
    const std::string& fastHash, double positionSeconds, int audioTrackId, int subtitleTrackId)
{
    if (!std::isfinite(positionSeconds) || positionSeconds < 0)
    {
        return;
    }

    // Looked up before taking m_mutex, so this never holds both locks:
    // Library's files-removed listener calls Remove() (see Engine).
    const auto file = Library::GetInstance().GetCachedFile(fastHash);
    if (!file)
    {
        return;
    }
    const auto durationSeconds = static_cast<double>(file->mediaInfo.durationSec);

    std::lock_guard lock(m_mutex);

    Log::Debug(LogCategory::History, "report {} pos={:.1f} audio={} subtitle={}", fastHash, positionSeconds,
        audioTrackId, subtitleTrackId);

    if (m_pendingResume && m_pendingResume->fastHash == fastHash)
    {
        const bool reached = positionSeconds >=
            m_pendingResume->startPositionSeconds - pendingResumeToleranceSeconds;

        if (!reached && std::chrono::steady_clock::now() < m_pendingResume->deadline)
        {
            return;
        }

        Log::Info(LogCategory::History, "pending resume of {} {} at {:.1f} (resume point {:.1f})", fastHash,
            reached ? "reached" : "timed out", positionSeconds, m_pendingResume->startPositionSeconds);
        m_pendingResume.reset();
    }

    PlaybackRecord updated;
    updated.positionSeconds = positionSeconds >= minimumResumeSeconds ? positionSeconds : 0.0;
    updated.audioTrackId = audioTrackId;
    updated.subtitleTrackId = subtitleTrackId;
    updated.completed =
        durationSeconds > 0 && positionSeconds / durationSeconds >= completedFraction;
    updated.updatedAtUnixSeconds = NowUnixSeconds();

    const auto existing = m_records.find(fastHash);
    const bool isNew = existing == m_records.end();

    if (!isNew)
    {
        const auto& previous = existing->second;

        // A paused player keeps reporting the same spot — nothing to write.
        if (previous.positionSeconds == updated.positionSeconds &&
            previous.audioTrackId == updated.audioTrackId &&
            previous.subtitleTrackId == updated.subtitleTrackId &&
            previous.completed == updated.completed)
        {
            PersistIfDueLocked();
            return;
        }
    }

    const bool completedChanged = !isNew && existing->second.completed != updated.completed;

    m_records.insert_or_assign(fastHash, updated);
    m_dirty = true;
    m_changes.Notify();

    // The first record and a finish are worth having on disk right away;
    // routine position ticks can wait for the next interval.
    if (isNew || completedChanged)
    {
        PersistLocked();
    }
    else
    {
        PersistIfDueLocked();
    }
}

void PlaybackHistory::BeginSession(const std::string& fastHash, double startPositionSeconds)
{
    std::lock_guard lock(m_mutex);

    Log::Info(LogCategory::History, "session {}: {}", fastHash,
        startPositionSeconds > 0 ? std::format("resume from {:.1f}", startPositionSeconds) : "from the start");

    if (startPositionSeconds > 0)
    {
        m_pendingResume = PendingResume{fastHash, startPositionSeconds,
            std::chrono::steady_clock::now() + pendingResumeTimeout};
    }
    else
    {
        m_pendingResume.reset();
    }

    if (m_dirty)
    {
        PersistLocked();
    }
}

void PlaybackHistory::ClearPendingResume()
{
    std::lock_guard lock(m_mutex);
    if (m_pendingResume)
    {
        Log::Info(LogCategory::History, "pending resume of {} ended by a seek", m_pendingResume->fastHash);
    }
    m_pendingResume.reset();
}

void PlaybackHistory::Remove(const std::string& fastHash)
{
    std::lock_guard lock(m_mutex);

    if (m_records.erase(fastHash) > 0)
    {
        m_dirty = true;
        PersistIfDueLocked();
    }
}

void PlaybackHistory::Flush()
{
    std::lock_guard lock(m_mutex);

    if (m_dirty)
    {
        PersistLocked();
    }
}

void PlaybackHistory::PersistLocked()
{
    const auto applicationFolder = File::getApplicationDataPath();
    std::filesystem::create_directories(applicationFolder);

    if (Json::SaveJsonToFile(applicationFolder / playbackHistoryFileName, m_records))
    {
        m_dirty = false;
    }
    else
    {
        Log::Error(LogCategory::History, "could not write {}", playbackHistoryFileName);
    }

    m_lastPersist = std::chrono::steady_clock::now();
}

void PlaybackHistory::PersistIfDueLocked()
{
    if (m_dirty && std::chrono::steady_clock::now() - m_lastPersist >= persistInterval)
    {
        PersistLocked();
    }
}

void PlaybackHistory::SetChangeListener(ChangeListener::Handler handler) { m_changes.Set(std::move(handler)); }
