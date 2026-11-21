#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

// A folder the user added with Add Folder (see Library::SelectFolder). Every
// supported video found anywhere under rootPath is in the library, with a
// FolderLocation pointing back here. Persisted in library_folders.json.
struct LibraryFolder
{
    // Random, stable across renames of rootPath — FolderLocation::folderId
    // refers to this, not to the path.
    std::string folderId;
    std::filesystem::path rootPath;

    // Base64-encoded macOS security-scoped bookmark for rootPath. One
    // folder bookmark grants access to everything under it, so files found
    // by a scan get no bookmark of their own.
    std::optional<std::string> securityBookmark;

    std::int64_t addedAtUnixSeconds = 0;
    std::int64_t lastScanUnixSeconds = 0;

    // folderId as library_folders.json said it before "folderId" ("id"):
    // read only, moved to folderId on load (see Library::LoadFolders).
    std::optional<std::string> legacyId;
};

// What the folder scanner (see Library::ScanFolder) is doing right now.
// scanning is false, and the rest empty/0, while no scan is running.
struct LibraryScanStatus
{
    bool scanning = false;
    std::string folderId;
    std::string folderName;
    int filesDone = 0;
    int filesTotal = 0;

    // Folders still waiting for their turn after this one.
    int foldersQueued = 0;
};
