#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace mango
{

// The few seconds between one video's end and the next one's start.
struct AutoplayCountdown
{
    std::string nextFastHash;
    std::string nextTitle;
    std::chrono::system_clock::time_point deadline;
};

// "Folder as playlist" (docs/implemented/folder-playlist-scope.md): which
// section the current video was started from, whether its end was already
// handled, and the countdown to the next one. Doesn't decide what
// "finished" means — the engine does (Engine::ClassifyCastPlaybackEnd) —
// nor what's next (LibrarySections::NextFastHash), nor when the countdown
// fires (the session's timer). Session thread only.
class FolderAutoplay
{
   public:
    // Unreadable files skipped in a row before the chain gives up — a whole
    // unplugged drive shouldn't be walked file by file.
    static constexpr int maxUnreadableInARow = 3;

    // Play (isAutoplay false, sectionId from the card) or autoplay
    // (isAutoplay true, the same section) asked for fastHash. "manual" and
    // "" mean no playlist.
    void Request(const std::string& fastHash, const std::string& sectionId, bool isAutoplay);

    // A stream of fastHash was handed to a player — including a reload of
    // the same video for a track change, whose old session's IDLE must not
    // count as this video's end.
    void StreamStarted(const std::string& fastHash);

    void NotePlaying(const std::string& fastHash, int token);

    // Whether an end reported for fastHash still needs handling: at most
    // once per stream start, and only once it was seen playing — before
    // that, an IDLE belongs to the previous session or a load in progress.
    [[nodiscard]] bool AwaitsEnd(const std::string& fastHash, int token) const;

    // The first Finished/Stopped answer wins: a TV-remote Stop near the end
    // must not turn into Finished once the receiver then drops the session.
    void MarkEndHandled();

    // Why AwaitsEnd answered as it did, for the log.
    [[nodiscard]] std::string DescribeEndState(const std::string& fastHash, int token) const;

    [[nodiscard]] bool IsAutoStarted(const std::string& fastHash) const;

    // Counts an unreadable autoplay pick; false once the chain should stop.
    bool NoteUnreadable();

    void StartCountdown(AutoplayCountdown countdown);

    // Returns the countdown that was running, if any.
    std::optional<AutoplayCountdown> CancelCountdown();

    [[nodiscard]] const std::optional<AutoplayCountdown>& Countdown() const { return m_countdown; }

    // "" when the current video has no playlist.
    [[nodiscard]] const std::string& SectionId() const { return m_sectionId; }
    [[nodiscard]] const std::string& CurrentFastHash() const { return m_currentFastHash; }

    // Changes whenever a stream is requested or handed to a player. A
    // status taken under an older token may still describe the previous
    // session, so it isn't attributed to the new one.
    [[nodiscard]] int Token() const { return m_token; }

   private:
    void ResetEnd();

    std::string m_sectionId;
    std::string m_currentFastHash;
    std::optional<AutoplayCountdown> m_countdown;
    int m_token = 0;
    bool m_hasSeenPlaying = false;
    bool m_endHandled = false;
    std::string m_autoStartedFastHash;
    int m_unreadableInARow = 0;
};

}  // namespace mango
