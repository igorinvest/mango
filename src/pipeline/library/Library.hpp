#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../configuration/ChangeListener.hpp"
#include "../../file/DirectorySnapshot.hpp"
#include "FileRecord.hpp"
#include "LibraryFolder.hpp"
#include "LibraryScanner.hpp"

class Library
{
   public:
    // fastHashes of the records just evicted from the library.
    using FilesRemovedHandler = std::function<void(const std::vector<std::string>& fastHashes)>;

   private:
    // Guards everything below it. Folder scans write the library from their
    // own thread for minutes at a time while UIs read it every second, so
    // every access goes through this — and readers get copies, never
    // pointers into fileCache.
    mutable std::mutex m_mutex;

    std::unordered_map<std::string, FileRecord> fileCache;
    std::vector<LibraryFolder> folders;

    // Folder ids waiting to be scanned by m_scanThread, in order.
    std::deque<std::string> m_scanQueue;
    std::condition_variable m_scanCondition;
    std::thread m_scanThread;
    bool m_stopScanning = false;
    LibraryScanStatus m_scanStatus;

    // Checked between files, without the lock, so StopScanning() can cut a
    // long scan short.
    std::atomic<bool> m_cancelScans{false};

    // Told of every change a reader would see: entries, folders, scan
    // status. Has its own lock.
    ChangeListener m_changes;

    // Told of records evicted from the library. Has its own lock, and is
    // only called with m_mutex released.
    mutable std::mutex m_filesRemovedMutex;
    FilesRemovedHandler m_filesRemoved;

    // What ScanFolder takes from the library before walking the disk.
    struct FolderScanStart
    {
        std::filesystem::path rootPath;
        // relativePath → what the file there looked like at the last scan.
        std::unordered_map<std::string, FolderLocation> knownLocations;
    };

    // ScanFolder's state while it goes through the folder's videos.
    struct FolderScanProgress
    {
        std::unordered_set<std::string> foundPaths;
        int done = 0;
        int unsavedChanges = 0;
        std::chrono::steady_clock::time_point lastSave = std::chrono::steady_clock::now();
    };

    void LoadFileCache();
    void LoadFolders();

    // Callers must hold m_mutex.
    void PersistFileCacheLocked() const;
    void PersistFoldersLocked() const;
    void EnqueueScanLocked(const std::string& folderId);
    void WakeScanThreadLocked();
    LibraryFolder* FindFolderLocked(const std::string& folderId);

    // If record's filePath is gone but one of its folder locations still
    // exists, plays from there instead. Callers must hold m_mutex.
    void RepairFilePathLocked(FileRecord& record) const;

    void ScanWorker();

    // Keeps what a re-added file already had in the library: its original
    // date (so it doesn't jump to the top of "Date Added"), the folders it
    // was found in and their external tracks. Callers must hold m_mutex.
    void MergeExistingRecordLocked(FileRecord& record) const;

    // Brings folderId's locations in line with what's on disk: adds new
    // videos (hash, ffprobe, cover — or just a location for one already in
    // the library), skips files whose size and mtime haven't changed, and
    // drops locations whose file is gone, evicting records left with no
    // manual flag and no locations.
    void ScanFolder(const std::string& folderId);

    // ScanFolder's steps. BeginFolderScan sets the scan status; nullopt if
    // the folder is gone.
    std::optional<FolderScanStart> BeginFolderScan(const std::string& folderId);
    void ReportScanTotal(int filesTotal);
    void ReportScanProgress(int filesDone);

    // Gives candidate its location in the library: skipped when unchanged
    // since the last scan, otherwise hashed — and probed if it's new to the
    // library. false when the folder went away mid-scan.
    bool ScanCandidateFile(const std::string& folderId, const ScanCandidate& candidate,
        const FolderScanStart& start, FolderScanProgress& progress);
    bool AddCandidateLocation(const std::string& folderId, const ScanCandidate& candidate,
        const std::string& fastHash, std::optional<FileRecord> newRecord, FolderScanProgress& progress);

    // Saved in batches rather than once per file, so a big scan doesn't
    // rewrite library.json thousands of times.
    void SaveScanBatchIfDue(FolderScanProgress& progress);

    // Drops the locations not found this time and persists. Returns the
    // evicted records' fastHashes; nullopt if the folder is gone.
    std::optional<std::vector<std::string>> FinishFolderScan(
        const std::string& folderId, const FolderScanProgress& progress);

    // Drops folderId's locations that aren't in foundPaths, evicting records
    // left with no manual flag and no locations (into removed). Returns
    // whether any record changed. Callers must hold m_mutex.
    bool DropMissingLocationsLocked(const std::string& folderId,
        const std::unordered_set<std::string>& foundPaths, std::vector<std::string>& removed);

    // Calls the files-removed listener, if any. Never with m_mutex held.
    void NotifyFilesRemoved(const std::vector<std::string>& fastHashes) const;

    // Re-detects the subtitle and audio files next to each given record's
    // video (see ExternalSubtitleFinder, ExternalAudioFinder) from a folder
    // scan's snapshot, and persists the library if any changed. New or
    // changed audio files are probed, so this runs ffprobe — scan thread
    // only. Returns whether anything changed.
    bool RefreshExternalTracks(
        const std::vector<std::string>& fastHashes, const DirectorySnapshot& snapshot);
    bool RefreshExternalTracksOf(const std::string& fastHash, const DirectorySnapshot& snapshot);

    // Merges what snapshot has next to videoPaths into record's external
    // subtitles and audio. Returns whether either changed.
    static bool MergeExternalTracks(FileRecord& record,
        const std::vector<std::filesystem::path>& videoPaths, const DirectorySnapshot& snapshot);

    // Drops record's external tracks — for a video that's in no added
    // folder, since only folder scans find them. Returns whether it had any.
    static bool ClearExternalTracks(FileRecord& record);

    [[nodiscard]] std::vector<std::string> FileHashesInFolder(const std::string& folderId) const;

    // Every place record's video lives: filePath and each folder location.
    // Callers must hold m_mutex.
    [[nodiscard]] std::vector<std::filesystem::path> VideoPathsLocked(
        const FileRecord& record) const;

    // 1. Делаем конструктор приватным, чтобы никто не мог создать объект через `new` или на стеке
    Library();

   public:
    ~Library();

    // 2. Запрещаем копирование и перемещение объекта
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    Library(Library&&) = delete;
    Library& operator=(Library&&) = delete;

    // 3. Статический метод для доступа к единственному экземпляру
    static Library& GetInstance()
    {
        static Library instance;  // Создастся один раз при первом вызове, потокобезопасно
        return instance;
    }

    // Opens the file picker (see File::OpenFileAsync) and adds the chosen
    // file (see AddFile).
    void SelectFile();

    // Adds the video at filePath: probes it, extracts its cover, inserts it
    // (keeping what a re-added file already had) and persists the library.
    // securityBookmark is empty when there's none. Returns false if the
    // file is unreadable or not a playable video.
    bool AddFile(const std::filesystem::path& filePath, const std::string& securityBookmark);

    // Adds folderPath and scans it in the background (see SelectFolder).
    void AddFolder(const std::filesystem::path& folderPath, const std::string& securityBookmark);

    // Opens the folder picker (see File::OpenFolderAsync) and adds the
    // chosen folder, then scans it in the background. A folder that's
    // already added, or inside one that is, just gets re-scanned; added
    // folders inside the chosen one are merged into it.
    void SelectFolder();

    [[nodiscard]] std::optional<FileRecord> GetCachedFile(const std::string& fastHash) const;
    [[nodiscard]] std::vector<FileRecord> GetAllCachedFiles() const;
    [[nodiscard]] std::vector<LibraryFolder> GetFolders() const;
    [[nodiscard]] LibraryScanStatus GetScanStatus() const;

    // Queues a background re-scan of every added folder. Like
    // RemoveMissingFiles, must only be called once folder bookmarks are
    // active — a folder that's not accessible yet would look empty.
    void RescanFolders();

    // Cancels any running scan, drops queued ones and waits for the scan
    // thread to exit. Nothing is scanned after this. Called on app
    // termination (see ShutdownCoordinator).
    void StopScanning();

    // Evicts cached entries whose file can no longer be found, at its
    // filePath or any of its folder locations. Must only be called after
    // every stored security-scoped bookmark (files and folders) has
    // already been activated (see Engine::PruneMissingLibraryFiles) —
    // otherwise a sandboxed file that's simply not accessible *yet* looks
    // identical to a deleted one, and gets evicted (taking its bookmark
    // with it) before it ever gets a chance to be re-accessed.
    void RemoveMissingFiles();

    // Called after every change to the entries, the folders or the scan
    // status, on whichever thread made it and possibly with the library's
    // lock held: must not call back into Library.
    void SetChangeListener(ChangeListener::Handler handler);

    // Called with the fastHashes of records evicted from the library (a
    // file deleted, or gone from every added folder), once per scan or
    // RemoveMissingFiles, on whichever thread evicted them and never with
    // the library's lock held. An empty handler stops notifications.
    void SetFilesRemovedListener(FilesRemovedHandler handler);
};
