#include "FfmpegBackend.hpp"

#include <algorithm>

#include "../../file/File.hpp"
#include "conversion/FFmpegArgumentFormatter.hpp"
#include "process/FFmpegProcess.hpp"
#include "FFmpegWrapper.hpp"
#include "conversion/FfmpegConversion.hpp"
#include "install/FFmpegInstalls.hpp"
#include "FFmpegStartupProbe.hpp"
#include "encoders/EncoderDrivers.hpp"
#include "encoders/VideoEncoderProbe.hpp"
#include "../../log/Log.hpp"

std::optional<MediaInfo> FfmpegBackend::Probe(const std::filesystem::path& file)
{
    return FFmpegWrapper::Read(file);
}

std::optional<AudioFileInfo> FfmpegBackend::ProbeAudioFile(const std::filesystem::path& file)
{
    return FFmpegWrapper::ReadAudioFile(file);
}

bool FfmpegBackend::BackfillMediaInfo(const FileRecord& file, MediaInfo& info)
{
    return FFmpegWrapper::BackfillTrackFields(info, file.getMetadataFilePath());
}

std::expected<std::unique_ptr<Conversion>, ConversionError> FfmpegBackend::StartConversion(
    const ConversionRequest& request, ConversionExitCallback onExit)
{
    if (const auto error = CheckRequest(request))
    {
        return std::unexpected(*error);
    }

    const auto run = PlanRun(request);
    if (!run)
    {
        return std::unexpected(run.error());
    }

    // A spec without a driver runs its encoder with standard arguments.
    const StandardEncoderDriver namedEncoder({.name = request.targets.video.codec,
        .encoder = request.targets.video.codec,
        .platforms = {StandardEncoderDriver::Platform::any},
        .software = false,
        .options = {},
        .requiredOption = ""});
    const auto& videoDriver = run->driver ? *run->driver : namedEncoder;

    auto conversion = FfmpegConversion::Start(*run->install,
        FFmpegArgumentFormatter::Format(request, videoDriver), request.outputFolder,
        std::move(onExit));
    if (!conversion)
    {
        Log::Error(LogCategory::Transcode, "failed to launch ffmpeg");
        return std::unexpected(ConversionError::Unknown);
    }
    return conversion;
}

std::optional<ConversionError> FfmpegBackend::CheckRequest(const ConversionRequest& request)
{
    if (!HasTools())
    {
        return ConversionError::FfmpegMissing;
    }

    if (!File::Exists(request.file.filePath))
    {
        return ConversionError::FileUnreadable;
    }

    if (EncodesVideo(request) && request.targets.video.codec.empty())
    {
        return ConversionError::NoVideoEncoder;
    }
    return std::nullopt;
}

std::unique_ptr<MediaJob> FfmpegBackend::StartSubtitleExtraction(
    const FileRecord& file, const std::vector<SubtitleOutput>& outputs, JobExitCallback onExit)
{
    return StartPrimary(SubtitleExtractionArguments(file, outputs), std::move(onExit));
}

std::unique_ptr<MediaJob> FfmpegBackend::StartSubtitleFileConversion(
    const std::filesystem::path& input, const std::filesystem::path& output, JobExitCallback onExit)
{
    // "-f webvtt" is explicit because the output's temporary extension
    // doesn't let ffmpeg infer the muxer.
    return StartPrimary({"-loglevel", "error", "-y", "-i", File::ToUtf8(input), "-map",
                                    "0:s:0", "-c:s", "webvtt", "-f", "webvtt", File::ToUtf8(output)},
        std::move(onExit));
}

bool FfmpegBackend::ExtractCover(
    const FileRecord& file, const CoverSource& source, const std::filesystem::path& output)
{
    // ffmpeg's exit status isn't trusted either way: what counts is whether
    // the image got written.
    RunToCompletion(CoverArguments(file, source, output));
    return File::Exists(output);
}

void FfmpegBackend::StartProbe() { FFmpegStartupProbe::Get().Start(); }

void FfmpegBackend::StopProbe() { FFmpegStartupProbe::Get().Stop(); }

bool FfmpegBackend::HasTools() { return !FFmpegInstalls::All().empty(); }

std::vector<std::string> FfmpegBackend::DescribeTools()
{
    const auto& installs = FFmpegInstalls::All();

    std::vector<std::string> lines;
    for (const auto& install : installs)
    {
        lines.push_back(DescribeInstall(install, &install == &installs.front()));
    }
    return lines;
}

std::string FfmpegBackend::DescribeInstall(const FFmpegInstall& install, bool primary)
{
    std::string line = std::string(FFmpegInstall::OriginName(install.origin)) +
        (primary ? " (primary)" : "") + ": " + File::ToUtf8(install.ffmpeg) + " " +
        (install.version.empty() ? "(unknown version)" : install.version);

    const auto h264 = install.encodersByCodec.find("h264");
    line += ", h264:";
    if (h264 == install.encodersByCodec.end())
    {
        line += " none";
    }
    else
    {
        for (const auto& encoder : h264->second)
        {
            line += " " + encoder;
        }
    }

    for (const auto* muxer : {"dash", "hls"})
    {
        line += std::string(", ") + muxer + (install.HasMuxer(muxer) ? "" : " missing");
    }
    return line;
}

std::optional<VideoEncoderOption> FfmpegBackend::VideoEncoder(const StreamProfile& profile)
{
    const auto* option = Choose(VideoEncoderProbe::Options(profile.playlist), profile);
    return option ? std::optional(Describe(*option)) : std::nullopt;
}

const ConversionOption* FfmpegBackend::Choose(
    const std::vector<ConversionOption>& options, const StreamProfile& profile)
{
    const auto match = std::ranges::find_if(options,
        [&profile](const ConversionOption& option) { return Accepts(profile, option.codec); });
    return match != options.end() ? &*match : nullptr;
}

std::vector<VideoEncoderOption> FfmpegBackend::ConversionOptions(PlaylistType playlist)
{
    std::vector<VideoEncoderOption> options;
    for (const auto& option : VideoEncoderProbe::Options(playlist))
    {
        options.push_back(Describe(option));
    }
    return options;
}

bool FfmpegBackend::Accepts(const StreamProfile& profile, const std::string& codec)
{
    return std::ranges::any_of(profile.videoCapabilities,
        [&codec](const VideoCodecConfig& entry) { return entry.codec == codec; });
}

VideoEncoderOption FfmpegBackend::Describe(const ConversionOption& option)
{
    return {.codec = option.codec,
        .driver = option.driver->Name(),
        .encoder = option.driver->Encoder(),
        .software = option.driver->IsSoftware(),
        .external = option.install->origin != FFmpegInstall::Origin::bundled};
}

bool FfmpegBackend::EncodesVideo(const ConversionRequest& request)
{
    return request.tracks.videoTrack && !request.targets.video.copy;
}

std::expected<FfmpegBackend::RunPlan, ConversionError> FfmpegBackend::PlanRun(
    const ConversionRequest& request)
{
    const auto& driverName = request.targets.video.driver;
    if (EncodesVideo(request) && !driverName.empty())
    {
        // The whole conversion runs in the option's install.
        const auto& options = VideoEncoderProbe::Options(request.profile.playlist);
        const auto match = std::ranges::find_if(options,
            [&driverName](const ConversionOption& option) { return option.driver->Name() == driverName; });
        if (match == options.end())
        {
            Log::Error(LogCategory::Transcode, "no conversion option with driver {}", driverName);
            return std::unexpected(ConversionError::NoVideoEncoder);
        }
        return RunPlan{.install = match->install, .driver = match->driver};
    }

    const auto* install = FFmpegInstalls::WithMuxer(VideoEncoderProbe::MuxerFor(request.profile.playlist));
    if (!install)
    {
        Log::Error(LogCategory::Transcode, "no ffmpeg install has the {} muxer",
            VideoEncoderProbe::MuxerFor(request.profile.playlist));
        return std::unexpected(ConversionError::Unknown);
    }
    return RunPlan{.install = install, .driver = nullptr};
}

std::vector<std::string> FfmpegBackend::SubtitleExtractionArguments(
    const FileRecord& file, const std::vector<SubtitleOutput>& outputs)
{
    std::vector<std::string> args = {"-loglevel", "error", "-y", "-i", File::ToUtf8(file.filePath)};

    // One output per track; "-f webvtt" as in StartSubtitleFileConversion.
    for (const auto& output : outputs)
    {
        args.insert(args.end(), {"-map", "0:" + std::to_string(output.trackIndex), "-c:s",
                                    "webvtt", "-f", "webvtt", File::ToUtf8(output.path)});
    }
    return args;
}

std::vector<std::string> FfmpegBackend::CoverArguments(
    const FileRecord& file, const CoverSource& source, const std::filesystem::path& output)
{
    const auto input = File::ToUtf8(file.filePath);
    const auto outputPath = File::ToUtf8(output);

    switch (source.kind)
    {
        case CoverSource::Kind::Attachment:
            // -dump_attachment is an input option and needs an (unused)
            // output on the command line - "-f null -" is the standard
            // no-op for that.
            return {"-loglevel", "error", "-y",
                "-dump_attachment:t:" + std::to_string(source.attachmentIndex), outputPath, "-i",
                input, "-f", "null", "-"};

        case CoverSource::Kind::AttachedPicture:
            return {"-loglevel", "error", "-y", "-i", input, "-map",
                "0:" + std::to_string(source.streamIndex), "-c", "copy", outputPath};

        case CoverSource::Kind::Frame:
        default:
            // -ss before -i is ffmpeg's fast, input-seeking form; frame
            // accuracy doesn't matter for a thumbnail.
            return {"-loglevel", "error", "-y", "-ss", std::to_string(source.frameSeconds), "-i",
                input, "-frames:v", "1", outputPath};
    }
}

std::unique_ptr<MediaJob> FfmpegBackend::StartPrimary(
    const std::vector<std::string>& arguments, JobExitCallback onExit)
{
    const auto* install = FFmpegInstalls::Primary();
    if (!install)
    {
        return nullptr;
    }
    return FFmpegProcess::Start(*install, arguments, std::move(onExit));
}

bool FfmpegBackend::RunToCompletion(const std::vector<std::string>& arguments)
{
    bool succeeded = false;
    auto process = StartPrimary(
        arguments, [&succeeded](bool completedSuccessfully) { succeeded = completedSuccessfully; });
    if (!process)
    {
        return false;
    }

    process->Wait();
    return succeeded;
}
