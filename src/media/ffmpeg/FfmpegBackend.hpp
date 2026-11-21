#pragma once

#include "../MediaBackend.hpp"
#include "encoders/EncoderDriver.hpp"
#include "install/FFmpegInstall.hpp"
#include "encoders/VideoEncoderProbe.hpp"

// MediaBackend on the ffmpeg/ffprobe tools (see FFmpegInstalls). Everything under
// src/media/ffmpeg/ is internal to it.
class FfmpegBackend final : public MediaBackend
{
   public:
    [[nodiscard]] std::optional<MediaInfo> Probe(const std::filesystem::path& file) override;

    [[nodiscard]] std::optional<AudioFileInfo> ProbeAudioFile(
        const std::filesystem::path& file) override;

    bool BackfillMediaInfo(const FileRecord& file, MediaInfo& info) override;

    [[nodiscard]] std::expected<std::unique_ptr<Conversion>, ConversionError> StartConversion(
        const ConversionRequest& request, ConversionExitCallback onExit) override;

    [[nodiscard]] std::unique_ptr<MediaJob> StartSubtitleExtraction(const FileRecord& file,
        const std::vector<SubtitleOutput>& outputs, JobExitCallback onExit) override;

    [[nodiscard]] std::unique_ptr<MediaJob> StartSubtitleFileConversion(
        const std::filesystem::path& input, const std::filesystem::path& output,
        JobExitCallback onExit) override;

    bool ExtractCover(const FileRecord& file, const CoverSource& source,
        const std::filesystem::path& output) override;

    void StartProbe() override;
    void StopProbe() override;
    [[nodiscard]] std::vector<std::string> DescribeTools() override;
    [[nodiscard]] bool HasTools() override;

    [[nodiscard]] std::optional<VideoEncoderOption> VideoEncoder(
        const StreamProfile& profile) override;

    [[nodiscard]] std::vector<VideoEncoderOption> ConversionOptions(PlaylistType playlist) override;

    // The first of options whose codec profile.videoCapabilities accepts,
    // or nullptr.
    [[nodiscard]] static const ConversionOption* Choose(
        const std::vector<ConversionOption>& options, const StreamProfile& profile);

   private:
    // Where a conversion runs: the install, and the video driver (nullptr
    // when video is copied or the spec names an encoder without a driver).
    struct RunPlan
    {
        const FFmpegInstall* install{nullptr};
        const EncoderDriver* driver{nullptr};
    };

    [[nodiscard]] static std::expected<RunPlan, ConversionError> PlanRun(
        const ConversionRequest& request);

    [[nodiscard]] static bool EncodesVideo(const ConversionRequest& request);

    // Whether profile.videoCapabilities has codec.
    [[nodiscard]] static bool Accepts(const StreamProfile& profile, const std::string& codec);

    [[nodiscard]] static VideoEncoderOption Describe(const ConversionOption& option);

    // "bundled (primary): /path/ffmpeg 8.1, h264: h264_videotoolbox, dash, hls"
    [[nodiscard]] static std::string DescribeInstall(const FFmpegInstall& install, bool primary);

    // A request ffmpeg can't even start on, or std::nullopt.
    [[nodiscard]] std::optional<ConversionError> CheckRequest(const ConversionRequest& request);

    [[nodiscard]] static std::vector<std::string> SubtitleExtractionArguments(
        const FileRecord& file, const std::vector<SubtitleOutput>& outputs);

    [[nodiscard]] static std::vector<std::string> CoverArguments(
        const FileRecord& file, const CoverSource& source, const std::filesystem::path& output);

    // Runs the primary install's ffmpeg; nullptr without one.
    [[nodiscard]] static std::unique_ptr<MediaJob> StartPrimary(
        const std::vector<std::string>& arguments, JobExitCallback onExit);

    // Runs ffmpeg and waits for it; true when it exited successfully.
    static bool RunToCompletion(const std::vector<std::string>& arguments);
};
