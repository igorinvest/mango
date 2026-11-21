// SessionCore's playback half: starting a stream and handing it to a player,
// cast commands, the TV's status and folder autoplay. Ported from the apps
// (ContentView.swift, MainWindow.xaml.cs); see
// docs/scope/shared-app-session-scope.md.

#include <format>
#include <glaze/glaze.hpp>

#include "SessionCore.hpp"
#include "log/Log.hpp"
#include "pipeline/stream/profiles/DeviceProfiles.hpp"

namespace mango
{

// Each item of LoadCastDevice's subtitleTracksJson. Not in the anonymous
// namespace: glaze's reflection needs a type with linkage.
struct SessionSubtitleTrackRef
{
    int index = 0;
    std::string contentUrl;
};

namespace
{

constexpr auto kDashContentType = "application/dash+xml";

std::string DescribeSubtitle(int index) { return index >= 0 ? std::to_string(index) : "off"; }

// A request's tracks for the log: "auto" for a kind the engine picks.
std::string DescribeTracks(const TrackRequest& tracks)
{
    return std::format("audio={} subtitle={}", tracks.audioTrackId ? std::to_string(*tracks.audioTrackId) : "auto",
        tracks.subtitleTrackId ? DescribeSubtitle(*tracks.subtitleTrackId) : "auto");
}

}  // namespace

// ---- Starting --------------------------------------------------------------

void SessionCore::Play(const std::string& fastHash, const std::string& sectionId,
    std::optional<double> startSeconds, const TrackRequest& chosen)
{
    const auto* entry = FindEntry(fastHash);
    if (entry == nullptr)
    {
        Log::Warning(LogCategory::UI, "Play {}: not in the library", fastHash);
        return;
    }

    const auto tracks = ChosenTracks(fastHash, chosen);
    Log::Info(LogCategory::UI, "Play {} ({}, section {}) {} resume={:.1f}", entry->title, fastHash,
        sectionId.empty() ? "none" : sectionId, DescribeTracks(tracks), entry->resumeStartSeconds);

    // A copy: the library may be refreshed while the stream starts.
    const auto played = *entry;
    if (StartStream(played, tracks, StartIntent::FreshPlay, startSeconds))
    {
        RequestPlaylist(fastHash, sectionId, false);
    }
}

// A card's track menu only switches the stream when that file is already
// what the TV plays; otherwise it's kept as a pending pick for Play, so
// browsing tracks never starts a player. The menu sends both kinds: the one
// that differs from what the card shows is the one picked.
void SessionCore::SelectTracks(const std::string& fastHash, int audioTrackId, int subtitleTrackId)
{
    const auto* entry = FindEntry(fastHash);
    if (entry == nullptr)
    {
        return;
    }

    const auto shown = ShownTracks(*entry);
    if (audioTrackId != shown.audioTrackId)
    {
        m_picks.Pick(fastHash, TrackKind::audio, audioTrackId);
    }
    if (subtitleTrackId != shown.subtitleTrackId)
    {
        m_picks.Pick(fastHash, TrackKind::subtitle, subtitleTrackId);
    }
    PublishLibrary();

    if (!LivePosition(fastHash))
    {
        Log::Info(LogCategory::UI, "tracks remembered for {}, not applied: not playing on the TV", fastHash);
        return;
    }

    // The kind not changed was already in effect: both go as chosen.
    const auto intent = audioTrackId != m_playing.audioTrackId ? StartIntent::TrackChange : StartIntent::SubtitleInPlace;
    StartStream(LibraryEntry(*entry), TrackRequest{audioTrackId, subtitleTrackId}, intent);
}

void SessionCore::SelectAudioTrack(int audioTrackId)
{
    const auto* entry = FindEntry(m_playing.fastHash);
    if (entry == nullptr)
    {
        return;
    }

    Log::Info(LogCategory::UI, "audio track {} → {} for {}, restart from 0", m_playing.audioTrackId, audioTrackId,
        entry->fastHash);
    StartStream(LibraryEntry(*entry), TrackRequest{audioTrackId, m_playing.subtitleTrackId}, StartIntent::TrackChange);
}

void SessionCore::SelectSubtitleTrack(int subtitleTrackId)
{
    const auto* entry = FindEntry(m_playing.fastHash);
    if (entry == nullptr)
    {
        return;
    }

    Log::Info(LogCategory::UI, "subtitle track {} → {} for {}", DescribeSubtitle(m_playing.subtitleTrackId),
        DescribeSubtitle(subtitleTrackId), entry->fastHash);
    StartStream(
        LibraryEntry(*entry), TrackRequest{m_playing.audioTrackId, subtitleTrackId}, StartIntent::SubtitleInPlace);
}

// ---- Track picks -----------------------------------------------------------

TrackRequest SessionCore::ChosenTracks(const std::string& fastHash, const TrackRequest& chosen) const
{
    const auto pending = m_picks.Find(fastHash).value_or(TrackRequest{});
    return TrackRequest{.audioTrackId = chosen.audioTrackId ? chosen.audioTrackId : pending.audioTrackId,
        .subtitleTrackId = chosen.subtitleTrackId ? chosen.subtitleTrackId : pending.subtitleTrackId};
}

TrackChoice SessionCore::ShownTracks(const LibraryEntry& entry) const
{
    const auto pending = m_picks.Find(entry.fastHash).value_or(TrackRequest{});
    return TrackChoice{entry.fastHash, pending.audioTrackId.value_or(entry.defaultAudioTrackId),
        pending.subtitleTrackId.value_or(entry.defaultSubtitleTrackId)};
}

std::vector<TrackChoice> SessionCore::BuildTrackChoices() const
{
    std::vector<TrackChoice> choices;
    for (const auto& [fastHash, picks] : m_picks.All())
    {
        if (const auto* entry = FindEntry(fastHash))
        {
            choices.push_back(ShownTracks(*entry));
        }
    }
    return choices;
}

// ---- Starting, continued -----------------------------------------------------

// Returns false when the request was ignored: another start is running, or
// the server isn't.
bool SessionCore::StartStream(
    const LibraryEntry& entry, const TrackRequest& tracks, StartIntent intent, std::optional<double> startSeconds)
{
    if (m_starter.IsStarting() || !m_backend->GetStatus().isRunning)
    {
        Log::Info(LogCategory::UI, "Play {} ignored: {}", entry.fastHash,
            m_starter.IsStarting() ? "another stream is still starting" : "server not running");
        return false;
    }

    // This same file, from this server, is what the TV plays right now —
    // so a subtitle change can switch in place, or at least resume there.
    const auto live = LivePosition(entry.fastHash);
    const auto device = SelectedCastDevice();
    const bool inPlace = intent == StartIntent::SubtitleInPlace && live && device;

    StreamRequest request{
        .entry = entry,
        .tracks = tracks,
        .intent = intent,
        .startSeconds = startSeconds.value_or(StartPosition(entry, intent, live)),
        .playlist = IsAirPlaySelected() ? PlaylistType::HLS : PlaylistType::DASH,
        .deviceKey = device ? DeviceProfiles::ChromecastKey(device->castId) : "",
        .inPlaceDevice = inPlace ? device : std::nullopt,
        .handsOver = IsAirPlaySelected() || device.has_value(),
    };
    return m_starter.Start(std::move(request));
}

double SessionCore::StartPosition(const LibraryEntry& entry, StartIntent intent, std::optional<double> livePosition) const
{
    switch (intent)
    {
        case StartIntent::TrackChange: return 0.0;
        case StartIntent::SubtitleInPlace: return livePosition.value_or(entry.resumeStartSeconds);
        case StartIntent::FreshPlay: break;
    }
    return entry.resumeStartSeconds;
}

// The TV's position while it plays this server's stream of fastHash.
std::optional<double> SessionCore::LivePosition(const std::string& fastHash) const
{
    const auto& status = m_tracker.Status();
    if (!SelectedCastDevice() || !status || !status->isFromThisServer || !PlaybackTracker::IsControllable(*status) ||
        m_playing.target != "chromecast" || m_playing.fastHash != fastHash)
    {
        return std::nullopt;
    }
    return status->currentTimeSeconds;
}

void SessionCore::OnSwitchedInPlace(const StreamRequest& request, const StreamInitiation& result)
{
    NotePlaying(request, result, "chromecast");

    // So the new track shows without waiting for the next poll.
    m_tracker.QueryNow();
    PublishNowPlaying();
}

void SessionCore::OnStreamReady(const StreamRequest& request, const StreamInitiation& result)
{
    if (request.playlist == PlaylistType::HLS)
    {
        HandOverLocal(request, result);
        return;
    }

    const auto device = SelectedCastDevice();
    NotePlaying(request, result, device ? "chromecast" : "");
    if (device)
    {
        LoadOnTv(*device, request, result);
    }
    PublishNowPlaying();
}

void SessionCore::OnStreamFailed(const StreamRequest& request, const std::string& error)
{
    auto params = TitleParams(request.entry.fastHash);
    params["error"] = error.empty() ? "unknown" : error;
    ShowNotice(NoticeCode::StreamStartFailed, std::move(params), request.entry.fastHash);
    PublishNowPlaying();
    HandleStreamStartFailure(request.entry.fastHash, error);
}

// The stream of request is what plays now: on target, with the tracks the
// engine says it plays. The card's pending picks are done: the engine's
// history has the tracks from now on.
void SessionCore::NotePlaying(const StreamRequest& request, const StreamInitiation& result, const std::string& target)
{
    const auto& fastHash = request.entry.fastHash;
    m_playing = Playing{fastHash, result.audioTrackId, result.subtitleTrackId, target};
    m_autoplay.StreamStarted(fastHash);
    m_picks.Clear(fastHash);
    PublishLibrary();
    PublishAutoplay();
}

void SessionCore::HandOverLocal(const StreamRequest& request, const StreamInitiation& result)
{
    NotePlaying(request, result, "airplay");
    m_local = LocalPlayback{result.serverUrl + "/stream/" + result.streamAddress, request.startSeconds,
        m_local.playbackId + 1, request.startSeconds};
    Log::Info(LogCategory::App, "AirPlay player gets {} from {:.1f}", m_local.url, request.startSeconds);
    PublishNowPlaying();
}

void SessionCore::LoadOnTv(const CastDevice& device, const StreamRequest& request, const StreamInitiation& result)
{
    if (m_loading)
    {
        Log::Info(LogCategory::UI, "LOAD of {} ignored: another one is in flight", request.entry.fastHash);
        return;
    }
    m_loading = true;

    const CastLoadRequest load{
        .contentUrl = result.serverUrl + "/stream/" + result.streamAddress,
        .contentType = kDashContentType,
        .title = request.entry.title,
        .fastHash = request.entry.fastHash,
        .subtitleTracksJson = SubtitleTracksJson(request.entry, result),
        .activeSubtitleTrackIndex = result.subtitleTrackId,
        .startTimeSeconds = request.startSeconds,
    };

    // Up to ~6 s for a cold receiver launch.
    m_commandWorker.Run(
        [this, device, load]
        {
            const auto answer = m_backend->Load(device, load);
            Post([this, answer] { OnLoaded(answer); });
        });
}

void SessionCore::OnLoaded(const std::string& result)
{
    m_loading = false;
    if (result == "Unreachable")
    {
        ShowNotice(NoticeCode::DeviceUnreachable, {{"device", SelectedDeviceName()}}, m_playing.fastHash);
    }

    // So the new session shows without waiting for the next poll.
    m_tracker.QueryNow();
    PublishNowPlaying();
}

// Every subtitle track the file has, so a later switch can happen in place —
// the engine declares only the ones it can serve (see
// Engine::LoadCastDevice).
std::string SessionCore::SubtitleTracksJson(const LibraryEntry& entry, const StreamInitiation& result)
{
    std::vector<SessionSubtitleTrackRef> tracks;
    for (const auto& track : entry.subtitleTracks)
    {
        tracks.push_back({track.index, std::format("{}/stream/{}/{}.vtt", result.serverUrl, result.streamId, track.index)});
    }
    return glz::write_json(tracks).value_or("[]");
}

// ---- Cast commands ---------------------------------------------------------

void SessionCore::PlayPause()
{
    const auto device = SelectedCastDevice();
    const auto& status = m_tracker.Status();
    if (!device || !status || m_commandInFlight)
    {
        Log::Info(LogCategory::UI, "Play/Pause ignored: {}", status ? CommandBlocker() : "no TV status yet");
        return;
    }

    const bool shouldPause = status->state == "Playing";
    Log::Info(LogCategory::UI, "{} on {} (TV state {})", shouldPause ? "Pause" : "Play", device->name, status->state);
    RunCommand(*device,
        [this, shouldPause](const CastDevice& target)
        { return shouldPause ? m_backend->Pause(target) : m_backend->Play(target); });
}

void SessionCore::Stop()
{
    const auto device = SelectedCastDevice();
    if (!device || m_commandInFlight)
    {
        Log::Info(LogCategory::UI, "Stop ignored: {}", CommandBlocker());
        return;
    }

    const auto& status = m_tracker.Status();
    Log::Info(LogCategory::UI, "Stop on {} at {:.1f} ({})", device->name, status ? status->currentTimeSeconds : 0.0,
        m_playing.fastHash.empty() ? "nothing playing" : m_playing.fastHash);

    // Before the command, so the IDLE it causes is never taken as the end
    // of the video (and never starts the next one).
    if (!m_playing.fastHash.empty())
    {
        m_backend->NotifyStoppedByUser(m_playing.fastHash);
    }
    CancelCountdown();

    RunCommand(*device, [this](const CastDevice& target) { return m_backend->Stop(target); });
}

void SessionCore::Seek(double positionSeconds)
{
    const auto device = SelectedCastDevice();
    if (!device)
    {
        Log::Info(LogCategory::UI, "Seek to {:.1f} ignored: {}", positionSeconds, CommandBlocker());
        return;
    }

    // Dragging the slider must not drop the final position: the latest
    // seek goes out once the command in flight is done.
    if (m_commandInFlight)
    {
        Log::Info(LogCategory::UI, "Seek to {:.1f} queued: a cast command is in flight", positionSeconds);
        m_pendingSeek = positionSeconds;
        return;
    }
    RunSeek(*device, positionSeconds);
}

void SessionCore::RunSeek(const CastDevice& device, double positionSeconds)
{
    const auto& status = m_tracker.Status();
    const auto* entry = FindEntry(m_playing.fastHash);
    Log::Info(LogCategory::UI, "Seek {:.1f} → {:.1f} (TV duration {:.1f}, library duration {:.1f})",
        status ? status->currentTimeSeconds : 0.0, positionSeconds, status ? status->durationSeconds : 0.0,
        entry ? entry->durationSeconds : 0.0);

    const auto playing = m_playing;
    RunCommand(device,
        [this, positionSeconds, playing](const CastDevice& target)
        {
            const auto result = m_backend->Seek(target, positionSeconds);

            // A seek to the very end can go IDLE/FINISHED before a poll
            // ever reports a position near it, and the end would then be
            // judged from the spot before the seek. A receiver still playing
            // overwrites this with where it really landed.
            if (result == "Success" && !playing.fastHash.empty())
            {
                m_backend->ReportPlaybackProgress(
                    playing.fastHash, positionSeconds, playing.audioTrackId, playing.subtitleTrackId);
            }
            return result;
        });
}

void SessionCore::RunCommand(const CastDevice& device, std::function<std::string(const CastDevice&)> send)
{
    m_commandInFlight = true;
    PublishNowPlaying();

    m_commandWorker.Run(
        [this, device, send = std::move(send)]
        {
            const auto result = send(device);
            Post([this, device, result] { OnCommandDone(device, result); });
        });
}

void SessionCore::OnCommandDone(const CastDevice& device, const std::string& result)
{
    m_commandInFlight = false;
    if (result == "Unreachable")
    {
        ShowNotice(NoticeCode::DeviceUnreachable, {{"device", SelectedDeviceName()}}, m_playing.fastHash);
    }

    const auto selected = SelectedCastDevice();
    if (m_pendingSeek && selected && selected->ipAddress == device.ipAddress && selected->port == device.port)
    {
        const auto position = *m_pendingSeek;
        m_pendingSeek.reset();
        RunSeek(*selected, position);
        return;
    }
    m_pendingSeek.reset();

    // So the UI shows the command's effect without waiting for the next poll.
    m_tracker.QueryNow();
    PublishNowPlaying();
}

std::string SessionCore::CommandBlocker() const
{
    if (!SelectedCastDevice())
    {
        return "no Chromecast selected";
    }
    return m_commandInFlight ? "a cast command is in flight" : "";
}

// ---- The TV's status and the end of playback -------------------------------

PollContext SessionCore::MakePollContext() const
{
    PollContext context;
    context.token = m_autoplay.Token();

    if (m_playing.target == "chromecast" && !m_playing.fastHash.empty())
    {
        context.progress = TrackChoice{m_playing.fastHash, m_playing.audioTrackId, m_playing.subtitleTrackId};

        // While a stream is starting or loading, the receiver still reports
        // the previous session: nothing to judge an end by.
        if (!m_starter.IsStarting() && !m_loading)
        {
            context.judgedFastHash = m_playing.fastHash;
        }
    }
    return context;
}

void SessionCore::OnPolledStatus(const PolledStatus& polled)
{
    RestoreFromTv(polled.status);
    if (!polled.context.judgedFastHash.empty())
    {
        OnEndVerdict(polled.endKind, polled.context.judgedFastHash, polled.context.token);
    }
    PublishNowPlaying();
}

// After a relaunch the TV may still be playing (or paused on) a file this
// server streamed: take it back as what plays, and its folder as the
// playlist, so progress is reported again and its end can start the next
// one. The real tracks aren't known; the entry's defaults (its history)
// are assumed.
void SessionCore::RestoreFromTv(const CastPlaybackStatus& status)
{
    if (!m_playing.fastHash.empty() || m_starter.IsStarting() || m_loading || !status.isFromThisServer ||
        !PlaybackTracker::IsControllable(status))
    {
        return;
    }
    const auto* entry = FindEntry(status.fastHash);
    if (entry == nullptr)
    {
        return;
    }

    const TrackChoice tracks{entry->fastHash, entry->defaultAudioTrackId, entry->defaultSubtitleTrackId};
    m_playing = Playing{entry->fastHash, tracks.audioTrackId, tracks.subtitleTrackId, "chromecast"};

    const auto section = std::ranges::find_if(m_library.sections, [&](const LibrarySection& candidate)
        { return candidate.kind == "folder" && std::ranges::contains(candidate.fastHashes, entry->fastHash); });
    Log::Info(LogCategory::App, "restored now playing from the TV: {} ({}) {} at {:.1f}, tracks assumed audio={} subtitle={}",
        entry->title, entry->fastHash, status.state, status.currentTimeSeconds, tracks.audioTrackId,
        DescribeSubtitle(tracks.subtitleTrackId));
    RequestPlaylist(entry->fastHash, section == m_library.sections.end() ? "" : section->sectionId, false);
}

// The first Finished or Stopped after the video was seen playing ends it;
// only Finished leads to the next video.
void SessionCore::ShowEndNotice(const std::string& endKind, const std::string& fastHash)
{
    auto params = TitleParams(fastHash);
    if (endKind == "Finished")
    {
        ShowNotice(NoticeCode::PlaybackFinished, std::move(params), fastHash);
        return;
    }
    if (const auto device = SelectedDeviceName(); !device.empty() && m_playing.target == "chromecast")
    {
        params["device"] = device;
    }
    ShowNotice(NoticeCode::PlaybackStopped, std::move(params), fastHash);
}

void SessionCore::OnEndVerdict(const std::string& endKind, const std::string& fastHash, int token)
{
    if (endKind == "Playing")
    {
        m_autoplay.NotePlaying(fastHash, token);
        return;
    }
    if (endKind != "Finished" && endKind != "Stopped")
    {
        return;
    }

    // Every poll after an end answers the same, hence Debug.
    if (!m_autoplay.AwaitsEnd(fastHash, token))
    {
        Log::Debug(LogCategory::Playlist, "{} {} ignored: {}", endKind, fastHash, m_autoplay.DescribeEndState(fastHash, token));
        return;
    }
    m_autoplay.MarkEndHandled();
    Log::Info(LogCategory::Playlist, "{} {}: end handled", endKind, fastHash);
    ShowEndNotice(endKind, fastHash);

    if (endKind == "Finished")
    {
        ScheduleNext(fastHash);
    }
}

void SessionCore::ReportLocalPlayback(double positionSeconds, bool ended)
{
    if (m_playing.target != "airplay" || m_playing.fastHash.empty())
    {
        return;
    }

    const auto playing = m_playing;
    const int token = m_autoplay.Token();
    m_local.positionSeconds = positionSeconds;

    if (!ended)
    {
        m_autoplay.NotePlaying(playing.fastHash, token);
        m_statusWorker.Run(
            [this, playing, positionSeconds]
            {
                m_backend->ReportPlaybackProgress(
                    playing.fastHash, positionSeconds, playing.audioTrackId, playing.subtitleTrackId);
            });
        PublishNowPlaying();
        return;
    }

    Log::Info(LogCategory::UI, "AirPlay: played to end at {:.1f}", positionSeconds);
    // AVPlayer also "ends" at the end of a conversion that died halfway:
    // the engine judges by the position.
    m_statusWorker.Run(
        [this, playing, positionSeconds, token]
        {
            const auto endKind = m_backend->ClassifyLocalEnd(playing.fastHash, positionSeconds, "Ended");
            Post([this, endKind, fastHash = playing.fastHash, token] { OnEndVerdict(endKind, fastHash, token); });
        });
}

void SessionCore::LocalPlayerClosed()
{
    Log::Info(LogCategory::UI, "AirPlay player closed ({})", m_playing.fastHash.empty() ? "none" : m_playing.fastHash);
    m_local.url.clear();
    CancelCountdown();
    PublishNowPlaying();
}

// ---- Autoplay --------------------------------------------------------------

void SessionCore::RequestPlaylist(const std::string& fastHash, const std::string& sectionId, bool isAutoplay)
{
    m_thread.Cancel(m_countdownTimer);
    m_countdownTimer = 0;
    m_autoplay.Request(fastHash, sectionId, isAutoplay);
    PublishAutoplay();
}

// With autoplay on, counts down to the next video; with it off nothing
// happens — Next still works.
void SessionCore::ScheduleNext(const std::string& fastHash)
{
    if (!m_settings.autoplayNextInFolder)
    {
        Log::Info(LogCategory::Playlist, "no next video after {}: autoplay off", fastHash);
        return;
    }

    const auto next = NextFastHash(fastHash);
    const auto* entry = next ? FindEntry(*next) : nullptr;
    if (entry == nullptr)
    {
        Log::Info(LogCategory::Playlist, "no next video after {}: {}", fastHash, NoNextReason());
        return;
    }

    m_autoplay.StartCountdown(
        AutoplayCountdown{entry->fastHash, entry->title, std::chrono::system_clock::now() + m_timings.autoplayCountdown});
    m_countdownTimer = m_thread.PostAfter(
        m_timings.autoplayCountdown, [this, nextFastHash = entry->fastHash] { OnCountdownFired(nextFastHash); });
    PublishAutoplay();
}

void SessionCore::OnCountdownFired(const std::string& nextFastHash)
{
    m_countdownTimer = 0;
    const auto& countdown = m_autoplay.Countdown();
    if (!countdown || countdown->nextFastHash != nextFastHash)
    {
        return;
    }

    m_autoplay.CancelCountdown();
    Log::Info(LogCategory::Playlist, "countdown fired → {}", nextFastHash);
    Autoplay(nextFastHash);
    PublishAutoplay();
}

void SessionCore::CancelAutoplay()
{
    Log::Info(LogCategory::UI, "countdown Cancel");
    CancelCountdown();
}

// Cancel: the chain stops here. Nothing else changes, so Next still works.
void SessionCore::CancelCountdown()
{
    m_thread.Cancel(m_countdownTimer);
    m_countdownTimer = 0;

    if (const auto cancelled = m_autoplay.CancelCountdown())
    {
        const auto left = std::chrono::duration<double>(cancelled->deadline - std::chrono::system_clock::now());
        Log::Info(LogCategory::Playlist, "countdown → {} cancelled, {:.1f}s left", cancelled->nextFastHash,
            std::max(0.0, left.count()));
    }
    PublishAutoplay();
}

// Next, or Now on the countdown.
void SessionCore::PlayNextNow()
{
    const auto& countdown = m_autoplay.Countdown();
    const auto& current = m_autoplay.CurrentFastHash();
    const auto next = countdown ? std::optional(countdown->nextFastHash) : NextFastHash(current);

    Log::Info(LogCategory::UI, "Next: {} → {}", current.empty() ? "none" : current,
        next ? *next : "no next (" + NoNextReason() + ")");
    CancelCountdown();
    if (next)
    {
        Autoplay(*next);
    }
}

// Starts fastHash like its card's Play: its pending picks (usually none —
// the engine's folder memory carries the season's choices) and the resume
// point, looked up now, since it may have changed while the previous video
// played.
void SessionCore::Autoplay(const std::string& fastHash)
{
    const auto* entry = FindEntry(fastHash);
    if (entry == nullptr)
    {
        Log::Warning(LogCategory::Playlist, "autoplay {}: no longer in the library", fastHash);
        return;
    }

    const auto sectionId = m_autoplay.SectionId();
    if (StartStream(LibraryEntry(*entry), ChosenTracks(fastHash), StartIntent::FreshPlay))
    {
        RequestPlaylist(fastHash, sectionId, true);
    }
}

// A video autoplay picked couldn't start: one that's gone from disk (an
// unplugged drive since the last scan) is skipped; any other error ends the
// chain.
void SessionCore::HandleStreamStartFailure(const std::string& fastHash, const std::string& error)
{
    if (!m_autoplay.IsAutoStarted(fastHash) || error != "fileUnreadable" || !m_autoplay.NoteUnreadable())
    {
        return;
    }
    if (const auto next = NextFastHash(fastHash))
    {
        Autoplay(*next);
    }
}

}  // namespace mango
