#pragma once

#include <mutex>
#include <optional>
#include <string>

// What a player said about the media it was playing — player-neutral, so
// Chromecast (see FromCastStatus) and a local AVPlayer go through the same
// end-of-playback rule.
enum class PlayerEndReason
{
    StillPlaying,  // playing, paused or buffering this server's stream
    Ended,         // the player reached the end of the media (IDLE/FINISHED, AVPlayer's end)
    Cancelled,     // stopped by someone (IDLE/CANCELLED, a Stop on the TV remote)
    Interrupted,   // another LOAD replaced it (IDLE/INTERRUPTED)
    Error,         // the player gave up (IDLE/ERROR)
    SessionGone,   // the media session disappeared without saying why
    OtherContent,  // the receiver now plays something this server didn't send
    Unknown,       // nothing useful: unreachable, or a state not listed above
};

struct PlayerEnd
{
    PlayerEndReason reason = PlayerEndReason::Unknown;

    // Where the player says it is, when it says so. The rule also uses the
    // last position the app reported (PlaybackHistory) — a receiver whose
    // session is gone reports none.
    std::optional<double> positionSeconds;
};

enum class PlaybackEndKind
{
    Playing,
    Finished,  // watched to the end — the app may start the next video
    Stopped,   // ended any other way — never start the next video
    Unknown,
};

// Decides whether playback of a file "finished" or "stopped", for the
// apps' Play-next-in-folder. Both apps ask the engine, so the end-window
// rule exists once. Holds only the "stopped by the user" mark; positions
// come from PlaybackHistory and durations from the Library.
class PlaybackEndDetector
{
   public:
    static PlaybackEndDetector& GetInstance()
    {
        static PlaybackEndDetector instance;
        return instance;
    }

    PlaybackEndDetector(const PlaybackEndDetector&) = delete;
    PlaybackEndDetector& operator=(const PlaybackEndDetector&) = delete;

    // How close to the end (from ffprobe's duration) the last position has
    // to be for an end to count as Finished: max(60 s, 3%).
    static constexpr double endWindowMinimumSeconds = 60.0;
    static constexpr double endWindowFraction = 0.03;

    // Classifies fastHash's playback, using its library duration and last
    // reported position.
    PlaybackEndKind Classify(const std::string& fastHash, const PlayerEnd& end) const;

    // The rule itself, with every input passed in — Classify's core, kept
    // free of Library/PlaybackHistory so it can be tested on its own.
    // durationSeconds <= 0 means unknown: the player's word is trusted then.
    static PlaybackEndKind Decide(const PlayerEnd& end, double durationSeconds,
        std::optional<double> lastReportedSeconds, bool stoppedByUser);

    static bool IsInEndWindow(double positionSeconds, double durationSeconds);

    // A Chromecast status (Engine's state names, plus MEDIA_STATUS's
    // idleReason) as a PlayerEnd. isFromThisServer is nullopt when the
    // status didn't say which media plays — Unknown, not OtherContent.
    static PlayerEnd FromCastStatus(const std::string& state, const std::string& idleReason,
        std::optional<bool> isFromThisServer, double currentTimeSeconds);

    // endReason "Ended"/"Stopped"/"Error" from a local player.
    static PlayerEnd FromLocalEnd(const std::string& endReason, double positionSeconds);

    // The app's Stop button: whatever the player says next about fastHash
    // is Stopped, until BeginSession.
    void NotifyStoppedByUser(const std::string& fastHash);

    // A new stream of fastHash is starting — forgets its Stop mark.
    void BeginSession(const std::string& fastHash);

    static std::string ToString(PlaybackEndKind kind);

   private:
    PlaybackEndDetector() = default;

    bool IsStoppedByUser(const std::string& fastHash) const;

    // The verdict with its inputs: Info for the first non-Playing one,
    // Debug for every call.
    void LogVerdict(
        const std::string& fastHash, const PlayerEnd& end, PlaybackEndKind kind, const std::string& inputs) const;

    mutable std::mutex m_mutex;
    std::optional<std::string> m_stoppedByUserFastHash;
    mutable std::optional<std::string> m_lastLoggedVerdict;
};
