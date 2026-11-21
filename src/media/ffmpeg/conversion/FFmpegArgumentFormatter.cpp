#include "FFmpegArgumentFormatter.hpp"

#include <algorithm>
#include <cmath>

#include "../../../file/File.hpp"
#include "../../../pipeline/stream/subtitles/SubtitleCharset.hpp"

void FFmpegArgumentFormatter::AppendVector(
    std::vector<std::string>& dest, std::vector<std::string>&& src)
{
    if (src.empty()) return;
    dest.insert(
        dest.end(), std::make_move_iterator(src.begin()), std::make_move_iterator(src.end()));
}

std::vector<std::string> FFmpegArgumentFormatter::Format(
    const ConversionRequest& request, const EncoderDriver& videoDriver)
{
    auto command = BuildLoggingOptions(request.outputFolder);
    AppendVector(command, BuildInputsAndMapping(request, videoDriver));

    const bool mapsAudio = request.tracks.audioTrack && !request.targets.audio.codec.empty();
    AppendVector(command,
        BuildDurationCap(request.file, mapsAudio ? request.tracks.audioTrack : std::nullopt));

    // 3. Выходной формат и настройки (DASH/HLS)
    const double fps = request.tracks.videoTrack ? request.tracks.videoTrack->fps : 0.0;

    if (request.profile.playlist == PlaylistType::HLS)
    {
        AppendVector(command, BuildHlsOptions(fps, request.profile));
    }
    else
    {
        AppendVector(command, BuildDashOptions(fps, request.profile));
    }

    return command;
}

std::vector<std::string> FFmpegArgumentFormatter::BuildLoggingOptions(
    const std::filesystem::path& outputFolder)
{
    // Timestamped and verbose, so every file ffmpeg writes shows up in
    // ffmpeg.log and lines up with the server's reads.log.
    //
    // -nostats would otherwise duplicate the same frame/time/speed info
    // -progress now writes in a structured, absolute path (not resolved
    // against ffmpeg's cwd like the manifest/segment names below, so it
    // works the same regardless of workingDirectory).
    return {"-loglevel", "datetime+verbose", "-nostats", "-progress",
        File::ToUtf8(outputFolder / kProgressFileName)};
}

std::vector<std::string> FFmpegArgumentFormatter::BuildInputsAndMapping(
    const ConversionRequest& request, const EncoderDriver& videoDriver)
{
    const auto& fileRecord = request.file;
    const auto& trackList = request.tracks;
    const auto& targetSpecs = request.targets;

    // 1. Глобальные опции ввода
    auto command = BuildInputOptions(request, videoDriver);
    command.push_back("-i");
    command.push_back(File::ToUtf8(fileRecord.filePath));

    // Every input must come before the first -map, so external audio and
    // subtitle files are added here and mapped further down.
    const bool mapsAudio = trackList.audioTrack.has_value() && !targetSpecs.audio.codec.empty();
    const bool mapsSubtitle =
        trackList.subtitleTrack.has_value() && !targetSpecs.subtitle.codec.empty();

    InputList inputs;
    const auto audioMap =
        mapsAudio ? BuildAudioInput(fileRecord, *trackList.audioTrack, inputs) : std::string{};
    const auto subtitleMap = mapsSubtitle
        ? BuildSubtitleInput(fileRecord, *trackList.subtitleTrack, inputs)
        : std::string{};
    AppendVector(command, inputs.TakeArguments());

    // 2. Опции обработки дорожек (Map + Codec для каждого типа)
    if (trackList.videoTrack.has_value())
    {
        command.insert(command.end(), {"-map", "0:" + std::to_string(trackList.videoTrack->index)});
        AppendVector(command, BuildVideoOptions(targetSpecs.video, videoDriver));
    }

    if (mapsAudio)
    {
        constexpr int outputAudioIndex = 0;
        command.insert(command.end(), {"-map", audioMap});
        AppendVector(command, BuildAudioOptions(targetSpecs.audio, outputAudioIndex));
    }

    // ffmpeg's webvtt muxer rejects a second subtitle stream outright
    // ("does not support more than one stream of type subtitle"), so this
    // only ever maps the single initially-selected track — never every
    // track the file has. For HLS that single track (if any) is what
    // StreamManager::WriteHlsMasterPlaylist references as the one
    // EXT-X-MEDIA subtitle rendition.
    if (mapsSubtitle)
    {
        command.insert(command.end(), {"-map", subtitleMap});
        AppendVector(command, BuildSubtitleOptions(targetSpecs.subtitle));
    }

    return command;
}

int FFmpegArgumentFormatter::InputList::Add(
    std::vector<std::string> options, const std::filesystem::path& file)
{
    AppendVector(m_arguments, std::move(options));
    m_arguments.push_back("-i");
    m_arguments.push_back(File::ToUtf8(file));
    return m_nextInput++;
}

std::string FFmpegArgumentFormatter::BuildAudioInput(
    const FileRecord& fileRecord, const AudioTrack& audioTrack, InputList& inputs)
{
    const auto* external = fileRecord.FindExternalAudio(audioTrack.index);
    if (!external)
    {
        return "0:" + std::to_string(audioTrack.index);
    }

    // No -itsoffset: without -copyts ffmpeg already starts every input at
    // 0, so a dub that starts at 0 lines up with a video whose timestamps
    // start later (TS, some MKV) — that's how a player showed it to
    // whoever synced the dub.
    const int input = inputs.Add({"-fflags", "+genpts"}, File::FromUtf8(external->path));
    return std::to_string(input) + ":a:0";
}

std::string FFmpegArgumentFormatter::BuildSubtitleInput(
    const FileRecord& fileRecord, const SubtitleTrack& subtitleTrack, InputList& inputs)
{
    const auto* external = fileRecord.FindExternalSubtitle(subtitleTrack.index);
    if (!external)
    {
        return "0:" + std::to_string(subtitleTrack.index);
    }

    // A non-UTF-8 file is read through its UTF-8 copy. If that can't be
    // written, the original is still better than failing the conversion.
    const auto inputPath =
        SubtitleCharset::PrepareFfmpegInput(*external, fileRecord.getSubtitleCacheFolder())
            .value_or(File::FromUtf8(external->path));

    return std::to_string(inputs.Add({}, inputPath)) + ":0";
}

std::vector<std::string> FFmpegArgumentFormatter::BuildDurationCap(
    const FileRecord& fileRecord, const std::optional<AudioTrack>& audioTrack)
{
    const auto* external = audioTrack ? fileRecord.FindExternalAudio(audioTrack->index) : nullptr;
    const auto videoSeconds = fileRecord.mediaInfo.durationSec;
    if (!external || videoSeconds <= 0)
    {
        return {};
    }

    // durationSec is whole seconds, rounded down: the cap allows one more,
    // so the video's own last frames are never cut. A dub within that
    // second of the video needs no cap at all.
    const auto capSeconds = videoSeconds + 1;
    if (external->startTimeSeconds + external->durationSeconds <= static_cast<double>(capSeconds))
    {
        return {};
    }
    return {"-t", std::to_string(capSeconds)};
}

std::vector<std::string> FFmpegArgumentFormatter::BuildInputOptions(
    const ConversionRequest& request, const EncoderDriver& videoDriver)
{
    std::vector<std::string> options;
    if (request.tracks.videoTrack && !request.targets.video.copy)
    {
        options = videoDriver.InputArgs();
    }
    AppendVector(options, {"-fflags", "+genpts", "-analyzeduration", "100M", "-probesize", "100M"});
    return options;
}

std::vector<std::string> FFmpegArgumentFormatter::BuildVideoOptions(
    const VideoTargetSpec& spec, const EncoderDriver& videoDriver)
{
    if (spec.copy)
    {
        return {"-c:v", "copy"};
    }
    return videoDriver.EncodeArgs(spec);
}

std::vector<std::string> FFmpegArgumentFormatter::BuildAudioOptions(
    const AudioTargetSpec& spec, int outputIndex)
{
    const std::string argCodec = "-c:a:" + std::to_string(outputIndex);
    if (spec.copy)
    {
        return {argCodec, "copy"};
    }
    if (spec.codec.empty())
    {
        return {};  // Пропускаем, если кодек не задан
    }

    std::vector<std::string> opts = {argCodec, spec.codec};
    if (!spec.bitrate.empty())
    {
        opts.push_back("-b:a:" + std::to_string(outputIndex));
        opts.push_back(spec.bitrate);
    }

    // Sources muxed with "side"/"back" surround variants (common for
    // EAC3/AC3/DTS tracks off Blu-ray/streaming rips, e.g. 5.1(side)) fail
    // ffmpeg's native AAC encoder with "Unsupported encoding type" — it only
    // recognizes a fixed table of canonical layouts. Forcing the output
    // through one of those remixes side/back variants automatically.
    opts.push_back("-filter:a");
    opts.push_back(
        "aformat=channel_layouts=mono|stereo|2.1|3.0|3.1|4.0|4.1|5.0|5.1|6.0|6.1|7.0|7.1");

    return opts;
}

std::vector<std::string> FFmpegArgumentFormatter::BuildSubtitleOptions(
    const SubtitleTargetSpec& spec)
{
    if (spec.codec.empty())
    {
        return {};
    }

    std::vector<std::string> opts = {"-c:s", spec.codec};
    if (!spec.bitrate.empty())
    {
        opts.push_back("-b:s:0");
        opts.push_back(spec.bitrate);
    }
    return opts;
}

std::vector<std::string> FFmpegArgumentFormatter::BuildDashOptions(
    double fps, const StreamProfile& profile)
{
    // Keyframes must land on segment boundaries, or the DASH muxer produces
    // uneven segment lengths instead of clean segmentDurationSec chunks.
    // 48 (the old hardcoded -g value) only happens to divide evenly at 24fps.
    const int segmentDuration = profile.segmentDurationSec > 0 ? profile.segmentDurationSec : 2;

    const int gop = fps > 0.0 ? std::lround(fps * segmentDuration) : 48;
    const std::string gopStr = std::to_string(gop);

    // Relative names, resolved against ffmpeg's cwd — the caller is
    // responsible for launching ffmpeg with that cwd set to the stream
    // folder (see FFmpegProcess::Start's workingDirectory).
    const std::string manifestName = profile.defaultFileName + profile.defaultExtension;

    std::vector<std::string> opts = {"-g", gopStr, "-keyint_min", gopStr, "-sc_threshold", "0",
        "-f", "dash", "-seg_duration", std::to_string(segmentDuration), "-dash_segment_type", "mp4",
        "-init_seg_name", "init_$RepresentationID$.mp4", "-media_seg_name",
        "seg_$RepresentationID$_$Number%03d$.m4s"};

#ifdef _WIN32
    // The dash muxer replaces every file by renaming "<name>.tmp" over it,
    // and on Windows that rename fails - aborting the whole conversion -
    // whenever anything (e.g. an antivirus scan) has the old file open.
    // Through the tee: protocol it writes every file in place instead and
    // never renames; the server covers a manifest caught mid-write (see
    // ServerWrapper). -ignore_io_errors turns a blocked manifest open into
    // one skipped update rather than a failed conversion.
    opts.insert(opts.end(), {"-ignore_io_errors", "1", "tee:" + manifestName});
#else
    opts.push_back(manifestName);
#endif

    return opts;
}

std::vector<std::string> FFmpegArgumentFormatter::BuildHlsOptions(
    double fps, const StreamProfile& profile)
{
    // Same keyframe-on-segment-boundary reasoning as BuildDashOptions.
    const int segmentDuration = profile.segmentDurationSec > 0 ? profile.segmentDurationSec : 2;

    const int gop = fps > 0.0 ? std::lround(fps * segmentDuration) : 48;
    const std::string gopStr = std::to_string(gop);

    // Relative name, resolved against ffmpeg's cwd (the stream folder) —
    // internal only. FileRecord::getStreamEntry returns the master
    // playlist StreamManager::WriteHlsMasterPlaylist writes separately, not
    // this file directly (see kHlsMediaPlaylistName).
    const std::string manifestName = kHlsMediaPlaylistName;

    // fmp4 segments (not .ts), matching the mp4-based output the DASH path
    // already produces. "event" keeps appending to the playlist as ffmpeg
    // encodes rather than finalizing it upfront — playback can start (and
    // AVPlayer can seek within what's already written) before the whole
    // file has been transcoded, mirroring how the DASH manifest above is
    // also written incrementally rather than as a finished VOD asset.
    return {"-g", gopStr, "-keyint_min", gopStr, "-sc_threshold", "0", "-f", "hls",
        "-hls_time", std::to_string(segmentDuration), "-hls_playlist_type", "event",
        "-hls_segment_type", "fmp4", "-hls_flags", "independent_segments", manifestName};
}
