#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../../Engine.hpp"
#include "../../session/CastWorker.hpp"
#include "../forwarding/ForwardingClient.hpp"
#include "ControlMessages.hpp"

namespace mango::remote
{

// B's copy of each source peer's library (step 8 of
// docs/scope/hybrid_streaming_architecture.md), fetched over the link as
// /library when it connects, with covers saved under coverFolder. Entries
// are LibraryEntry with "remote:<peerId>:<fastHash>" ids (see
// RemoteEntryId) and the peer's id and name. A library stays known while
// its link is down, so the grid doesn't flicker; it goes when the peer is
// unpaired.
//
// Fetching runs on its own worker; everything else is safe from any
// thread.
class RemoteLibraries
{
   public:
    // How a peer's library is doing (see RemotePeerState::library).
    struct Status
    {
        std::string state = "none";
        int count = 0;
    };

    // onChanged: entries or a status changed. Called on the worker.
    RemoteLibraries(std::filesystem::path coverFolder, std::function<void()> onChanged);
    ~RemoteLibraries();

    RemoteLibraries(const RemoteLibraries&) = delete;
    RemoteLibraries& operator=(const RemoteLibraries&) = delete;

    // (Re)loads peerId's library through client, then its missing covers.
    void Fetch(const std::string& peerId, const std::string& peerName, std::shared_ptr<ForwardingClient> client);

    // The peer was unpaired: its entries and covers go.
    void Forget(const std::string& peerId);

    [[nodiscard]] std::vector<LibraryEntry> Entries() const;
    [[nodiscard]] std::optional<LibraryEntry> Find(const std::string& entryId) const;
    [[nodiscard]] Status StatusOf(const std::string& peerId) const;

   private:
    struct PeerLibrary
    {
        std::string name;
        Status status;
        std::vector<LibraryEntry> entries;
    };

    // On the worker.
    void Load(const std::string& peerId, const std::string& peerName, ForwardingClient& client);
    [[nodiscard]] std::optional<RemoteLibrary> Download(ForwardingClient& client) const;
    void LoadCovers(const std::string& peerId, ForwardingClient& client, const std::vector<std::string>& covers);
    [[nodiscard]] std::optional<std::filesystem::path> DownloadCover(
        const std::string& peerId, ForwardingClient& client, const std::string& fastHash) const;

    void SetStatus(const std::string& peerId, const std::string& peerName, const std::string& state);
    void Store(const std::string& peerId, const std::string& peerName, RemoteLibrary library);
    void SetCover(const std::string& peerId, const std::string& entryId, const std::filesystem::path& path);
    [[nodiscard]] LibraryEntry ToLocal(const std::string& peerId, const std::string& peerName, LibraryEntry entry) const;

    [[nodiscard]] std::filesystem::path PeerCoverFolder(const std::string& peerId) const;
    // The cover already saved for fastHash, if any.
    [[nodiscard]] std::optional<std::filesystem::path> SavedCover(
        const std::string& peerId, const std::string& fastHash) const;

    void NotifyChanged() const;

    const std::filesystem::path m_coverFolder;
    const std::function<void()> m_onChanged;

    mutable std::mutex m_mutex;
    std::map<std::string, PeerLibrary> m_libraries;
    // Unpaired while a fetch may still run: what it finds is dropped.
    std::set<std::string> m_forgotten;

    CastWorker m_worker{"remote library"};
};

}  // namespace mango::remote
