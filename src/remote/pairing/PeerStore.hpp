#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glaze/glaze.hpp>

namespace mango::remote
{

// A device this one paired with. Which side it's on follows from the store
// it's in (see PeerStore).
struct PairedPeer
{
    std::string deviceId;
    std::string publicKey;
    std::string name;
    std::int64_t pairedAtUnixSeconds = 0;
};

// One list of paired devices in a JSON file: the host module's clients
// (remote/clients.json) or the client module's sources
// (remote/sources.json). The same device can be in both. Removing a peer
// makes its module ignore its signals from then on. Not thread-safe: used
// from the remote thread only.
class PeerStore
{
   public:
    explicit PeerStore(std::filesystem::path path);

    // Reads the file; a missing or unreadable one is an empty list.
    void Load();

    [[nodiscard]] const std::vector<PairedPeer>& All() const { return m_peers; }
    [[nodiscard]] const PairedPeer* Find(const std::string& deviceId) const;

    // The peer's name, or the start of its id when it isn't in the store.
    [[nodiscard]] std::string NameOf(const std::string& deviceId) const;

    // Replaces a peer with the same deviceId. Saves right away.
    void Add(PairedPeer peer);

    // Returns whether there was such a peer. Saves right away.
    bool Remove(const std::string& deviceId);

   private:
    void Save() const;

    std::filesystem::path m_path;
    std::vector<PairedPeer> m_peers;
};

}  // namespace mango::remote

// peers.json's keys, spelled out so renaming a member can't lose the
// user's pairings.
template <>
struct glz::meta<mango::remote::PairedPeer>
{
    using T = mango::remote::PairedPeer;
    static constexpr auto value = object("deviceId", &T::deviceId, "publicKey", &T::publicKey, "name", &T::name,
        "pairedAtUnixSeconds", &T::pairedAtUnixSeconds);
};
