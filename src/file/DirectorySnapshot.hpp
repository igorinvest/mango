#pragma once

#include <filesystem>
#include <map>
#include <vector>

// What a directory walk saw, kept in memory so the files next to each video
// can be looked up without listing the same directories again (see
// ExternalFileFinder). Holds whatever files the walker chose to add, not
// necessarily every file.
class DirectorySnapshot
{
   public:
    // Registers directory as walked, and as a subfolder of its parent when
    // the parent was walked too. A walk adds its root first, then each
    // directory before what's inside it.
    void AddDirectory(const std::filesystem::path& directory);

    // Adds file to its directory's list. Ignored when that directory wasn't
    // added.
    void AddFile(const std::filesystem::path& file);

    // Whether directory was walked.
    [[nodiscard]] bool Covers(const std::filesystem::path& directory) const;

    // directory's files / subfolders; empty when it wasn't walked.
    [[nodiscard]] const std::vector<std::filesystem::path>& Files(
        const std::filesystem::path& directory) const;
    [[nodiscard]] const std::vector<std::filesystem::path>& Subfolders(
        const std::filesystem::path& directory) const;

   private:
    struct Directory
    {
        std::vector<std::filesystem::path> files;
        std::vector<std::filesystem::path> subfolders;
    };

    [[nodiscard]] const Directory& Find(const std::filesystem::path& directory) const;

    // std::map rather than unordered_map: path comparison treats '/' and
    // '\' alike on Windows, while hashing a path doesn't promise to.
    std::map<std::filesystem::path, Directory> m_directories;
};
