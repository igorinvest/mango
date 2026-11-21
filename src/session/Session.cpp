#include "Session.hpp"

#include "SessionCore.hpp"

namespace mango
{

Session::Session(std::unique_ptr<SessionBackend> backend, SessionTimings timings)
    : m_core(std::make_unique<SessionCore>(std::move(backend), timings))
{
}

Session::~Session() { Shutdown(); }

void Session::Start()
{
    m_core->Post([core = m_core.get()] { core->Start(); });
}

void Session::Shutdown() { m_core->Shutdown(); }

void Session::SetChangedHandler(SessionChangedHandler handler) { m_core->SetChangedHandler(std::move(handler)); }

std::string Session::GetPartJson(SessionPart part) const { return m_core->GetPartJson(part); }

void Session::ImportAppPreferences(const std::string& lastUsedDeviceId, const std::string& librarySortOrder)
{
    m_core->Post([core = m_core.get(), lastUsedDeviceId, librarySortOrder]
        { core->ImportAppPreferences(lastUsedDeviceId, librarySortOrder); });
}

void Session::AddFile(const std::string& path)
{
    m_core->Post([core = m_core.get(), path] { core->AddFile(path); });
}

void Session::AddFolder(const std::string& path)
{
    m_core->Post([core = m_core.get(), path] { core->AddFolder(path); });
}

void Session::SelectFile()
{
    m_core->Post([core = m_core.get()] { core->SelectFile(); });
}

void Session::SelectFolder()
{
    m_core->Post([core = m_core.get()] { core->SelectFolder(); });
}

void Session::RescanLibraryFolders()
{
    m_core->Post([core = m_core.get()] { core->RescanLibraryFolders(); });
}

void Session::SetSortOrder(const std::string& sortOrder)
{
    m_core->Post([core = m_core.get(), sortOrder] { core->SetSortOrder(sortOrder); });
}

void Session::RefreshDevices()
{
    m_core->Post([core = m_core.get()] { core->RefreshDevices(); });
}

void Session::SelectDevice(const std::string& receiverId)
{
    m_core->Post([core = m_core.get(), receiverId] { core->SelectDevice(receiverId); });
}

void Session::SetAirPlayAvailable(bool available)
{
    m_core->Post([core = m_core.get(), available] { core->SetAirPlayAvailable(available); });
}

void Session::Play(const std::string& fastHash, const std::string& sectionId, std::optional<double> startSeconds,
    const TrackRequest& chosen)
{
    m_core->Post([core = m_core.get(), fastHash, sectionId, startSeconds, chosen]
        { core->Play(fastHash, sectionId, startSeconds, chosen); });
}

void Session::SelectTracks(const std::string& fastHash, int audioTrackId, int subtitleTrackId)
{
    m_core->Post([core = m_core.get(), fastHash, audioTrackId, subtitleTrackId]
        { core->SelectTracks(fastHash, audioTrackId, subtitleTrackId); });
}

void Session::SelectAudioTrack(int audioTrackId)
{
    m_core->Post([core = m_core.get(), audioTrackId] { core->SelectAudioTrack(audioTrackId); });
}

void Session::SelectSubtitleTrack(int subtitleTrackId)
{
    m_core->Post([core = m_core.get(), subtitleTrackId] { core->SelectSubtitleTrack(subtitleTrackId); });
}

void Session::PlayPause()
{
    m_core->Post([core = m_core.get()] { core->PlayPause(); });
}

void Session::Stop()
{
    m_core->Post([core = m_core.get()] { core->Stop(); });
}

void Session::Seek(double positionSeconds)
{
    m_core->Post([core = m_core.get(), positionSeconds] { core->Seek(positionSeconds); });
}

void Session::CancelAutoplay()
{
    m_core->Post([core = m_core.get()] { core->CancelAutoplay(); });
}

void Session::PlayNextNow()
{
    m_core->Post([core = m_core.get()] { core->PlayNextNow(); });
}

void Session::ReportLocalPlayback(double positionSeconds, bool ended)
{
    m_core->Post([core = m_core.get(), positionSeconds, ended] { core->ReportLocalPlayback(positionSeconds, ended); });
}

void Session::LocalPlayerClosed()
{
    m_core->Post([core = m_core.get()] { core->LocalPlayerClosed(); });
}

void Session::SetVideoTranscodeMode(const std::string& mode)
{
    m_core->Post([core = m_core.get(), mode] { core->SetVideoTranscodeMode(mode); });
}

void Session::SetEncodingPreset(const std::string& preset)
{
    m_core->Post([core = m_core.get(), preset] { core->SetEncodingPreset(preset); });
}

void Session::SetAutoplayNextInFolder(bool enabled)
{
    m_core->Post([core = m_core.get(), enabled] { core->SetAutoplayNextInFolder(enabled); });
}

void Session::SetListenOnlyWhileStreaming(bool enabled)
{
    m_core->Post([core = m_core.get(), enabled] { core->SetListenOnlyWhileStreaming(enabled); });
}

void Session::SetLogDebug(bool enabled)
{
    m_core->Post([core = m_core.get(), enabled] { core->SetLogDebug(enabled); });
}

void Session::SetShowSystemNotifications(bool enabled)
{
    m_core->Post([core = m_core.get(), enabled] { core->SetShowSystemNotifications(enabled); });
}

void Session::SetAudioLanguages(const std::vector<std::string>& codes)
{
    m_core->Post([core = m_core.get(), codes] { core->SetAudioLanguages(codes); });
}

void Session::SetSubtitleMode(SubtitleMode mode)
{
    m_core->Post([core = m_core.get(), mode] { core->SetSubtitleMode(mode); });
}

void Session::SetSubtitleLanguages(const std::vector<std::string>& codes)
{
    m_core->Post([core = m_core.get(), codes] { core->SetSubtitleLanguages(codes); });
}

void Session::ForgetFolderTracks()
{
    m_core->Post([core = m_core.get()] { core->ForgetFolderTracks(); });
}

}  // namespace mango
