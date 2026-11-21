#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>

#include "CastWorker.hpp"
#include "SessionBackend.hpp"
#include "SessionThread.hpp"

namespace mango
{

// Why a stream is (re)started — picks where it starts:
//   FreshPlay: the entry's resume point.
//   TrackChange: 0 (a new audio track is a new conversion).
//   SubtitleInPlace: switch subtitles on the TV without reloading when it
//   plays this file; otherwise reload at the TV's position.
enum class StartIntent
{
    FreshPlay,
    TrackChange,
    SubtitleInPlace
};

// One start, with everything decided when it was asked for.
struct StreamRequest
{
    LibraryEntry entry;

    // Only the tracks the user chose; the engine picks the rest (see
    // docs/scope/track-selection-scope.md, section 3). SubtitleInPlace
    // always carries a subtitle.
    TrackRequest tracks;
    StartIntent intent = StartIntent::FreshPlay;
    double startSeconds = 0.0;
    PlaylistType playlist = PlaylistType::DASH;

    // The TV's entry in device-profiles.json (see DeviceProfiles::
    // ChromecastKey); empty uses the default settings.
    std::string deviceKey;

    // Set to switch subtitles on this TV without reloading.
    std::optional<CastDevice> inPlaceDevice;

    // Whether a player gets the stream (a TV or AirPlay): only then does a
    // fresh conversion get its head start.
    bool handsOver = false;
};

struct StreamStarterCallbacks
{
    // Subtitles switched on the TV, nothing else to do. result says which
    // tracks play.
    std::function<void(const StreamRequest&, const StreamInitiation&)> onSwitchedInPlace;

    // The stream is ready to hand to the player.
    std::function<void(const StreamRequest&, const StreamInitiation&)> onReady;

    // No stream started; error as in StreamInitiation::error.
    std::function<void(const StreamRequest&, const std::string& error)> onFailed;

    // IsStarting or StartWait changed.
    std::function<void()> onStateChanged;
};

// The stream start flow: one start at a time. Starts the stream on a
// worker, switches subtitles in place when asked and possible, then gives a
// fresh conversion its head start — up to the configured delay, or until the
// first segments are ready (Engine::IsStreamStartReady). Session thread
// only; the callbacks run there too.
class StreamStarter
{
   public:
    using WaitRange = std::pair<std::chrono::system_clock::time_point, std::chrono::system_clock::time_point>;

    StreamStarter(SessionBackend& backend, SessionThread& thread, CastWorker& worker,
        std::chrono::milliseconds readyPollInterval, StreamStarterCallbacks callbacks);

    // False (nothing happens) while another start is running.
    bool Start(StreamRequest request);

    [[nodiscard]] bool IsStarting() const { return m_starting; }

    // While the head start runs.
    [[nodiscard]] const std::optional<WaitRange>& StartWait() const { return m_wait; }

   private:
    void Initiate(const StreamRequest& request);
    void OnInitiated(const StreamRequest& request, const StreamInitiation& result, const std::string& switchResult);
    void BeginHeadStart(const StreamRequest& request, const StreamInitiation& result);
    void PollHeadStart(const StreamRequest& request, const StreamInitiation& result,
        std::chrono::steady_clock::time_point end);
    void Finish();

    SessionBackend& m_backend;
    SessionThread& m_thread;
    CastWorker& m_worker;
    const std::chrono::milliseconds m_readyPollInterval;
    StreamStarterCallbacks m_callbacks;

    bool m_starting = false;
    std::optional<WaitRange> m_wait;
};

}  // namespace mango
