#include "PeerStore.hpp"

#include <algorithm>
#include <system_error>

#include "../../file/Json.hpp"
#include "../../log/Log.hpp"

namespace mango::remote
{

// clients.json or sources.json.
struct PeersFile
{
    std::vector<PairedPeer> peers;
};

}  // namespace mango::remote

template <>
struct glz::meta<mango::remote::PeersFile>
{
    using T = mango::remote::PeersFile;
    static constexpr auto value = object("peers", &T::peers);
};

namespace mango::remote
{

PeerStore::PeerStore(std::filesystem::path path) : m_path(std::move(path)) {}

void PeerStore::Load()
{
    const auto file = Json::ReadJsonFromFileLenient<PeersFile>(m_path);
    m_peers = file ? file->peers : std::vector<PairedPeer>{};
}

const PairedPeer* PeerStore::Find(const std::string& deviceId) const
{
    const auto found = std::ranges::find(m_peers, deviceId, &PairedPeer::deviceId);
    return found != m_peers.end() ? &*found : nullptr;
}

std::string PeerStore::NameOf(const std::string& deviceId) const
{
    const auto* peer = Find(deviceId);
    return peer ? peer->name : deviceId.substr(0, 8);
}

void PeerStore::Add(PairedPeer peer)
{
    std::erase_if(m_peers, [&](const PairedPeer& existing) { return existing.deviceId == peer.deviceId; });
    m_peers.push_back(std::move(peer));
    Save();
}

bool PeerStore::Remove(const std::string& deviceId)
{
    const auto removed = std::erase_if(m_peers, [&](const PairedPeer& peer) { return peer.deviceId == deviceId; });
    if (removed > 0)
    {
        Save();
    }
    return removed > 0;
}

void PeerStore::Save() const
{
    std::error_code ignored;
    std::filesystem::create_directories(m_path.parent_path(), ignored);
    if (!Json::SaveJsonToFile<glz::opts{.prettify = true}>(m_path, PeersFile{m_peers}))
    {
        Log::Error(LogCategory::Remote, "can't save {}", m_path.string());
    }
}

}  // namespace mango::remote
