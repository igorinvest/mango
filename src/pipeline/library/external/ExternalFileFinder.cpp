#include "ExternalFileFinder.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <unordered_set>

#include "../../../configuration/Languages.hpp"
#include "../../../file/File.hpp"

namespace
{
namespace fs = std::filesystem;

std::string ToLower(std::string_view value)
{
    std::string lower(value);
    std::ranges::transform(lower, lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

bool IsSeparator(char c) { return c == '.' || c == '_' || c == '-' || c == ' '; }

bool IsNumber(std::string_view token)
{
    return !token.empty() &&
        std::ranges::all_of(token, [](unsigned char c) { return std::isdigit(c) != 0; });
}

std::vector<std::string_view> SplitTokens(std::string_view text)
{
    std::vector<std::string_view> tokens;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i)
    {
        if (i == text.size() || IsSeparator(text[i]))
        {
            if (i > start) tokens.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    return tokens;
}

std::string Stem(const fs::path& path) { return File::ToUtf8(path.stem()); }

std::string RelativeTitle(const fs::path& path, const fs::path& videoFolder)
{
    const auto relative = path.lexically_relative(videoFolder).generic_u8string();
    return {reinterpret_cast<const char*>(relative.data()), relative.size()};
}

// Applies one name token to tags. shortCodeLanguage collects a 2-letter
// match ("en"), which only counts when nothing longer matched.
void ApplyToken(std::string_view token, ExternalNameTags& tags, std::string& shortCodeLanguage)
{
    const auto lower = ToLower(token);
    if (lower == "forced")
    {
        tags.forced = true;
    }
    else if (lower == "sdh" || lower == "hi" || lower == "cc")
    {
        tags.hearingImpaired = true;
    }
    else if (lower == "commentary")
    {
        tags.commentary = true;
    }
    else if (tags.language.empty() && !IsNumber(token))
    {
        // Two-letter codes are also ordinary words ("it", "no"), so a
        // longer match ("English", "rus") wins over them.
        auto language = Languages::Normalize(token).value_or("");
        auto& target = token.size() <= 2 ? shortCodeLanguage : tags.language;
        if (target.empty()) target = std::move(language);
    }
}
}  // namespace

std::vector<ExternalFileCandidate> ExternalFileFinder::FindCandidates(
    const ExternalFileKind& kind, const DirectorySnapshot& snapshot, const fs::path& videoPath)
{
    const VideoLocation video{videoPath.parent_path(), Stem(videoPath)};

    auto paths = MatchingSiblings(ListFiles(kind, snapshot, video.folder), video.stem);
    std::ranges::move(FilesInSubfolders(kind, snapshot, video, video.folder, 1, false),
        std::back_inserter(paths));

    std::vector<ExternalFileCandidate> candidates;
    for (auto& path : paths)
    {
        auto title = RelativeTitle(path, video.folder);
        candidates.push_back({std::move(path), std::move(title), video.stem});
    }
    return candidates;
}

std::vector<ExternalFileCandidate> ExternalFileFinder::FindCandidates(
    const ExternalFileKind& kind, const DirectorySnapshot& snapshot,
    const std::vector<fs::path>& videoPaths)
{
    std::vector<ExternalFileCandidate> all;
    std::unordered_set<std::string> seenPaths;

    for (const auto& videoPath : videoPaths)
    {
        for (auto& candidate : FindCandidates(kind, snapshot, videoPath))
        {
            if (seenPaths.insert(File::ToUtf8(candidate.path)).second)
            {
                all.push_back(std::move(candidate));
            }
        }
    }

    std::ranges::sort(all, {}, &ExternalFileCandidate::title);
    return all;
}

ExternalNameTags ExternalFileFinder::ParseNameTags(
    std::string_view videoStem, std::string_view fileName)
{
    auto stem = fileName.substr(0, fileName.rfind('.'));
    if (IsNamedAfter(stem, videoStem))
    {
        stem.remove_prefix(videoStem.size());
    }

    ExternalNameTags tags;
    std::string shortCodeLanguage;
    for (const auto token : SplitTokens(stem))
    {
        ApplyToken(token, tags, shortCodeLanguage);
    }

    if (tags.language.empty())
    {
        tags.language = std::move(shortCodeLanguage);
    }
    return tags;
}

ExternalNameTags ExternalFileFinder::CandidateTags(const ExternalFileCandidate& candidate)
{
    auto tags = ParseNameTags(candidate.videoStem, File::ToUtf8(candidate.path.filename()));

    auto folder = File::FromUtf8(candidate.title).parent_path();
    while (tags.language.empty() && !folder.empty())
    {
        tags.language = ParseNameTags({}, File::ToUtf8(folder.filename())).language;
        folder = folder.parent_path();
    }
    return tags;
}

bool ExternalFileFinder::HasKindExtension(const ExternalFileKind& kind, const fs::path& path)
{
    return std::ranges::contains(kind.extensions, ToLower(File::ToUtf8(path.extension())));
}

std::optional<ExternalFileStamp> ExternalFileFinder::Stamp(const fs::path& path)
{
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec)
    {
        return std::nullopt;
    }
    return ExternalFileStamp{size, File::LastWriteUnixSeconds(path).value_or(0)};
}

bool ExternalFileFinder::IsUnchanged(const std::string& path, const ExternalFileStamp& stamp)
{
    return Stamp(File::FromUtf8(path)) == stamp;
}

bool ExternalFileFinder::IsCovered(const DirectorySnapshot& snapshot, const std::string& path)
{
    return snapshot.Covers(File::FromUtf8(path).parent_path());
}

std::vector<fs::path> ExternalFileFinder::ListFiles(
    const ExternalFileKind& kind, const DirectorySnapshot& snapshot, const fs::path& directory)
{
    std::vector<fs::path> files;
    std::ranges::copy_if(snapshot.Files(directory), std::back_inserter(files),
        [&kind](const fs::path& file) { return HasKindExtension(kind, file); });
    return files;
}

std::vector<fs::path> ExternalFileFinder::MatchingSiblings(
    const std::vector<fs::path>& files, std::string_view videoStem)
{
    std::vector<fs::path> matching;
    std::ranges::copy_if(files, std::back_inserter(matching),
        [videoStem](const fs::path& file) { return IsNamedAfter(Stem(file), videoStem); });
    return matching;
}

std::vector<fs::path> ExternalFileFinder::FilesInFolder(const ExternalFileKind& kind,
    const DirectorySnapshot& snapshot, const fs::path& directory, std::string_view videoStem,
    bool isVideosFolder)
{
    auto files = ListFiles(kind, snapshot, directory);

    auto matching = MatchingSiblings(files, videoStem);
    return matching.empty() && isVideosFolder ? files : matching;
}

std::vector<fs::path> ExternalFileFinder::FilesInSubfolders(const ExternalFileKind& kind,
    const DirectorySnapshot& snapshot, const VideoLocation& video, const fs::path& directory,
    int depth, bool isVideosFolder)
{
    std::vector<fs::path> paths;
    if (depth > kMaxFolderDepth)
    {
        return paths;
    }

    for (const auto& subfolder : snapshot.Subfolders(directory))
    {
        if (IsOtherVideosFolder(snapshot, video, subfolder))
        {
            continue;
        }

        const bool isVideos = isVideosFolder || IsVideosFolder(kind, video, subfolder, depth);
        std::ranges::move(FilesInFolder(kind, snapshot, subfolder, video.stem, isVideos),
            std::back_inserter(paths));
        std::ranges::move(
            FilesInSubfolders(kind, snapshot, video, subfolder, depth + 1, isVideos),
            std::back_inserter(paths));
    }
    return paths;
}

bool ExternalFileFinder::IsVideosFolder(
    const ExternalFileKind& kind, const VideoLocation& video, const fs::path& folder, int depth)
{
    const auto name = ToLower(File::ToUtf8(folder.filename()));
    return (depth == 1 && kind.isKindFolder(name)) || name == ToLower(video.stem);
}

bool ExternalFileFinder::IsOtherVideosFolder(
    const DirectorySnapshot& snapshot, const VideoLocation& video, const fs::path& folder)
{
    const auto name = ToLower(File::ToUtf8(folder.filename()));
    if (name == ToLower(video.stem))
    {
        return false;
    }

    return std::ranges::any_of(snapshot.Files(video.folder),
        [&name](const fs::path& file) { return ToLower(Stem(file)) == name; });
}

bool ExternalFileFinder::IsNamedAfter(std::string_view fileStem, std::string_view videoStem)
{
    if (videoStem.empty() || fileStem.size() < videoStem.size() ||
        ToLower(fileStem.substr(0, videoStem.size())) != ToLower(videoStem))
    {
        return false;
    }

    // "Movie 2.srt" belongs to "Movie 2", not "Movie": a space right after
    // the name doesn't count as a separator.
    return fileStem.size() == videoStem.size() ||
        (IsSeparator(fileStem[videoStem.size()]) && fileStem[videoStem.size()] != ' ');
}
