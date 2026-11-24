#include "RemoteCore.hpp"

#include <future>

#include <glaze/glaze.hpp>

#include "../configuration/Settings.hpp"
#include "../file/File.hpp"
#include "../log/Log.hpp"
#include "../platform/HostName.hpp"
#include "link/HelloMessage.hpp"

namespace mango::remote
{

namespace
{

constexpr auto kEnabledKey = "remoteStreamingEnabled";
constexpr auto kDeviceNameKey = "remoteDeviceName";
constexpr auto kSignalingUrlKey = "remoteSignalingUrl";
constexpr auto kAllowedKey = "remoteStreamingAllowed";

std::filesystem::path RemoteFolder(const RemoteOptions& options)
{
    return options.folder.empty() ? File::getApplicationDataPath() / "remote" : options.folder;
}

#ifndef MANGO_SIGNALING_URL
#define MANGO_SIGNALING_URL ""
#endif

// The build's default service (-DMANGO_SIGNALING_URL in CMake); may be empty.
constexpr auto kBuildSignalingUrl = MANGO_SIGNALING_URL;

// The options' URL, else settings.json's, else the build's. Empty: none
// configured. Without settings (tests) only the options' counts.
std::string SignalingUrl(const RemoteOptions& options)
{
    if (!options.signalingUrl.empty() || !options.useSettings)
    {
        return options.signalingUrl;
    }
    const auto configured = Settings::GetStringSetting(kSignalingUrlKey).value_or("");
    return configured.empty() ? kBuildSignalingUrl : configured;
}

std::string SignalingState(const SignalingClient* signaling)
{
    if (!signaling)
    {
        return "off";
    }
    switch (signaling->GetState())
    {
        case SignalingClient::State::Ready: return "ready";
        case SignalingClient::State::Connecting: return "connecting";
        case SignalingClient::State::Stopped: break;
    }
    return "off";
}

RemoteNotice PairedNotice(const std::string& peerName)
{
    RemoteNotice notice;
    notice.code = "paired";
    notice.peer = peerName;
    return notice;
}

}  // namespace

// The engine's version until the app sets its own (the CLI never does).
RemoteCore::RemoteCore(RemoteOptions options)
    : m_options(std::move(options)),
      m_host(m_thread, HostCallbacks()),
      m_client(m_thread, RemoteFolder(m_options) / "covers", ClientCallbacks()),
      m_router(m_host, m_client),
      m_appVersion(MANGO_ENGINE_VERSION)
{
    m_stateJson = glz::write_json(RemoteState{}).value_or("{}");
}

RemoteCore::~RemoteCore() { Shutdown(); }

// Waits for the load (a small file, or making a key), so the state has
// this device's id and name once Start returns.
void RemoteCore::Start()
{
    if (m_shutDown)
    {
        return;
    }

    std::promise<void> loaded;
    Post([this, &loaded] {
        if (!m_started)
        {
            m_started = true;
            Load();
            Publish();
        }
        loaded.set_value();
    });
    loaded.get_future().wait();
}

// Everything libdatachannel holds is closed and destroyed on the thread,
// before the thread itself stops.
void RemoteCore::Shutdown()
{
    if (m_shutDown)
    {
        return;
    }
    m_shutDown = true;

    std::promise<void> done;
    m_thread.Post([this, &done] {
        m_host.Shutdown();
        m_client.Shutdown();
        m_signaling.reset();
        done.set_value();
    });
    done.get_future().wait();
    m_thread.Stop();
}

void RemoteCore::SetChangedHandler(std::function<void()> handler)
{
    std::lock_guard lock(m_stateMutex);
    m_handler = std::move(handler);
}

void RemoteCore::SetNoticeHandler(std::function<void(const RemoteNotice&)> handler)
{
    std::lock_guard lock(m_stateMutex);
    m_noticeHandler = std::move(handler);
}

std::string RemoteCore::GetStateJson() const
{
    std::lock_guard lock(m_stateMutex);
    return m_stateJson;
}

// ---- Shared settings ----------------------------------------------------------

void RemoteCore::SetAppVersion(const std::string& version)
{
    Post([this, version] {
        m_appVersion = version;
        m_host.SetAppVersion(version);
        m_client.SetAppVersion(version);
    });
}

void RemoteCore::SetEnabled(bool enabled)
{
    Post([this, enabled] {
        if (m_options.useSettings)
        {
            Settings::SetBoolSetting(kEnabledKey, enabled);
        }
        ApplyEnabled(enabled);
        Publish();
    });
}

void RemoteCore::SetStreamingAllowed(bool allowed)
{
    Post([this, allowed] {
        ApplyStreamingAllowed(allowed);
        Publish();
    });
}

void RemoteCore::SetDeviceName(const std::string& name)
{
    Post([this, name] {
        m_deviceName = name.empty() ? HostName() : name;
        if (m_options.useSettings)
        {
            Settings::SetStringSetting(kDeviceNameKey, name.empty() ? std::nullopt : std::optional(name));
        }
        m_host.SetDeviceName(m_deviceName);
        m_client.SetDeviceName(m_deviceName);
        Publish();
    });
}

// ---- Host ---------------------------------------------------------------------

void RemoteCore::OpenInvite()
{
    Post([this] {
        Enable();
        m_host.OpenInvite();
    });
}

void RemoteCore::CancelHostPairing()
{
    Post([this] { m_host.CancelPairing(); });
}

void RemoteCore::RemoveClient(const std::string& deviceId)
{
    Post([this, deviceId] { m_host.Remove(deviceId); });
}

void RemoteCore::PingClient(const std::string& deviceId)
{
    Post([this, deviceId] { m_host.Ping(deviceId); });
}

// ---- Client -------------------------------------------------------------------

bool RemoteCore::JoinInvite(const std::string& link)
{
    if (!PairingProof::ParseLink(link))
    {
        return false;
    }

    Post([this, link] {
        Enable();
        m_client.JoinInvite(link);
    });
    return true;
}

void RemoteCore::CancelClientPairing()
{
    Post([this] { m_client.CancelPairing(); });
}

void RemoteCore::ConnectSource(const std::string& deviceId)
{
    Post([this, deviceId] { m_client.Connect(deviceId); });
}

void RemoteCore::DisconnectSource(const std::string& deviceId)
{
    Post([this, deviceId] { m_client.Disconnect(deviceId); });
}

void RemoteCore::RemoveSource(const std::string& deviceId)
{
    Post([this, deviceId] { m_client.Remove(deviceId); });
}

void RemoteCore::PingSource(const std::string& deviceId)
{
    Post([this, deviceId] { m_client.Ping(deviceId); });
}

void RemoteCore::RefreshLibrary(const std::string& deviceId)
{
    Post([this, deviceId] { m_client.RefreshLibrary(deviceId); });
}

// ---- Setup --------------------------------------------------------------------

void RemoteCore::Load()
{
    m_identity = DeviceIdentity::LoadOrCreate(RemoteFolder(m_options) / "identity.json");
    if (!m_identity)
    {
        return;
    }

    if (m_options.useSettings)
    {
        m_deviceName = Settings::GetStringSetting(kDeviceNameKey).value_or("");
        m_enabled = Settings::GetBoolSetting(kEnabledKey).value_or(false);
        m_streamingAllowed = Settings::GetBoolSetting(kAllowedKey).value_or(true);
    }
    if (m_deviceName.empty())
    {
        m_deviceName = HostName();
    }

    CreateSignaling();
    LoadModules();
    Log::Info(LogCategory::Remote, "device {} \"{}\", {} clients, {} sources, remote streaming {}",
        m_identity->DeviceId(), m_deviceName, m_host.Count(), m_client.Count(), m_enabled ? "on" : "off");

    if (m_enabled)
    {
        m_signaling->Start();
    }
}

// The one socket: the service closes a second one for the same device.
void RemoteCore::CreateSignaling()
{
    m_signaling = std::make_unique<SignalingClient>(*m_identity, m_thread, SignalingUrl(m_options),
        SignalingClient::Handlers{
            .onReady = [this] {
                m_router.OnReady();
                Publish();
            },
            .onDisconnected = [this] {
                m_router.OnLost();
                Publish();
            },
            .onMessage = [this](const std::string& type, const std::string& text) { m_router.OnMessage(type, text); },
        });
}

void RemoteCore::LoadModules()
{
    const auto folder = RemoteFolder(m_options);
    m_host.Load({.identity = *m_identity,
                    .signaling = *m_signaling,
                    .storePath = folder / "clients.json",
                    .deviceName = m_deviceName,
                    .appVersion = m_appVersion},
        m_streamingAllowed);
    m_client.Load({.identity = *m_identity,
        .signaling = *m_signaling,
        .storePath = folder / "sources.json",
        .deviceName = m_deviceName,
        .appVersion = m_appVersion});
}

void RemoteCore::ApplyEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!m_signaling)
    {
        return;
    }

    Log::Info(LogCategory::Remote, "remote streaming {}", enabled ? "on" : "off");
    if (enabled)
    {
        m_signaling->Start();
        return;
    }
    m_host.CloseAll();
    m_client.CloseAll();
    m_signaling->Stop();
}

// Saved in settings.json; the clients' links close when it goes off.
void RemoteCore::ApplyStreamingAllowed(bool allowed)
{
    if (allowed == m_streamingAllowed)
    {
        return;
    }
    m_streamingAllowed = allowed;
    if (m_options.useSettings)
    {
        Settings::SetBoolSetting(kAllowedKey, allowed);
    }
    Log::Info(LogCategory::Remote, "remote streaming {}", allowed ? "allowed" : "not allowed");
    m_host.SetStreamingAllowed(allowed);
}

// Pairing needs the service: asking to pair turns remote streaming on.
void RemoteCore::Enable()
{
    if (!m_enabled)
    {
        if (m_options.useSettings)
        {
            Settings::SetBoolSetting(kEnabledKey, true);
        }
        ApplyEnabled(true);
        Publish();
    }
}

// A new client means streaming to it is wanted.
void RemoteCore::OnHostPaired(const std::string& deviceId)
{
    ApplyStreamingAllowed(true);
    ShowNotice(PairedNotice(m_host.NameOf(deviceId)));
}

ModuleCallbacks RemoteCore::HostCallbacks()
{
    return {
        .onChanged = [this] { Publish(); },
        .onNotice = [this](RemoteNotice notice) { ShowNotice(std::move(notice)); },
        .onPaired = [this](const std::string& deviceId) { OnHostPaired(deviceId); },
    };
}

ModuleCallbacks RemoteCore::ClientCallbacks()
{
    return {
        .onChanged = [this] { Publish(); },
        .onNotice = [this](RemoteNotice notice) { ShowNotice(std::move(notice)); },
        .onPaired = [this](const std::string& deviceId) {
            ShowNotice(PairedNotice(m_client.NameOf(deviceId)));
        },
    };
}

// ---- State --------------------------------------------------------------------

void RemoteCore::ShowNotice(RemoteNotice notice)
{
    notice.noticeId = m_notice.noticeId + 1;
    m_notice = std::move(notice);
    Publish();

    std::function<void(const RemoteNotice&)> handler;
    {
        std::lock_guard lock(m_stateMutex);
        handler = m_noticeHandler;
    }
    if (handler)
    {
        handler(m_notice);
    }
}

void RemoteCore::Publish()
{
    auto json = glz::write_json(BuildState()).value_or("{}");

    std::function<void()> handler;
    {
        std::lock_guard lock(m_stateMutex);
        if (json == m_stateJson)
        {
            return;
        }
        m_stateJson = std::move(json);
        handler = m_handler;
    }

    if (handler)
    {
        handler();
    }
}

RemoteState RemoteCore::BuildState() const
{
    return {.enabled = m_enabled,
        .streamingAllowed = m_streamingAllowed,
        .available = m_identity.has_value() || !m_started,
        .signaling = SignalingState(m_signaling.get()),
        .signalingConfigured = !m_signaling || m_signaling->HasUrl(),
        .deviceId = m_identity ? m_identity->DeviceId() : "",
        .deviceName = m_deviceName,
        .protocol = kProtocolVersion,
        .hostPairing = m_host.Pairing(),
        .clientPairing = m_client.Pairing(),
        .clients = m_host.Peers(),
        .sources = m_client.Peers(),
        .notice = m_notice};
}

void RemoteCore::Post(std::function<void()> task)
{
    m_thread.Post(std::move(task));
}

}  // namespace mango::remote
