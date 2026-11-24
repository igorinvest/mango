#include "RemoteLibraries.hpp"

#include <fstream>

#include "../../log/Log.hpp"
#include "../../server/RequestGuard.hpp"
#include "../signaling/SignalingMessages.hpp"
#include "RemoteEntryId.hpp"

namespace mango::remote
{

namespace
{

using namespace std::chrono_literals;

constexpr std::size_t kMaxLibraryBytes = 64 * 1024 * 1024;
constexpr std::size_t kMaxCoverBytes = 16 * 1024 * 1024;
constexpr auto kTimeout = 30s;

constexpr std::array kCoverExtensions{".jpg", ".png"};

std::string ShortId(const std::string& deviceId) { return deviceId.substr(0, 8); }

bool WriteFile(const std::filesystem::path& path, const std::string& bytes)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

}  // namespace

RemoteLibraries::RemoteLibraries(std::filesystem::path coverFolder, std::function<void()> onChanged)
    : m_coverFolder(std::move(coverFolder)), m_onChanged(std::move(onChanged))
{
}

RemoteLibraries::~RemoteLibraries() { m_worker.Stop(); }

void RemoteLibraries::Fetch(
    const std::string& peerId, const std::string& peerName, std::shared_ptr<ForwardingClient> client)
{
    {
        std::lock_guard lock(m_mutex);
        m_forgotten.erase(peerId);
    }
    SetStatus(peerId, peerName, "loading");
    m_worker.Run([this, peerId, peerName, client = std::move(client)] { Load(peerId, peerName, *client); });
}

void RemoteLibraries::Forget(const std::string& peerId)
{
    {
        std::lock_guard lock(m_mutex);
        m_libraries.erase(peerId);
        m_forgotten.insert(peerId);
    }
    std::error_code error;
    std::filesystem::remove_all(PeerCoverFolder(peerId), error);
    NotifyChanged();
}

std::vector<LibraryEntry> RemoteLibraries::Entries() const
{
    std::lock_guard lock(m_mutex);
    std::vector<LibraryEntry> entries;
    for (const auto& [peerId, library] : m_libraries)
    {
        entries.insert(entries.end(), library.entries.begin(), library.entries.end());
    }
    return entries;
}

std::optional<LibraryEntry> RemoteLibraries::Find(const std::string& entryId) const
{
    const auto parts = RemoteEntryId::Parse(entryId);
    std::lock_guard lock(m_mutex);
    const auto library = parts ? m_libraries.find(parts->peerId) : m_libraries.end();
    if (library == m_libraries.end())
    {
        return std::nullopt;
    }
    for (const auto& entry : library->second.entries)
    {
        if (entry.fastHash == entryId)
        {
            return entry;
        }
    }
    return std::nullopt;
}

RemoteLibraries::Status RemoteLibraries::StatusOf(const std::string& peerId) const
{
    std::lock_guard lock(m_mutex);
    const auto found = m_libraries.find(peerId);
    return found != m_libraries.end() ? found->second.status : Status{};
}

void RemoteLibraries::Load(const std::string& peerId, const std::string& peerName, ForwardingClient& client)
{
    auto library = Download(client);
    if (!library)
    {
        Log::Warning(LogCategory::Remote, "couldn't load the library of {} ({})", peerName, ShortId(peerId));
        SetStatus(peerId, peerName, "failed");
        return;
    }

    Log::Info(LogCategory::Remote, "library of {} ({}): {} videos", peerName, ShortId(peerId), library->entries.size());
    const auto covers = library->covers;
    Store(peerId, peerName, std::move(*library));
    LoadCovers(peerId, client, covers);
}

std::optional<RemoteLibrary> RemoteLibraries::Download(ForwardingClient& client) const
{
    const auto response = FetchWhole(client, "/library", kMaxLibraryBytes, kTimeout);
    return response ? signaling::Read<RemoteLibrary>(response->body) : std::nullopt;
}

// One at a time, each shown as soon as it's saved.
void RemoteLibraries::LoadCovers(
    const std::string& peerId, ForwardingClient& client, const std::vector<std::string>& covers)
{
    for (const auto& fastHash : covers)
    {
        if (!RequestGuard::IsSafePathSegment(fastHash) || SavedCover(peerId, fastHash))
        {
            continue;
        }
        if (const auto path = DownloadCover(peerId, client, fastHash))
        {
            SetCover(peerId, RemoteEntryId::Make(peerId, fastHash), *path);
        }
    }
}

std::optional<std::filesystem::path> RemoteLibraries::DownloadCover(
    const std::string& peerId, ForwardingClient& client, const std::string& fastHash) const
{
    const auto response = FetchWhole(client, "/library-cover/" + fastHash, kMaxCoverBytes, kTimeout);
    if (!response)
    {
        return std::nullopt;
    }

    const bool png = forward::FindHeader(response->head.headers, "Content-Type") == "image/png";
    const auto path = PeerCoverFolder(peerId) / (fastHash + (png ? ".png" : ".jpg"));
    return WriteFile(path, response->body) ? std::optional(path) : std::nullopt;
}

void RemoteLibraries::SetStatus(const std::string& peerId, const std::string& peerName, const std::string& state)
{
    {
        std::lock_guard lock(m_mutex);
        if (m_forgotten.contains(peerId))
        {
            return;
        }
        auto& library = m_libraries[peerId];
        library.name = peerName;
        library.status.state = state;
    }
    NotifyChanged();
}

void RemoteLibraries::Store(const std::string& peerId, const std::string& peerName, RemoteLibrary library)
{
    std::vector<LibraryEntry> entries;
    for (auto& entry : library.entries)
    {
        if (RequestGuard::IsSafePathSegment(entry.fastHash))
        {
            entries.push_back(ToLocal(peerId, peerName, std::move(entry)));
        }
    }

    {
        std::lock_guard lock(m_mutex);
        if (m_forgotten.contains(peerId))
        {
            return;
        }
        auto& stored = m_libraries[peerId];
        stored.name = peerName;
        stored.status = {.state = "ready", .count = static_cast<int>(entries.size())};
        stored.entries = std::move(entries);
    }
    NotifyChanged();
}

void RemoteLibraries::SetCover(const std::string& peerId, const std::string& entryId, const std::filesystem::path& path)
{
    {
        std::lock_guard lock(m_mutex);
        const auto library = m_libraries.find(peerId);
        if (library == m_libraries.end())
        {
            return;
        }
        for (auto& entry : library->second.entries)
        {
            if (entry.fastHash == entryId)
            {
                entry.coverPath = path.string();
            }
        }
    }
    NotifyChanged();
}

// A's entry as B's session shows it: B's id, no paths from A's disk (A
// sends only folderNames), and B's own copy of the cover.
LibraryEntry RemoteLibraries::ToLocal(const std::string& peerId, const std::string& peerName, LibraryEntry entry) const
{
    const auto cover = SavedCover(peerId, entry.fastHash);
    entry.fastHash = RemoteEntryId::Make(peerId, entry.fastHash);
    entry.filePath.clear();
    entry.coverPath = cover ? cover->string() : std::string();
    entry.addedManually = false;
    entry.folderLocations.clear();
    entry.addedThisSession = false;
    entry.remotePeerId = peerId;
    entry.remotePeerName = peerName;
    return entry;
}

std::filesystem::path RemoteLibraries::PeerCoverFolder(const std::string& peerId) const
{
    return m_coverFolder / ShortId(peerId);
}

std::optional<std::filesystem::path> RemoteLibraries::SavedCover(
    const std::string& peerId, const std::string& fastHash) const
{
    for (const auto* extension : kCoverExtensions)
    {
        auto path = PeerCoverFolder(peerId) / (fastHash + extension);
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error))
        {
            return path;
        }
    }
    return std::nullopt;
}

void RemoteLibraries::NotifyChanged() const
{
    if (m_onChanged)
    {
        m_onChanged();
    }
}

}  // namespace mango::remote
