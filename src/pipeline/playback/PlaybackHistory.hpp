#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "../../configuration/ChangeListener.hpp"

// What's remembered about one library file's playback, keyed by its
// fastHash in PlaybackHistory. A record exists once the file has been
// played at all — that alone means its tracks were a real choice, which is
// what lets "played, and explicitly chose no subtitles" (subtitleTrackId -1)
// win over the language-based default for a file never played.
struct PlaybackRecord
{
    // Where a later Play resumes from. 0 when there's no resume point: the
    // file was only played for a few seconds (see PlaybackHistory::
    // minimumResumeSeconds).
    double positionSeconds = 0.0;

    int audioTrackId = -1;
    int subtitleTrackId = -1;

    // Watched (nearly) to the end — see PlaybackHistory::completedFraction.
    // A completed file restarts from 0 instead of resuming a second before
    // its own end, which would stop playback instantly.
    bool completed = false;

    std::int64_t updatedAtUnixSeconds = 0;

    // Where a fresh Play should start.
    [[nodiscard]] double ResumeStartSeconds() const { return completed ? 0.0 : positionSeconds; }
};

// Owns every file's remembered playback position and tracks, and every rule
// about them — clients only report positions (see Report) and read the
// results back through Engine::GetLibraryEntriesJson. Stored in its own playback_history.json rather than on
// FileRecord, since it changes every couple of seconds while something
// plays and library.json holds everything else about every file.
class PlaybackHistory
{
   public:
    // Below this a report doesn't leave a resume point — a Play that was
    // only on for a moment shouldn't resume 4 seconds in.
    static constexpr double minimumResumeSeconds = 15.0;

    // Watched this far counts as finished. Not the exact end: the last
    // position reported before a stream ends is rarely its last frame.
    static constexpr double completedFraction = 0.95;

    static PlaybackHistory& GetInstance()
    {
        static PlaybackHistory instance;
        return instance;
    }

    PlaybackHistory(const PlaybackHistory&) = delete;
    PlaybackHistory& operator=(const PlaybackHistory&) = delete;

    ~PlaybackHistory();

    [[nodiscard]] std::optional<PlaybackRecord> Get(const std::string& fastHash) const;
    [[nodiscard]] std::unordered_map<std::string, PlaybackRecord> GetAll() const;

    // Records how far into fastHash's file playback reached and which
    // tracks were active (subtitleTrackId -1 for "off"). Meant to be called
    // every couple of seconds while something plays. No-op if fastHash
    // isn't in the library, and while a resume started by BeginSession
    // hasn't landed yet. Writes to disk at most every persistInterval.
    void Report(const std::string& fastHash, double positionSeconds, int audioTrackId,
        int subtitleTrackId);

    // Call when fastHash is about to be loaded starting at
    // startPositionSeconds. Until playback actually reaches it, reports
    // from well before it are dropped: a receiver can report ~0 while it
    // buffers right after a load, which would otherwise wipe out the very
    // position being resumed. Gives up after pendingResumeTimeout, so a
    // receiver that never gets there still records where it really is.
    void BeginSession(const std::string& fastHash, double startPositionSeconds);

    // The user moved playback themselves — whatever is reported next is
    // real, even if it's before the pending resume point.
    void ClearPendingResume();

    // Forgets fastHash's history — for a file evicted from the library.
    void Remove(const std::string& fastHash);

    // Writes any change not yet on disk. Called on shutdown.
    void Flush();

    // Called after every change to a record (it shows in the library's
    // entries), possibly with this class's lock held.
    void SetChangeListener(ChangeListener::Handler handler);

   private:
    static constexpr auto persistInterval = std::chrono::seconds(10);
    static constexpr auto pendingResumeTimeout = std::chrono::seconds(60);

    // How far before the pending resume point still counts as having
    // reached it.
    static constexpr double pendingResumeToleranceSeconds = 10.0;

    struct PendingResume
    {
        std::string fastHash;
        double startPositionSeconds = 0.0;
        std::chrono::steady_clock::time_point deadline;
    };

    PlaybackHistory();

    void Load();

    // Callers must hold m_mutex.
    void PersistLocked();
    void PersistIfDueLocked();

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, PlaybackRecord> m_records;
    std::optional<PendingResume> m_pendingResume;
    bool m_dirty = false;
    std::chrono::steady_clock::time_point m_lastPersist;
    ChangeListener m_changes;
};
