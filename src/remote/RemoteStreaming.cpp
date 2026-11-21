#include "RemoteStreaming.hpp"

#include <glaze/glaze.hpp>

#include "RemoteCore.hpp"

namespace mango
{

RemoteStreaming::RemoteStreaming(RemoteOptions options)
    : m_core(std::make_unique<remote::RemoteCore>(std::move(options)))
{
}

RemoteStreaming::~RemoteStreaming() = default;

void RemoteStreaming::Start() { m_core->Start(); }

void RemoteStreaming::Shutdown() { m_core->Shutdown(); }

void RemoteStreaming::SetChangedHandler(std::function<void()> handler) { m_core->SetChangedHandler(std::move(handler)); }

void RemoteStreaming::SetNoticeHandler(std::function<void(const RemoteNotice&)> handler)
{
    m_core->SetNoticeHandler(std::move(handler));
}

std::string RemoteStreaming::GetStateJson() const { return m_core->GetStateJson(); }

void RemoteStreaming::SetAppVersion(const std::string& version) { m_core->SetAppVersion(version); }

void RemoteStreaming::SetEnabled(bool enabled) { m_core->SetEnabled(enabled); }

void RemoteStreaming::SetStreamingAllowed(bool allowed) { m_core->SetStreamingAllowed(allowed); }

void RemoteStreaming::SetDeviceName(const std::string& name) { m_core->SetDeviceName(name); }

void RemoteStreaming::OpenInvite() { m_core->OpenInvite(); }

void RemoteStreaming::CancelHostPairing() { m_core->CancelHostPairing(); }

void RemoteStreaming::RemoveClient(const std::string& deviceId) { m_core->RemoveClient(deviceId); }

void RemoteStreaming::PingClient(const std::string& deviceId) { m_core->PingClient(deviceId); }

bool RemoteStreaming::JoinInvite(const std::string& link) { return m_core->JoinInvite(link); }

void RemoteStreaming::CancelClientPairing() { m_core->CancelClientPairing(); }

void RemoteStreaming::ConnectSource(const std::string& deviceId) { m_core->ConnectSource(deviceId); }

void RemoteStreaming::DisconnectSource(const std::string& deviceId) { m_core->DisconnectSource(deviceId); }

void RemoteStreaming::RemoveSource(const std::string& deviceId) { m_core->RemoveSource(deviceId); }

void RemoteStreaming::PingSource(const std::string& deviceId) { m_core->PingSource(deviceId); }

std::string RemoteStreaming::GetLibraryEntriesJson() const
{
    return glz::write_json(m_core->GetLibraryEntries()).value_or("[]");
}

void RemoteStreaming::RefreshLibrary(const std::string& deviceId) { m_core->RefreshLibrary(deviceId); }

void RemoteStreaming::SetLibraryChangedHandler(std::function<void()> handler)
{
    m_core->SetLibraryChangedHandler(std::move(handler));
}

}  // namespace mango
