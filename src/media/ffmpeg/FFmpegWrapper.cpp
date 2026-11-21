
#include "FFmpegWrapper.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <optional>
#include "../../file/File.hpp"
#include "../../file/Json.hpp"
#include "../../configuration/Settings.hpp"
#include "install/FFmpegInstalls.hpp"
#include "process/FFprobeProcess.hpp"
#include "../../log/Log.hpp"

// Raw shape of `ffprobe -print_format json -show_format -show_streams`.
// ffprobe stringifies several numeric fields (they can be "N/A"), so those
// are kept as optional<string> and parsed defensively below rather than
// deserialized straight into numeric MediaInfo fields.
struct FFprobeTags
{
    std::optional<std::string> language;
    std::optional<std::string> title;
    // Older files carry their display rotation here instead of side data.
    std::optional<std::string> rotate;
    // Attachment streams only.
    std::optional<std::string> filename;
    std::optional<std::string> mimetype;
};

struct FFprobeDisposition
{
    std::optional<int> attached_pic;
    std::optional<int> isDefault;  // "default"
    std::optional<int> forced;
    std::optional<int> comment;
};

template <>
struct glz::meta<FFprobeDisposition>
{
    using T = FFprobeDisposition;
    static constexpr auto value = object("attached_pic", &T::attached_pic, "default", &T::isDefault, "forced",
        &T::forced, "comment", &T::comment);
};

struct FFprobeSideData
{
    std::optional<std::string> side_data_type;
    std::optional<double> rotation;
};

struct FFprobeStream
{
    int index = 0;
    std::string codec_name;
    std::string codec_type;
    std::optional<int> width;
    std::optional<int> height;
    std::optional<std::string> r_frame_rate;
    std::optional<std::string> avg_frame_rate;
    // "progressive", "tt", "bb", "tb", "bt" or "unknown".
    std::optional<std::string> field_order;
    std::optional<std::string> bit_rate;
    std::optional<int> channels;
    std::optional<std::string> sample_rate;
    std::optional<std::string> start_time;
    std::optional<std::string> duration;
    std::optional<FFprobeTags> tags;
    std::optional<FFprobeDisposition> disposition;
    std::optional<std::vector<FFprobeSideData>> side_data_list;
};

struct FFprobeFormat
{
    std::optional<std::string> format_long_name;
    std::optional<std::string> duration;
    std::optional<std::string> start_time;
    std::optional<FFprobeTags> tags;
};

struct FFprobeOutput
{
    std::vector<FFprobeStream> streams;
    FFprobeFormat format;
};

namespace
{
double ParseFrameRate(const std::string &rate)
{
    const auto slash = rate.find('/');
    if (slash == std::string::npos)
    {
        return 0.0;
    }

    try
    {
        const double numerator = std::stod(rate.substr(0, slash));
        const double denominator = std::stod(rate.substr(slash + 1));

        return denominator != 0.0 ? numerator / denominator : 0.0;
    }
    catch (...)
    {
        return 0.0;
    }
}

int64_t ParseInt64(const std::optional<std::string> &value, int64_t fallback = 0)
{
    if (!value) return fallback;

    try
    {
        return std::stoll(*value);
    }
    catch (...)
    {
        return fallback;
    }
}

int ParseInt(const std::optional<std::string> &value, int fallback = 0)
{
    if (!value) return fallback;

    try
    {
        return std::stoi(*value);
    }
    catch (...)
    {
        return fallback;
    }
}

double ParseDouble(const std::optional<std::string> &value, double fallback = 0.0)
{
    if (!value) return fallback;

    try
    {
        return std::stod(*value);
    }
    catch (...)
    {
        return fallback;
    }
}

std::string TagLanguage(const std::optional<FFprobeTags> &tags)
{
    return tags && tags->language ? *tags->language : "";
}

std::string TagTitle(const std::optional<FFprobeTags> &tags)
{
    return tags && tags->title ? *tags->title : "";
}

// Snaps an angle in degrees (ffprobe reports e.g. -90) to 0, 90, 180 or 270.
int NormalizeRotation(double degrees)
{
    const auto quarterTurns = static_cast<int>(std::lround(degrees / 90.0));
    return ((quarterTurns % 4) + 4) % 4 * 90;
}

int StreamRotation(const FFprobeStream &stream)
{
    for (const auto &sideData : stream.side_data_list.value_or(std::vector<FFprobeSideData>{}))
    {
        if (sideData.side_data_type == "Display Matrix" && sideData.rotation)
        {
            return NormalizeRotation(*sideData.rotation);
        }
    }

    if (stream.tags && stream.tags->rotate)
    {
        return NormalizeRotation(ParseDouble(stream.tags->rotate));
    }

    return 0;
}

// Any field order but "progressive" / "unknown" (or none reported) means
// the frames are coded as two fields.
bool StreamInterlaced(const FFprobeStream &stream)
{
    const auto order = stream.field_order.value_or("progressive");
    return order == "tt" || order == "bb" || order == "tb" || order == "bt";
}

// Fills the VideoTrack fields older records lack from their probe stream.
void BackfillVideoTrack(VideoTrack &track, const FFprobeStream *stream)
{
    if (!track.rotation)
    {
        track.rotation = stream ? StreamRotation(*stream) : 0;
    }
    if (!track.interlaced)
    {
        track.interlaced = stream ? StreamInterlaced(*stream) : false;
    }
}

bool NeedsBackfill(const VideoTrack &track)
{
    return !track.rotation || !track.interlaced;
}

bool DispositionFlag(const FFprobeStream &stream, std::optional<int> FFprobeDisposition::*flag)
{
    return stream.disposition && (*stream.disposition.*flag).value_or(0) == 1;
}

void ReadAudioFlags(AudioTrack &track, const FFprobeStream *stream)
{
    track.isDefault = stream && DispositionFlag(*stream, &FFprobeDisposition::isDefault);
    track.commentary = stream && DispositionFlag(*stream, &FFprobeDisposition::comment);
}

void ReadSubtitleFlags(SubtitleTrack &track, const FFprobeStream *stream)
{
    track.isDefault = stream && DispositionFlag(*stream, &FFprobeDisposition::isDefault);
    track.forced = stream && DispositionFlag(*stream, &FFprobeDisposition::forced);
}

bool NeedsAudioBackfill(const AudioTrack &track) { return !track.isDefault || !track.commentary; }

bool NeedsSubtitleBackfill(const SubtitleTrack &track) { return !track.isDefault || !track.forced; }

bool NeedsAnyBackfill(const MediaInfo &info)
{
    return std::ranges::any_of(info.videos, NeedsBackfill) || std::ranges::any_of(info.audios, NeedsAudioBackfill) ||
           std::ranges::any_of(info.subtitles, NeedsSubtitleBackfill);
}

// The probe stream with index, or nullptr.
const FFprobeStream *FindStream(const FFprobeOutput &probe, int index)
{
    const auto stream = std::ranges::find(probe.streams, index, &FFprobeStream::index);
    return stream != probe.streams.end() ? &*stream : nullptr;
}

VideoTrack ToVideoTrack(const FFprobeStream &stream)
{
    VideoTrack track;

    track.index = stream.index;
    track.codec = stream.codec_name;
    track.width = stream.width.value_or(0);
    track.height = stream.height.value_or(0);

    track.fps = ParseFrameRate(stream.avg_frame_rate.value_or("0/0"));
    if (track.fps == 0.0)
    {
        track.fps = ParseFrameRate(stream.r_frame_rate.value_or("0/0"));
    }

    track.bitrate = ParseInt64(stream.bit_rate);
    track.language = TagLanguage(stream.tags);
    track.title = TagTitle(stream.tags);
    track.rotation = StreamRotation(stream);
    track.interlaced = StreamInterlaced(stream);
    return track;
}

AudioTrack ToAudioTrack(const FFprobeStream &stream)
{
    AudioTrack track;

    track.index = stream.index;
    track.codec = stream.codec_name;
    track.channels = stream.channels.value_or(0);
    track.sampleRate = ParseInt(stream.sample_rate);
    track.bitrate = ParseInt64(stream.bit_rate);
    track.language = TagLanguage(stream.tags);
    track.title = TagTitle(stream.tags);
    ReadAudioFlags(track, &stream);
    return track;
}

SubtitleTrack ToSubtitleTrack(const FFprobeStream &stream)
{
    SubtitleTrack track;

    track.index = stream.index;
    track.codec = stream.codec_name;
    track.language = TagLanguage(stream.tags);
    track.title = TagTitle(stream.tags);
    ReadSubtitleFlags(track, &stream);
    return track;
}

AttachmentTrack ToAttachmentTrack(const FFprobeStream &stream, int attachmentIndex)
{
    return AttachmentTrack{.index = stream.index,
        .attachmentIndex = attachmentIndex,
        .fileName = stream.tags ? stream.tags->filename.value_or("") : "",
        .mimeType = stream.tags ? stream.tags->mimetype.value_or("") : ""};
}

bool IsAttachedPicture(const FFprobeStream &stream)
{
    return stream.disposition && stream.disposition->attached_pic.value_or(0) == 1;
}

// attachmentIndex counts the attachment streams seen so far.
void AddStream(MediaInfo &metadata, const FFprobeStream &stream, int &attachmentIndex)
{
    if (stream.codec_type == "video")
    {
        // Still listed as a video track too, as before.
        metadata.videos.push_back(ToVideoTrack(stream));
        if (IsAttachedPicture(stream))
        {
            metadata.attachedPictures.push_back({.index = stream.index, .codec = stream.codec_name});
        }
    }
    else if (stream.codec_type == "audio")
    {
        metadata.audios.push_back(ToAudioTrack(stream));
    }
    else if (stream.codec_type == "subtitle")
    {
        metadata.subtitles.push_back(ToSubtitleTrack(stream));
    }
    else if (stream.codec_type == "attachment")
    {
        metadata.attachments.push_back(ToAttachmentTrack(stream, attachmentIndex++));
    }
}
}  // namespace

namespace
{
// The primary install. With none (Linux without ffmpeg), files can't be
// probed: that's logged once, not for every file of a scan.
const FFmpegInstall *PrimaryInstall()
{
    const auto *install = FFmpegInstalls::Primary();
    if (!install)
    {
        static std::once_flag logged;
        std::call_once(logged,
            [] { Log::Warning(LogCategory::Library, "no ffmpeg found: media files aren't probed"); });
    }
    return install;
}

// Runs ffprobe on filePath, keeping its raw report as this file's
// metadata.json (the backend's private cache, see BackfillTrackFields).
std::optional<FFprobeOutput> RunProbe(const std::filesystem::path &filePath)
{
    const auto fastHash = File::FastHash(filePath);
    if (!fastHash)
    {
        return std::nullopt;
    }

    // Mirrors FileRecord::getMetadataFolder()/getMetadataFilePath() - this
    // runs before a FileRecord exists for the file, so it computes the same
    // path independently rather than needing one.
    const auto metadataFolder = File::getApplicationDataPath() /
        Settings::GetStringSetting("libraryMetadataFolder").value_or("library-metadata") / *fastHash;

    if (!File::CreateDirectoryPath(metadataFolder))
    {
        return std::nullopt;
    }

    const auto outputPath = metadataFolder / "metadata.json";

    const std::vector<std::string> args = {
        "-v", "quiet", "-print_format", "json", "-show_format", "-show_streams",
        File::ToUtf8(filePath)};

    const auto* install = PrimaryInstall();
    auto process = install ? FFprobeProcess::Start(*install, args, outputPath) : nullptr;
    if (!process)
    {
        return std::nullopt;
    }

    if (!process->Wait())
    {
        Log::Warning(LogCategory::Library, "ffprobe failed for {}", File::ToUtf8(filePath));

        return std::nullopt;
    }

    return Json::ReadJsonFromFileLenient<FFprobeOutput>(outputPath);
}

MediaInfo ToMediaInfo(const FFprobeOutput &probe, const std::filesystem::path &filePath)
{
    MediaInfo metadata;
    int attachmentIndex = 0;

    for (const auto &stream : probe.streams)
    {
        AddStream(metadata, stream, attachmentIndex);
    }

    metadata.durationSec = static_cast<int64_t>(ParseDouble(probe.format.duration));
    metadata.fileSizeBytes = ParseInt64(File::FileSize(filePath));
    metadata.format = probe.format.format_long_name.value_or("");
    metadata.title = TagTitle(probe.format.tags);
    return metadata;
}
}  // namespace

namespace
{
// A file name for one probe report that no other probe running at the same
// time uses.
std::filesystem::path TemporaryReportPath()
{
    static std::atomic<unsigned> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();

    std::error_code ec;
    const auto folder = std::filesystem::temp_directory_path(ec);
    return (ec ? File::getApplicationDataPath() : folder) /
        ("mango-audio-probe-" + std::to_string(stamp) + "-" + std::to_string(counter++) +
            ".json");
}

std::optional<FFprobeOutput> RunAudioProbe(const std::filesystem::path &filePath)
{
    const auto reportPath = TemporaryReportPath();
    const std::vector<std::string> args = {"-v", "quiet", "-print_format", "json",
        "-show_format", "-show_streams", "-select_streams", "a:0", File::ToUtf8(filePath)};

    std::optional<FFprobeOutput> probe;
    const auto* install = PrimaryInstall();
    if (auto process = install ? FFprobeProcess::Start(*install, args, reportPath) : nullptr;
        process && process->Wait())
    {
        probe = Json::ReadJsonFromFileLenient<FFprobeOutput>(reportPath);
    }

    std::error_code ec;
    std::filesystem::remove(reportPath, ec);
    return probe;
}

// A stream's own timing when it has one, otherwise the container's.
double StreamOrFormat(const std::optional<std::string> &streamValue,
    const std::optional<std::string> &formatValue)
{
    return ParseDouble(streamValue, ParseDouble(formatValue));
}

AudioFileInfo ToAudioFileInfo(const FFprobeStream &stream, const FFprobeFormat &format)
{
    const auto track = ToAudioTrack(stream);
    return AudioFileInfo{.codec = track.codec,
        .channels = track.channels,
        .sampleRate = track.sampleRate,
        .bitrate = track.bitrate,
        .durationSeconds = StreamOrFormat(stream.duration, format.duration),
        .startTimeSeconds = StreamOrFormat(stream.start_time, format.start_time)};
}
}  // namespace

std::optional<AudioFileInfo> FFmpegWrapper::ReadAudioFile(const std::filesystem::path &filePath)
{
    const auto probe = RunAudioProbe(filePath);
    if (!probe)
    {
        return std::nullopt;
    }

    const auto stream = std::ranges::find(probe->streams, "audio", &FFprobeStream::codec_type);
    if (stream == probe->streams.end())
    {
        return std::nullopt;
    }
    return ToAudioFileInfo(*stream, probe->format);
}

std::optional<MediaInfo> FFmpegWrapper::Read(const std::filesystem::path &filePath)
{
    if (!File::Exists(filePath))
    {
        return std::nullopt;
    }

    const auto probe = RunProbe(filePath);
    if (!probe)
    {
        return std::nullopt;
    }
    return ToMediaInfo(*probe, filePath);
}

bool FFmpegWrapper::BackfillTrackFields(MediaInfo &info, const std::filesystem::path &metadataFilePath)
{
    if (!NeedsAnyBackfill(info))
    {
        return false;
    }

    const auto probe = Json::ReadJsonFromFileLenient<FFprobeOutput>(metadataFilePath);
    if (!probe)
    {
        return false;
    }

    for (auto &track : info.videos)
    {
        BackfillVideoTrack(track, FindStream(*probe, track.index));
    }
    for (auto &track : info.audios)
    {
        ReadAudioFlags(track, FindStream(*probe, track.index));
    }
    for (auto &track : info.subtitles)
    {
        ReadSubtitleFlags(track, FindStream(*probe, track.index));
    }

    return true;
}

void FFmpegWrapper::PrintMetadata(const MediaInfo &metadata)
{
    if (!Log::IsEnabled(LogLevel::Debug))
    {
        return;
    }

    Log::Debug(LogCategory::Library, "media info: title \"{}\", format {}, {} s, {} MB", metadata.title,
        metadata.format, metadata.durationSec, metadata.fileSizeBytes / (1024 * 1024));

    for (const auto &video : metadata.videos)
    {
        Log::Debug(LogCategory::Library, "  video #{}: {} {}x{} {} fps, bitrate {}, {} \"{}\"", video.index,
            video.codec, video.width, video.height, video.fps, video.bitrate, video.language, video.title);
    }

    for (const auto &audio : metadata.audios)
    {
        Log::Debug(LogCategory::Library, "  audio #{}: {} {} ch {} Hz, bitrate {}, {} \"{}\"", audio.index,
            audio.codec, audio.channels, audio.sampleRate, audio.bitrate, audio.language, audio.title);
    }

    for (const auto &subtitle : metadata.subtitles)
    {
        Log::Debug(LogCategory::Library, "  subtitle #{}: {} {} \"{}\"", subtitle.index, subtitle.codec,
            subtitle.language, subtitle.title);
    }
}
