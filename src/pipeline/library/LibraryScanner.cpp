#include "LibraryScanner.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

#include "../../configuration/VideoFileTypes.hpp"
#include "../../file/File.hpp"
#include "../../log/Log.hpp"
#include "external/ExternalAudioFinder.hpp"
#include "external/ExternalSubtitleFinder.hpp"

namespace
{
namespace fs = std::filesystem;

std::string ToLower(std::string value)
{
    std::ranges::transform(value, value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool IsSupportedVideo(const fs::path& path)
{
    const auto extension = ToLower(File::ToUtf8(path.extension()));
    return std::ranges::any_of(VideoFileTypes::GetSupportedTypes(),
        [&extension](const VideoFileType& type) { return type.extension == extension; });
}

// macOS shows these directories as single files (packages) — a video
// inside an app bundle or a Photos library isn't something the user put in
// their folder.
bool IsPackage(const fs::path& directory)
{
    static const std::unordered_set<std::string> packageExtensions = {".app", ".bundle",
        ".framework", ".plugin", ".kext", ".photoslibrary", ".fcpbundle", ".imovielibrary",
        ".tvlibrary", ".musiclibrary", ".theater", ".rtfd", ".xcodeproj", ".xcworkspace"};

    return packageExtensions.contains(ToLower(File::ToUtf8(directory.extension())));
}

bool IsHidden(const fs::path& path)
{
    const auto name = File::ToUtf8(path.filename());
    return !name.empty() && name.front() == '.';
}

// A file a video's external subtitle or audio track could come from.
bool IsExternalTrackFile(const fs::path& path)
{
    return ExternalFileFinder::HasKindExtension(ExternalSubtitleFinder::Kind(), path) ||
        ExternalFileFinder::HasKindExtension(ExternalAudioFinder::Kind(), path);
}

// Opens root for walking and adds it to snapshot. std::nullopt (logged) when
// it can't be read; an end iterator when it doesn't exist.
std::optional<fs::recursive_directory_iterator> OpenRoot(const fs::path& root, DirectorySnapshot& snapshot)
{
    std::error_code ec;
    const auto status = fs::status(root, ec);
    if (status.type() == fs::file_type::not_found)
    {
        return fs::recursive_directory_iterator();
    }

    if (ec || !fs::is_directory(status))
    {
        Log::Warning(LogCategory::Library, "can't read folder {}: {}", File::ToUtf8(root), ec.message());
        return std::nullopt;
    }

    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    if (ec)
    {
        Log::Warning(LogCategory::Library, "can't open folder {}: {}", File::ToUtf8(root), ec.message());
        return std::nullopt;
    }
    snapshot.AddDirectory(root);
    return it;
}

// Hidden folders and packages aren't walked; every other folder goes into
// snapshot.
void VisitDirectory(fs::recursive_directory_iterator& it, DirectorySnapshot& snapshot)
{
    const auto& path = it->path();
    if (IsHidden(path) || IsPackage(path))
    {
        it.disable_recursion_pending();
    }
    else
    {
        snapshot.AddDirectory(path);
    }
}

// Adds a video or external track file to snapshot, and returns the video as
// a candidate. Videos go into the snapshot too: ExternalFileFinder tells
// another episode's folder (Subs/S01E02/) by its video next to it.
std::optional<ScanCandidate> VisitFile(
    const fs::directory_entry& entry, const fs::path& root, DirectorySnapshot& snapshot)
{
    std::error_code ec;
    if (IsHidden(entry.path()) || !entry.is_regular_file(ec))
    {
        return std::nullopt;
    }

    const bool isVideo = IsSupportedVideo(entry.path());
    if (isVideo || IsExternalTrackFile(entry.path()))
    {
        snapshot.AddFile(entry.path());
    }
    if (!isVideo)
    {
        return std::nullopt;
    }

    ScanCandidate candidate;
    candidate.absolutePath = entry.path();
    candidate.relativePath = LibraryScanner::RelativePathUtf8(entry.path(), root);
    candidate.fileSizeBytes = entry.file_size(ec);
    candidate.lastWriteUnixSeconds = File::LastWriteUnixSeconds(entry.path()).value_or(0);
    return candidate;
}
}  // namespace

namespace LibraryScanner
{

std::optional<std::vector<ScanCandidate>> CollectVideoFiles(
    const fs::path& root, const std::atomic<bool>& cancel, DirectorySnapshot& snapshot)
{
    auto opened = OpenRoot(root, snapshot);
    if (!opened)
    {
        return std::nullopt;
    }

    std::vector<ScanCandidate> candidates;
    std::error_code ec;
    for (auto& it = *opened; it != fs::recursive_directory_iterator(); it.increment(ec))
    {
        if (ec)
        {
            Log::Warning(LogCategory::Library, "stopped walking {}: {}", File::ToUtf8(root), ec.message());
            break;
        }

        if (cancel)
        {
            return std::nullopt;
        }

        std::error_code entryEc;
        if (it->is_symlink(entryEc))
        {
            continue;
        }

        if (it->is_directory(entryEc))
        {
            VisitDirectory(it, snapshot);
        }
        else if (auto candidate = VisitFile(*it, root, snapshot))
        {
            candidates.push_back(std::move(*candidate));
        }
    }

    std::ranges::sort(candidates, {}, &ScanCandidate::relativePath);
    return candidates;
}

std::string RelativePathUtf8(const fs::path& path, const fs::path& root)
{
    const auto utf8 = path.lexically_relative(root).generic_u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

}  // namespace LibraryScanner
