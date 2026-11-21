#include "CoverExtractor.hpp"

#include <algorithm>
#include <cctype>

#include "../../file/File.hpp"

namespace
{

std::string ToLower(std::string value)
{
    std::ranges::transform(
        value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

// Kodi/Jellyfin's long-standing convention for an MKV cover attachment.
bool LooksLikeCoverFilename(const std::string& filename)
{
    const auto lower = ToLower(filename);
    return lower.starts_with("cover") || lower.starts_with("poster") || lower.starts_with("folder");
}

bool IsCoverAttachment(const AttachmentTrack& attachment)
{
    return LooksLikeCoverFilename(attachment.fileName) || attachment.mimeType.starts_with("image/");
}

std::string ExtensionForAttachment(const AttachmentTrack& attachment)
{
    if (!attachment.fileName.empty())
    {
        auto ext = std::filesystem::path(attachment.fileName).extension().string();
        if (!ext.empty())
        {
            return ext;
        }
    }

    return attachment.mimeType == "image/png" ? ".png" : ".jpg";
}

std::string ExtensionForCodec(const std::string& codecName)
{
    return codecName == "png" ? ".png" : ".jpg";
}

}  // namespace

std::optional<std::string> CoverExtractor::Extract(const FileRecord& fileRecord)
{
    const auto coverFolder = fileRecord.getMetadataFolder();
    if (!File::CreateDirectoryPath(coverFolder))
    {
        return std::nullopt;
    }

    if (const auto embedded = FindEmbedded(fileRecord.mediaInfo))
    {
        if (auto written = Write(fileRecord, coverFolder, *embedded))
        {
            return written;
        }
    }

    if (const auto frame = FindFrame(fileRecord.mediaInfo))
    {
        return Write(fileRecord, coverFolder, *frame);
    }
    return std::nullopt;
}

std::optional<CoverExtractor::Candidate> CoverExtractor::FindEmbedded(const MediaInfo& info)
{
    // An MKV-style attachment named like cover.jpg/poster.jpg wins over an
    // attached picture, which is sometimes just a low-resolution thumbnail
    // while the former is the actual poster.
    const auto attachment = std::ranges::find_if(info.attachments, IsCoverAttachment);
    if (attachment != info.attachments.end())
    {
        return Candidate{.source = {.kind = CoverSource::Kind::Attachment,
                             .attachmentIndex = attachment->attachmentIndex,
                             .streamIndex = attachment->index},
            .extension = ExtensionForAttachment(*attachment)};
    }

    if (!info.attachedPictures.empty())
    {
        const auto& picture = info.attachedPictures.front();
        return Candidate{
            .source = {.kind = CoverSource::Kind::AttachedPicture, .streamIndex = picture.index},
            .extension = ExtensionForCodec(picture.codec)};
    }

    return std::nullopt;
}

std::optional<CoverExtractor::Candidate> CoverExtractor::FindFrame(const MediaInfo& info)
{
    if (info.videos.empty() || info.durationSec <= 0)
    {
        return std::nullopt;
    }

    return Candidate{
        .source = {.kind = CoverSource::Kind::Frame, .frameSeconds = info.durationSec / 10},
        .extension = ".jpg"};
}

std::optional<std::string> CoverExtractor::Write(const FileRecord& fileRecord,
    const std::filesystem::path& coverFolder, const Candidate& candidate)
{
    const auto fileName = "cover" + candidate.extension;
    if (!MediaBackends::Get().ExtractCover(fileRecord, candidate.source, coverFolder / fileName))
    {
        return std::nullopt;
    }
    return fileName;
}
