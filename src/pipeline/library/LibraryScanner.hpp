#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "../../file/DirectorySnapshot.hpp"

// A supported video found under an added folder.
struct ScanCandidate
{
    std::filesystem::path absolutePath;
    std::string relativePath;  // see LibraryScanner::RelativePathUtf8
    std::uint64_t fileSizeBytes = 0;
    std::int64_t lastWriteUnixSeconds = 0;
};

// Walking an added folder on disk (see Library::ScanFolder). Stateless: it
// only reads the file system, never the library.
namespace LibraryScanner
{

// Every supported video under root, sorted by relative path. Doesn't
// follow symlinks, and skips hidden files/folders and macOS packages.
// Every directory walked, and the subtitle and audio files in it, go into
// snapshot, so the videos' external tracks are found without listing
// anything again.
// std::nullopt when root exists but can't be read (e.g. its bookmark isn't
// active), or when cancelled — distinct from an empty result, which means
// the folder is genuinely gone or has no videos, so the caller knows not to
// drop every location on a mere access failure.
[[nodiscard]] std::optional<std::vector<ScanCandidate>> CollectVideoFiles(
    const std::filesystem::path& root, const std::atomic<bool>& cancel, DirectorySnapshot& snapshot);

// path relative to root, '/'-separated UTF-8 on every platform — the form
// FolderLocation::relativePath is stored in, so library.json means the same
// thing wherever it's read.
[[nodiscard]] std::string RelativePathUtf8(const std::filesystem::path& path, const std::filesystem::path& root);

}  // namespace LibraryScanner
