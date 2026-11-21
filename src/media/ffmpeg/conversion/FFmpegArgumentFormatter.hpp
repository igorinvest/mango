#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "../../../pipeline/library/FileRecord.hpp"
#include "../../../pipeline/MediaInfo.hpp"
#include "../../../pipeline/stream/profiles/StreamProfile.hpp"
#include "../../../pipeline/stream/target/StreamTargetSpecs.hpp"
#include "../../MediaBackend.hpp"
#include "../../StreamOutputLayout.hpp"
#include "../encoders/EncoderDriver.hpp"

class FFmpegArgumentFormatter
{
   public:
    // ffmpeg only supports one subtitle stream per HLS invocation (its own
    // webvtt muxer rejects a second: "does not support more than one stream
    // of type subtitle") — but when exactly one subtitle stream is mapped
    // alongside video/audio, it auto-segments a companion WebVTT playlist
    // named after the media playlist (its stem + "_vtt" + extension) as a
    // side effect, with no dedicated flag to control it. That's why
    // StreamOutputLayout's two HLS names have to stay in step.
    static constexpr const char* kHlsMediaPlaylistName = StreamOutputLayout::kHlsMediaPlaylistName;

    // Name of the file ffmpeg's "-progress" option is pointed at (see
    // Format()), written into the output folder alongside the manifest/
    // segments — FfmpegConversion::ProgressSeconds tails it.
    static constexpr const char* kProgressFileName = "progress.txt";

    // videoDriver runs the video conversion (ignored when video is copied).
    [[nodiscard]] static std::vector<std::string> Format(
        const ConversionRequest& request, const EncoderDriver& videoDriver);

   private:
    // The inputs added after the video file (input 0) — an external audio
    // file, an external subtitle file — each numbered as it's added.
    class InputList
    {
       public:
        // Adds "<options> -i <file>" and returns the input's number.
        int Add(std::vector<std::string> options, const std::filesystem::path& file);

        [[nodiscard]] std::vector<std::string> TakeArguments() { return std::move(m_arguments); }

       private:
        std::vector<std::string> m_arguments;
        int m_nextInput = 1;
    };

    // The -map specifier for the selected audio track: "0:<index>" for a
    // track of the video file, "<n>:a:0" for an external file, which is
    // added to inputs.
    [[nodiscard]] static std::string BuildAudioInput(
        const FileRecord& fileRecord, const AudioTrack& audioTrack, InputList& inputs);

    // The same for the selected subtitle track: "0:<index>" or "<n>:0".
    [[nodiscard]] static std::string BuildSubtitleInput(
        const FileRecord& fileRecord, const SubtitleTrack& subtitleTrack, InputList& inputs);

    // "-t" capping the output at the video's length when an external audio
    // file runs longer than the video, so the stream doesn't go on over a
    // frozen last frame. Empty otherwise.
    [[nodiscard]] static std::vector<std::string> BuildDurationCap(
        const FileRecord& fileRecord, const std::optional<AudioTrack>& audioTrack);

    // The driver's input options (devices, hwaccel) when it converts video.
    [[nodiscard]] static std::vector<std::string> BuildInputOptions(
        const ConversionRequest& request, const EncoderDriver& videoDriver);

    // -loglevel and -progress (into outputFolder).
    [[nodiscard]] static std::vector<std::string> BuildLoggingOptions(
        const std::filesystem::path& outputFolder);

    // Every -i (the video, then external files) followed by each selected
    // track's -map and codec options.
    [[nodiscard]] static std::vector<std::string> BuildInputsAndMapping(
        const ConversionRequest& request, const EncoderDriver& videoDriver);

    // "-c:v copy", or the driver's filter and encode arguments.
    [[nodiscard]] static std::vector<std::string> BuildVideoOptions(
        const VideoTargetSpec& spec, const EncoderDriver& videoDriver);

    [[nodiscard]] static std::vector<std::string> BuildAudioOptions(
        const AudioTargetSpec& spec, int outputIndex);

    [[nodiscard]] static std::vector<std::string> BuildSubtitleOptions(
        const SubtitleTargetSpec& spec);

    [[nodiscard]] static std::vector<std::string> BuildDashOptions(
        double fps, const StreamProfile& profile);

    [[nodiscard]] static std::vector<std::string> BuildHlsOptions(
        double fps, const StreamProfile& profile);

    static void AppendVector(std::vector<std::string>& dest, std::vector<std::string>&& src);
};
