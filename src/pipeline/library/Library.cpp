#include "Library.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <print>
#include <random>
#include <string>
#include <unordered_set>

#include "../../configuration/VideoFileTypes.hpp"
#include "../../media/MediaBackend.hpp"
#include "../../file/File.hpp"
#include "../../file/Json.hpp"
#include "CoverExtractor.hpp"
#include "LibraryScanner.hpp"
#include "external/ExternalAudioFinder.hpp"
#include "external/ExternalSubtitleFinder.hpp"
#include "../../log/Log.hpp"

const std::string libraryFileName = "library.json";
const std::string libraryFoldersFileName = "library_folders.json";

namespace
{
namespace fs = std::filesystem;

std::int64_t NowUnixSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::optional<std::int64_t> LastWriteUnixSeconds(const fs::path& path)
{
    return File::LastWriteUnixSeconds(path);
}

fs::path FromUtf8(const std::string& utf8) { return File::FromUtf8(utf8); }

// Whether path is root itself or anywhere under it.
bool IsSameOrInside(const fs::path& path, const fs::path& root)
{
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
}

std::string FolderDisplayName(const fs::path& rootPath)
{
    const auto name = File::ToUtf8(rootPath.filename());
    return name.empty() ? File::ToUtf8(rootPath) : name;
}

std::string NewFolderId()
{
    std::random_device device;
    std::mt19937_64 generator((static_cast<std::uint64_t>(device()) << 32) ^ device());
    return std::format("{:016x}{:016x}", generator(), generator());
}

// Removes folderId's location at relativePath from every record — a path
// holds one file at a time, so whichever record had it before no longer
// does once a different file is found there.
void RemoveLocation(std::unordered_map<std::string, FileRecord>& fileCache,
    const std::string& folderId, const std::string& relativePath)
{
    for (auto& [_, record] : fileCache)
    {
        std::erase_if(record.folderLocations, [&](const FolderLocation& location)
            { return location.folderId == folderId && location.relativePath == relativePath; });
    }
}

// Moves each folder's legacyId to folderId. true when any had one.
bool MigrateLegacyFolderIds(std::vector<LibraryFolder>& folders)
{
    bool migrated = false;
    for (auto& folder : folders)
    {
        if (folder.legacyId)
        {
            if (folder.folderId.empty())
            {
                folder.folderId = *folder.legacyId;
            }
            folder.legacyId.reset();
            migrated = true;
        }
    }
    return migrated;
}

// A video new to the library, probed and with its cover extracted. nullopt
// if it can't be probed.
std::optional<FileRecord> ProbeNewFile(const ScanCandidate& candidate, const std::string& fastHash)
{
    const auto mediaInfo = MediaBackends::Get().Probe(candidate.absolutePath);
    if (!mediaInfo)
    {
        return std::nullopt;
    }

    FileRecord record(fastHash, candidate.absolutePath, *mediaInfo);
    record.coverFileName = CoverExtractor::Extract(record);
    record.addedManually = false;
    record.addedThisSession = true;
    record.addedAtUnixSeconds = NowUnixSeconds();
    return record;
}
}  // namespace

Library::Library()
{
    LoadFileCache();
    LoadFolders();
};

Library::~Library() { StopScanning(); }

void Library::LoadFileCache()
{
    const auto cachePath = File::getApplicationDataPath() / libraryFileName;

    // Lenient: library.json written before playback history moved to
    // PlaybackHistory still has those keys on every record.
    auto cache =
        Json::ReadJsonFromFileLenient<std::unordered_map<std::string, FileRecord>>(cachePath);

    if (!cache)
    {
        return;
    }

    fileCache = std::move(*cache);

    // Whatever addedThisSession value happened to be persisted belongs to a
    // previous run - everything just loaded from library.json is, by
    // definition, not new to *this* one.
    bool backfilled = false;
    for (auto& [_, record] : fileCache)
    {
        record.addedThisSession = false;

        // External tracks come only from folder scans (see ScanFolder):
        // records from before that rule lose what Add Movie found.
        if (record.folderLocations.empty())
        {
            backfilled = ClearExternalTracks(record) || backfilled;
        }

        // Records from before VideoTrack::rotation / interlaced or the tracks'
        // disposition flags existed.
        backfilled =
            MediaBackends::Get().BackfillMediaInfo(record, record.mediaInfo) ||
            backfilled;

        // Records from before addedAtUnixSeconds existed - the metadata
        // folder is created when a file is added, so its timestamp is the
        // closest surviving approximation of when that happened.
        if (record.addedAtUnixSeconds == 0)
        {
            if (const auto addedAt = LastWriteUnixSeconds(record.getMetadataFolder()))
            {
                record.addedAtUnixSeconds = *addedAt;
                backfilled = true;
            }
        }
    }

    if (backfilled)
    {
        PersistFileCacheLocked();
    }
}

void Library::LoadFolders()
{
    const auto foldersPath = File::getApplicationDataPath() / libraryFoldersFileName;

    if (auto loaded = Json::ReadJsonFromFile<std::vector<LibraryFolder>>(foldersPath))
    {
        folders = std::move(*loaded);
        if (MigrateLegacyFolderIds(folders))
        {
            PersistFoldersLocked();
        }
    }
}

void Library::SelectFile()
{
    File::OpenFileAsync("Select video file",
        {"All Supported Video Files", VideoFileTypes::GetFileDialogFilter()},
        [this](const std::string& selectedFilePath, const std::string& securityBookmark)
        {
            if (!selectedFilePath.empty())
            {
                AddFile(FromUtf8(selectedFilePath), securityBookmark);
            }
        });
}

bool Library::AddFile(const fs::path& filePath, const std::string& securityBookmark)
{
    const auto fastHash = File::FastHash(filePath);
    const auto mediaInfo = fastHash ? MediaBackends::Get().Probe(filePath) : std::nullopt;

    if (!fastHash || !mediaInfo)
    {
        Log::Warning(LogCategory::Library, "not added: {} is unreadable or not a playable video",
            File::ToUtf8(filePath));
        return false;
    }

    Log::Info(LogCategory::Library, "added file {} ({}), security bookmark: {}",
        File::ToUtf8(filePath), *fastHash,
        securityBookmark.empty() ? "none" : std::to_string(securityBookmark.size()) + " bytes");

    FileRecord record(*fastHash, filePath, *mediaInfo,
        securityBookmark.empty() ? std::nullopt : std::optional<std::string>(securityBookmark));
    record.coverFileName = CoverExtractor::Extract(record);
    record.addedThisSession = true;

    std::lock_guard lock(m_mutex);
    MergeExistingRecordLocked(record);
    fileCache.insert_or_assign(*fastHash, std::move(record));
    PersistFileCacheLocked();
    return true;
}

void Library::MergeExistingRecordLocked(FileRecord& record) const
{
    const auto existing = fileCache.find(record.fastHash);
    if (existing == fileCache.end())
    {
        record.addedAtUnixSeconds = NowUnixSeconds();
        return;
    }

    const auto& old = existing->second;
    record.addedAtUnixSeconds =
        old.addedAtUnixSeconds != 0 ? old.addedAtUnixSeconds : NowUnixSeconds();
    record.folderLocations = old.folderLocations;
    record.externalSubtitles = old.externalSubtitles;
    record.nextExternalSubtitleIndex = old.nextExternalSubtitleIndex;
    record.externalAudios = old.externalAudios;
    record.nextExternalAudioIndex = old.nextExternalAudioIndex;
}

void Library::SelectFolder()
{
    File::OpenFolderAsync("Select folder",
        [this](const std::string& selectedFolderPath, const std::string& securityBookmark)
        {
            if (!selectedFolderPath.empty())
            {
                AddFolder(FromUtf8(selectedFolderPath), securityBookmark);
            }
        });
}

void Library::AddFolder(const fs::path& folderPath, const std::string& securityBookmark)
{
    std::error_code ec;
    auto rootPath = fs::weakly_canonical(folderPath, ec);
    if (ec)
    {
        rootPath = folderPath.lexically_normal();
    }

    std::lock_guard lock(m_mutex);

    // Already covered by an added folder (the same one, or one above it):
    // nothing new to add, just look at it again.
    for (auto& folder : folders)
    {
        if (IsSameOrInside(rootPath, folder.rootPath))
        {
            if (rootPath == folder.rootPath && !securityBookmark.empty())
            {
                folder.securityBookmark = securityBookmark;
                PersistFoldersLocked();
            }

            EnqueueScanLocked(folder.folderId);
            return;
        }
    }

    LibraryFolder folder;
    folder.folderId = NewFolderId();
    folder.rootPath = rootPath;
    folder.securityBookmark =
        securityBookmark.empty() ? std::nullopt : std::optional<std::string>(securityBookmark);
    folder.addedAtUnixSeconds = NowUnixSeconds();

    // Added folders inside the new one become part of it: their locations
    // are re-rooted, so their videos aren't hashed and probed again.
    bool cacheChanged = false;
    for (const auto& inner : folders)
    {
        if (!IsSameOrInside(inner.rootPath, rootPath))
        {
            continue;
        }

        const auto prefix = LibraryScanner::RelativePathUtf8(inner.rootPath, rootPath);

        for (auto& [_, record] : fileCache)
        {
            for (auto& location : record.folderLocations)
            {
                if (location.folderId == inner.folderId)
                {
                    location.folderId = folder.folderId;
                    location.relativePath = prefix + "/" + location.relativePath;
                    cacheChanged = true;
                }
            }
        }

        Log::Info(LogCategory::Library, "merging folder {} into {}", File::ToUtf8(inner.rootPath),
            File::ToUtf8(rootPath));
    }

    std::erase_if(folders, [&rootPath](const LibraryFolder& inner)
        { return IsSameOrInside(inner.rootPath, rootPath); });

    Log::Info(LogCategory::Library, "adding folder {}", File::ToUtf8(rootPath));

    folders.push_back(folder);
    PersistFoldersLocked();

    if (cacheChanged)
    {
        PersistFileCacheLocked();
    }

    EnqueueScanLocked(folder.folderId);
}

void Library::RescanFolders()
{
    std::lock_guard lock(m_mutex);

    for (const auto& folder : folders)
    {
        EnqueueScanLocked(folder.folderId);
    }
}

void Library::EnqueueScanLocked(const std::string& folderId)
{
    if (m_stopScanning || std::ranges::find(m_scanQueue, folderId) != m_scanQueue.end())
    {
        return;
    }

    m_scanQueue.push_back(folderId);
    WakeScanThreadLocked();
}

void Library::WakeScanThreadLocked()
{
    if (!m_scanThread.joinable())
    {
        m_scanThread = std::thread([this]() { ScanWorker(); });
    }

    m_scanCondition.notify_one();
}

void Library::StopScanning()
{
    {
        std::lock_guard lock(m_mutex);
        m_stopScanning = true;
        m_scanQueue.clear();
    }

    m_cancelScans = true;
    m_scanCondition.notify_all();

    if (m_scanThread.joinable() && m_scanThread.get_id() != std::this_thread::get_id())
    {
        m_scanThread.join();
    }
}

void Library::ScanWorker()
{
    while (true)
    {
        std::string folderId;

        {
            std::unique_lock lock(m_mutex);
            m_scanCondition.wait(lock, [this]() {
                return m_stopScanning || !m_scanQueue.empty();
            });

            if (m_stopScanning)
            {
                return;
            }

            folderId = std::move(m_scanQueue.front());
            m_scanQueue.pop_front();

            // Set in the same critical section as the pop, so a status read
            // never sees "nothing scanning, nothing queued" in between.
            m_scanStatus = {};
            m_scanStatus.scanning = true;
            m_scanStatus.folderId = folderId;
            m_changes.Notify();
        }

        ScanFolder(folderId);

        {
            std::lock_guard lock(m_mutex);
            m_scanStatus = {};
            m_changes.Notify();
        }
    }
}

void Library::ScanFolder(const std::string& folderId)
{
    const auto start = BeginFolderScan(folderId);
    if (!start)
    {
        return;
    }

    Log::Info(LogCategory::Library, "scanning folder {}", File::ToUtf8(start->rootPath));

    DirectorySnapshot snapshot;
    const auto candidates = LibraryScanner::CollectVideoFiles(start->rootPath, m_cancelScans, snapshot);
    if (!candidates)
    {
        return;
    }
    ReportScanTotal(static_cast<int>(candidates->size()));

    FolderScanProgress progress;
    for (const auto& candidate : *candidates)
    {
        if (m_cancelScans || !ScanCandidateFile(folderId, candidate, *start, progress))
        {
            return;
        }
        ReportScanProgress(++progress.done);
        SaveScanBatchIfDue(progress);
    }

    if (m_cancelScans)
    {
        return;
    }

    const auto removed = FinishFolderScan(folderId, progress);
    if (!removed)
    {
        return;
    }
    NotifyFilesRemoved(*removed);

    RefreshExternalTracks(FileHashesInFolder(folderId), snapshot);

    Log::Info(LogCategory::Library, "finished scanning {}: {} videos", File::ToUtf8(start->rootPath),
        candidates->size());
}

std::optional<Library::FolderScanStart> Library::BeginFolderScan(const std::string& folderId)
{
    std::lock_guard lock(m_mutex);

    const auto* folder = FindFolderLocked(folderId);
    if (!folder)
    {
        return std::nullopt;
    }

    FolderScanStart start{folder->rootPath, {}};
    for (const auto& [_, record] : fileCache)
    {
        for (const auto& location : record.folderLocations)
        {
            if (location.folderId == folderId)
            {
                start.knownLocations.emplace(location.relativePath, location);
            }
        }
    }

    m_scanStatus = {true, folderId, FolderDisplayName(start.rootPath), 0, 0,
        static_cast<int>(m_scanQueue.size())};
    m_changes.Notify();
    return start;
}

void Library::ReportScanTotal(int filesTotal)
{
    std::lock_guard lock(m_mutex);
    m_scanStatus.filesTotal = filesTotal;
    m_changes.Notify();
}

void Library::ReportScanProgress(int filesDone)
{
    std::lock_guard lock(m_mutex);
    m_scanStatus.filesDone = filesDone;
    m_scanStatus.foldersQueued = static_cast<int>(m_scanQueue.size());
    m_changes.Notify();
}

bool Library::ScanCandidateFile(const std::string& folderId, const ScanCandidate& candidate,
    const FolderScanStart& start, FolderScanProgress& progress)
{
    const auto known = start.knownLocations.find(candidate.relativePath);
    const bool unchanged = known != start.knownLocations.end() &&
        known->second.fileSizeBytes == candidate.fileSizeBytes &&
        known->second.lastWriteUnixSeconds == candidate.lastWriteUnixSeconds;

    if (unchanged)
    {
        progress.foundPaths.insert(candidate.relativePath);
        return true;
    }

    const auto fastHash = File::FastHash(candidate.absolutePath);
    if (!fastHash)
    {
        return true;
    }

    bool alreadyInLibrary = false;
    {
        std::lock_guard lock(m_mutex);
        alreadyInLibrary = fileCache.contains(*fastHash);
    }

    // Only a video new to the library is probed — one that's already there
    // (manual, or in another folder) just gets another location.
    auto newRecord = alreadyInLibrary ? std::nullopt : ProbeNewFile(candidate, *fastHash);
    return AddCandidateLocation(folderId, candidate, *fastHash, std::move(newRecord), progress);
}

bool Library::AddCandidateLocation(const std::string& folderId, const ScanCandidate& candidate,
    const std::string& fastHash, std::optional<FileRecord> newRecord, FolderScanProgress& progress)
{
    std::lock_guard lock(m_mutex);

    // Merged into another folder or otherwise gone mid-scan.
    if (!FindFolderLocked(folderId))
    {
        return false;
    }

    auto record = fileCache.find(fastHash);
    if (record == fileCache.end() && newRecord)
    {
        record = fileCache.emplace(fastHash, std::move(*newRecord)).first;
    }
    if (record == fileCache.end())
    {
        return true;
    }

    RemoveLocation(fileCache, folderId, candidate.relativePath);
    record->second.folderLocations.push_back(FolderLocation{folderId, candidate.relativePath,
        candidate.fileSizeBytes, candidate.lastWriteUnixSeconds});
    RepairFilePathLocked(record->second);

    progress.foundPaths.insert(candidate.relativePath);
    ++progress.unsavedChanges;
    return true;
}

void Library::SaveScanBatchIfDue(FolderScanProgress& progress)
{
    constexpr int kBatchSize = 10;
    constexpr auto kBatchInterval = std::chrono::seconds(1);

    const auto now = std::chrono::steady_clock::now();
    const bool due = progress.unsavedChanges >= kBatchSize ||
        (progress.unsavedChanges > 0 && now - progress.lastSave >= kBatchInterval);
    if (!due)
    {
        return;
    }

    {
        std::lock_guard lock(m_mutex);
        PersistFileCacheLocked();
    }
    progress.unsavedChanges = 0;
    progress.lastSave = now;
}

std::optional<std::vector<std::string>> Library::FinishFolderScan(
    const std::string& folderId, const FolderScanProgress& progress)
{
    std::lock_guard lock(m_mutex);

    auto* folder = FindFolderLocked(folderId);
    if (!folder)
    {
        return std::nullopt;
    }

    // Whatever wasn't found this time was deleted or moved (a moved file
    // already got its new location while scanning).
    std::vector<std::string> removed;
    const bool changed = DropMissingLocationsLocked(folderId, progress.foundPaths, removed);

    folder->lastScanUnixSeconds = NowUnixSeconds();
    PersistFoldersLocked();

    if (changed || progress.unsavedChanges > 0)
    {
        PersistFileCacheLocked();
    }
    return removed;
}

bool Library::DropMissingLocationsLocked(const std::string& folderId,
    const std::unordered_set<std::string>& foundPaths, std::vector<std::string>& removed)
{
    bool changed = false;

    for (auto it = fileCache.begin(); it != fileCache.end();)
    {
        auto& record = it->second;

        const auto dropped = std::erase_if(record.folderLocations,
            [&](const FolderLocation& location) {
                return location.folderId == folderId && !foundPaths.contains(location.relativePath);
            });
        changed = changed || dropped > 0;

        if (!record.addedManually && record.folderLocations.empty())
        {
            Log::Info(LogCategory::Library, "removing {} ({}): no longer in any folder",
                File::ToUtf8(record.filePath), it->first);
            removed.push_back(it->first);
            it = fileCache.erase(it);
            continue;
        }

        if (dropped > 0)
        {
            RepairFilePathLocked(record);
        }

        // External tracks come only from folder scans: a video that's left
        // in no folder keeps none.
        if (record.folderLocations.empty() && ClearExternalTracks(record))
        {
            changed = true;
        }

        ++it;
    }
    return changed;
}

void Library::NotifyFilesRemoved(const std::vector<std::string>& fastHashes) const
{
    if (fastHashes.empty())
    {
        return;
    }

    FilesRemovedHandler handler;
    {
        std::lock_guard lock(m_filesRemovedMutex);
        handler = m_filesRemoved;
    }
    if (handler)
    {
        handler(fastHashes);
    }
}

void Library::SetFilesRemovedListener(FilesRemovedHandler handler)
{
    std::lock_guard lock(m_filesRemovedMutex);
    m_filesRemoved = std::move(handler);
}

LibraryFolder* Library::FindFolderLocked(const std::string& folderId)
{
    const auto it = std::ranges::find(folders, folderId, &LibraryFolder::folderId);
    return it == folders.end() ? nullptr : &*it;
}

void Library::RepairFilePathLocked(FileRecord& record) const
{
    if (File::Exists(record.filePath))
    {
        return;
    }

    for (const auto& location : record.folderLocations)
    {
        const auto folder = std::ranges::find(folders, location.folderId, &LibraryFolder::folderId);
        if (folder == folders.end())
        {
            continue;
        }

        auto candidate = folder->rootPath / FromUtf8(location.relativePath);
        if (File::Exists(candidate))
        {
            record.filePath = std::move(candidate);
            return;
        }
    }
}

std::vector<std::string> Library::FileHashesInFolder(const std::string& folderId) const
{
    std::lock_guard lock(m_mutex);

    std::vector<std::string> hashes;
    for (const auto& [fastHash, record] : fileCache)
    {
        if (std::ranges::contains(record.folderLocations, folderId, &FolderLocation::folderId))
        {
            hashes.push_back(fastHash);
        }
    }
    return hashes;
}

bool Library::RefreshExternalTracks(
    const std::vector<std::string>& fastHashes, const DirectorySnapshot& snapshot)
{
    bool changed = false;
    for (const auto& fastHash : fastHashes)
    {
        if (m_cancelScans)
        {
            break;
        }
        changed = RefreshExternalTracksOf(fastHash, snapshot) || changed;
    }

    if (changed)
    {
        std::lock_guard lock(m_mutex);
        PersistFileCacheLocked();
    }
    return changed;
}

bool Library::RefreshExternalTracksOf(
    const std::string& fastHash, const DirectorySnapshot& snapshot)
{
    // Only the scan thread gets here, so nothing else changes this record's
    // external tracks between the copy and the write-back.
    FileRecord record;
    std::vector<fs::path> videoPaths;
    {
        std::lock_guard lock(m_mutex);
        const auto it = fileCache.find(fastHash);
        if (it == fileCache.end())
        {
            return false;
        }
        record = it->second;
        videoPaths = VideoPathsLocked(record);
    }

    // Encoding detection and probing, outside m_mutex.
    if (!MergeExternalTracks(record, videoPaths, snapshot))
    {
        return false;
    }

    std::lock_guard lock(m_mutex);
    const auto it = fileCache.find(fastHash);
    if (it == fileCache.end())
    {
        return false;
    }

    it->second.externalSubtitles = std::move(record.externalSubtitles);
    it->second.nextExternalSubtitleIndex = record.nextExternalSubtitleIndex;
    it->second.externalAudios = std::move(record.externalAudios);
    it->second.nextExternalAudioIndex = record.nextExternalAudioIndex;
    return true;
}

bool Library::MergeExternalTracks(FileRecord& record, const std::vector<fs::path>& videoPaths,
    const DirectorySnapshot& snapshot)
{
    const auto subtitles = ExternalSubtitleFinder::FindCandidates(snapshot, videoPaths);
    const auto audios = ExternalAudioFinder::FindCandidates(snapshot, videoPaths);

    const bool subtitlesChanged = ExternalSubtitleFinder::MergeInto(record, subtitles, snapshot);
    const bool audiosChanged = ExternalAudioFinder::MergeInto(record, audios, snapshot);
    return subtitlesChanged || audiosChanged;
}

bool Library::ClearExternalTracks(FileRecord& record)
{
    if (record.externalSubtitles.empty() && record.externalAudios.empty())
    {
        return false;
    }

    // The next-index counters stay, so an index is still never reused.
    record.externalSubtitles.clear();
    record.externalAudios.clear();
    return true;
}

std::vector<fs::path> Library::VideoPathsLocked(const FileRecord& record) const
{
    std::vector<fs::path> paths = {record.filePath};

    for (const auto& location : record.folderLocations)
    {
        const auto folder = std::ranges::find(folders, location.folderId, &LibraryFolder::folderId);
        if (folder == folders.end())
        {
            continue;
        }

        auto path = folder->rootPath / FromUtf8(location.relativePath);
        if (std::ranges::find(paths, path) == paths.end())
        {
            paths.push_back(std::move(path));
        }
    }

    return paths;
}

void Library::PersistFileCacheLocked() const
{
    auto applicationFolder = File::getApplicationDataPath();

    std::filesystem::create_directories(applicationFolder);

    Json::SaveJsonToFile(applicationFolder / libraryFileName, fileCache);
    m_changes.Notify();
}

void Library::PersistFoldersLocked() const
{
    auto applicationFolder = File::getApplicationDataPath();

    std::filesystem::create_directories(applicationFolder);

    Json::SaveJsonToFile(applicationFolder / libraryFoldersFileName, folders);
    m_changes.Notify();
}

void Library::SetChangeListener(ChangeListener::Handler handler) { m_changes.Set(std::move(handler)); }

std::optional<FileRecord> Library::GetCachedFile(const std::string &fastHash) const
{
    std::lock_guard lock(m_mutex);

    auto it = fileCache.find(fastHash);
    if (it == fileCache.end())
    {
        return std::nullopt;
    }
    return it->second;
}

void Library::RemoveMissingFiles()
{
    bool cacheChanged = false;
    std::vector<std::string> removed;

    {
        std::lock_guard lock(m_mutex);

        for (auto it = fileCache.begin(); it != fileCache.end();)
        {
            const auto originalPath = it->second.filePath;
            RepairFilePathLocked(it->second);

            if (!File::Exists(it->second.filePath))
            {
                Log::Info(LogCategory::Library, "removing missing file {} ({})", File::ToUtf8(it->second.filePath),
                    it->first);

                removed.push_back(it->first);
                it = fileCache.erase(it);
                cacheChanged = true;
                continue;
            }

            cacheChanged = cacheChanged || it->second.filePath != originalPath;
            ++it;
        }

        if (cacheChanged)
        {
            PersistFileCacheLocked();
        }
    }

    NotifyFilesRemoved(removed);
}

std::vector<FileRecord> Library::GetAllCachedFiles() const
{
    std::vector<FileRecord> files;

    {
        std::lock_guard lock(m_mutex);
        files.reserve(fileCache.size());

        for (const auto &[_, record] : fileCache)
        {
            files.push_back(record);
        }
    }

    std::ranges::sort(files, {}, &FileRecord::libraryName);

    return files;
}

std::vector<LibraryFolder> Library::GetFolders() const
{
    std::lock_guard lock(m_mutex);
    return folders;
}

LibraryScanStatus Library::GetScanStatus() const
{
    std::lock_guard lock(m_mutex);

    auto status = m_scanStatus;
    status.foldersQueued = static_cast<int>(m_scanQueue.size());
    return status;
}
