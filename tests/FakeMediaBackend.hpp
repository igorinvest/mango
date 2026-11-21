#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "media/MediaBackend.hpp"

// A MediaBackend that runs no tool: probes return what a test put in
// mediaInfos, conversions and jobs end only when the test says so. Install
// it with MediaBackends::SetForTesting before anything uses the backend.
class FakeMediaBackend final : public MediaBackend
{
   public:
    class FakeConversion final : public Conversion
    {
       public:
        explicit FakeConversion(ConversionExitCallback onExit) : m_onExit(std::move(onExit)) {}

        void Stop() override { Finish(std::unexpected(ConversionError::Cancelled)); }
        void Wait() override {}
        [[nodiscard]] std::optional<double> ProgressSeconds() const override { return progress; }

        // Ends the conversion with result; later calls do nothing.
        void Finish(ConversionResult result)
        {
            if (m_onExit)
            {
                auto onExit = std::move(m_onExit);
                m_onExit = nullptr;
                onExit(result);
            }
        }

        std::optional<double> progress;

       private:
        ConversionExitCallback m_onExit;
    };

    class FakeJob final : public MediaJob
    {
       public:
        explicit FakeJob(JobExitCallback onExit) : m_onExit(std::move(onExit)) {}

        void Stop() override { Finish(false); }
        void Wait() override {}

        void Finish(bool completedSuccessfully)
        {
            if (m_onExit)
            {
                auto onExit = std::move(m_onExit);
                m_onExit = nullptr;
                onExit(completedSuccessfully);
            }
        }

       private:
        JobExitCallback m_onExit;
    };

    [[nodiscard]] std::optional<MediaInfo> Probe(const std::filesystem::path& file) override
    {
        const auto it = mediaInfos.find(file);
        return it != mediaInfos.end() ? std::optional(it->second) : std::nullopt;
    }

    [[nodiscard]] std::optional<AudioFileInfo> ProbeAudioFile(
        const std::filesystem::path& file) override
    {
        const auto it = audioFileInfos.find(file);
        return it != audioFileInfos.end() ? std::optional(it->second) : std::nullopt;
    }

    bool BackfillMediaInfo(const FileRecord&, MediaInfo&) override { return false; }

    [[nodiscard]] std::expected<std::unique_ptr<Conversion>, ConversionError> StartConversion(
        const ConversionRequest& request, ConversionExitCallback onExit) override
    {
        requests.push_back({.filePath = request.file.filePath,
            .targets = request.targets,
            .outputFolder = request.outputFolder});
        if (startError)
        {
            return std::unexpected(*startError);
        }

        auto conversion = std::make_unique<FakeConversion>(std::move(onExit));
        lastConversion = conversion.get();
        return conversion;
    }

    [[nodiscard]] std::unique_ptr<MediaJob> StartSubtitleExtraction(
        const FileRecord&, const std::vector<SubtitleOutput>& outputs, JobExitCallback onExit) override
    {
        subtitleOutputs.insert(subtitleOutputs.end(), outputs.begin(), outputs.end());
        return std::make_unique<FakeJob>(std::move(onExit));
    }

    [[nodiscard]] std::unique_ptr<MediaJob> StartSubtitleFileConversion(
        const std::filesystem::path&, const std::filesystem::path&, JobExitCallback onExit) override
    {
        return std::make_unique<FakeJob>(std::move(onExit));
    }

    bool ExtractCover(const FileRecord&, const CoverSource& source,
        const std::filesystem::path&) override
    {
        coverSources.push_back(source);
        return coverSucceeds;
    }

    [[nodiscard]] std::optional<VideoEncoderOption> VideoEncoder(const StreamProfile&) override
    {
        return videoEncoder ? std::optional(Option(*videoEncoder)) : std::nullopt;
    }

    [[nodiscard]] std::vector<VideoEncoderOption> ConversionOptions(PlaylistType) override
    {
        return videoEncoder ? std::vector{Option(*videoEncoder)} : std::vector<VideoEncoderOption>{};
    }

    static VideoEncoderOption Option(const std::string& encoder)
    {
        return {.codec = "h264", .driver = "", .encoder = encoder, .software = false, .external = false};
    }

    // What a test sets up.
    std::map<std::filesystem::path, MediaInfo> mediaInfos;
    std::map<std::filesystem::path, AudioFileInfo> audioFileInfos;
    std::optional<ConversionError> startError;
    std::optional<std::string> videoEncoder = "fake_h264";
    bool coverSucceeds = true;

    // What the engine asked for.
    struct RecordedRequest
    {
        std::filesystem::path filePath;
        StreamTargetSpecs targets;
        std::filesystem::path outputFolder;
    };
    std::vector<RecordedRequest> requests;
    std::vector<SubtitleOutput> subtitleOutputs;
    std::vector<CoverSource> coverSources;
    FakeConversion* lastConversion = nullptr;
};
