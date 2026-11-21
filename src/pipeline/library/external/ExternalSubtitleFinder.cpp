#include "ExternalSubtitleFinder.hpp"

#include <algorithm>
#include <cctype>

#include "../../../file/File.hpp"
#include "../../stream/subtitles/SubtitleCharset.hpp"

namespace fs = std::filesystem;

const ExternalFileKind& ExternalSubtitleFinder::Kind()
{
    static const ExternalFileKind kind{.extensions = {".srt", ".vtt", ".ass", ".ssa"},
        .isKindFolder = &IsSubsFolder};
    return kind;
}

std::vector<ExternalFileCandidate> ExternalSubtitleFinder::FindCandidates(
    const DirectorySnapshot& snapshot, const std::vector<fs::path>& videoPaths)
{
    return ExternalFileFinder::FindCandidates(Kind(), snapshot, videoPaths);
}

bool ExternalSubtitleFinder::MergeInto(FileRecord& record,
    const std::vector<ExternalFileCandidate>& candidates, const DirectorySnapshot& snapshot)
{
    std::vector<ExternalSubtitle> merged;

    for (const auto& candidate : candidates)
    {
        const auto path = File::ToUtf8(candidate.path);
        const auto known =
            std::ranges::find(record.externalSubtitles, path, &ExternalSubtitle::path);

        if (known == record.externalSubtitles.end())
        {
            merged.push_back(Describe(candidate, TakeNextIndex(record)));
        }
        else if (ExternalFileFinder::IsUnchanged(
                     known->path, {known->fileSizeBytes, known->lastWriteUnixSeconds}))
        {
            merged.push_back(*known);
            merged.back().title = candidate.title;
        }
        else
        {
            merged.push_back(Describe(candidate, known->index));
        }
    }

    for (const auto& known : record.externalSubtitles)
    {
        if (!ExternalFileFinder::IsCovered(snapshot, known.path) &&
            !std::ranges::contains(merged, known.path, &ExternalSubtitle::path))
        {
            merged.push_back(known);
        }
    }

    std::ranges::sort(merged, {}, &ExternalSubtitle::index);

    if (merged == record.externalSubtitles)
    {
        return false;
    }

    record.externalSubtitles = std::move(merged);
    return true;
}

std::optional<std::string> ExternalSubtitleFinder::CodecForExtension(std::string_view extension)
{
    std::string lower(extension);
    std::ranges::transform(lower, lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower == ".srt") return "subrip";
    if (lower == ".vtt") return "webvtt";
    if (lower == ".ass") return "ass";
    if (lower == ".ssa") return "ssa";
    return std::nullopt;
}

bool ExternalSubtitleFinder::IsSubsFolder(std::string_view lowerFolderName)
{
    return lowerFolderName == "subs" || lowerFolderName == "subtitles" ||
        lowerFolderName.ends_with(" subs") || lowerFolderName.ends_with(" subtitles");
}

ExternalSubtitle ExternalSubtitleFinder::Describe(
    const ExternalFileCandidate& candidate, int index)
{
    const auto tags = ExternalFileFinder::CandidateTags(candidate);
    const auto stamp = ExternalFileFinder::Stamp(candidate.path).value_or(ExternalFileStamp{});

    ExternalSubtitle subtitle;
    subtitle.index = index;
    subtitle.path = File::ToUtf8(candidate.path);
    subtitle.language = tags.language;
    subtitle.title = candidate.title;
    subtitle.forced = tags.forced;
    subtitle.hearingImpaired = tags.hearingImpaired;
    subtitle.codec =
        CodecForExtension(File::ToUtf8(candidate.path.extension())).value_or("subrip");
    subtitle.charset = SubtitleCharset::Detect(candidate.path, tags.language);
    subtitle.fileSizeBytes = stamp.fileSizeBytes;
    subtitle.lastWriteUnixSeconds = stamp.lastWriteUnixSeconds;
    return subtitle;
}

int ExternalSubtitleFinder::TakeNextIndex(FileRecord& record)
{
    int next = std::max(record.nextExternalSubtitleIndex, kExternalSubtitleIndexBase);
    for (const auto& subtitle : record.externalSubtitles)
    {
        next = std::max(next, subtitle.index + 1);
    }

    record.nextExternalSubtitleIndex = next + 1;
    return next;
}
