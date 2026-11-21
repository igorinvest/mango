#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../pipeline/library/FileRecord.hpp"
#include "../pipeline/MediaInfo.hpp"
#include "../pipeline/stream/profiles/StreamProfile.hpp"
#include "../pipeline/stream/target/StreamTargetSpecs.hpp"
#include "MediaJob.hpp"
#include "VideoEncoderOption.hpp"

// Everything a conversion needs, already decided: StreamTargetResolver
// picks copy or convert, sizes and bitrates; the backend only carries that
// out. Outputs go into outputFolder under the names in StreamOutputLayout
// and the profile's entry name (defaultFileName + defaultExtension).
struct ConversionRequest
{
    const FileRecord& file;

    // Only the tracks to mux - a sidecar-subtitle profile's subtitle track
    // is already removed.
    TrackList tracks;

    StreamTargetSpecs targets;

    // Playlist type and segment settings.
    const StreamProfile& profile;

    std::filesystem::path outputFolder;
};

// One built-in subtitle track to write as WebVTT.
struct SubtitleOutput
{
    int trackIndex = 0;
    std::filesystem::path path;
};

// An external audio file's first audio stream, as a backend probed it.
struct AudioFileInfo
{
    std::string codec;
    int channels = 0;
    int sampleRate = 0;
    std::int64_t bitrate = 0;
    double durationSeconds = 0.0;
    double startTimeSeconds = 0.0;
};

// Where a cover image comes from (picked by CoverExtractor from MediaInfo).
struct CoverSource
{
    enum class Kind
    {
        Attachment,       // MediaInfo::attachments[...]
        AttachedPicture,  // MediaInfo::attachedPictures[...]
        Frame             // a frame of the main video at frameSeconds
    };

    Kind kind = Kind::Frame;
    int attachmentIndex = -1;  // Attachment: AttachmentTrack::attachmentIndex
    int streamIndex = -1;      // AttachedPicture: the stream to copy out
    std::int64_t frameSeconds = 0;
};

// The one place the engine runs a media tool. The rest of the engine asks
// for "probe this file" or "start this conversion" and doesn't know which
// tool does the work. One backend per process (see MediaBackends::Get).
class MediaBackend
{
   public:
    virtual ~MediaBackend() = default;

    // std::nullopt when the file can't be read as media.
    [[nodiscard]] virtual std::optional<MediaInfo> Probe(const std::filesystem::path& file) = 0;

    // A light probe of a standalone audio file (see ExternalAudioFinder).
    // std::nullopt when it can't be read or has no audio stream.
    [[nodiscard]] virtual std::optional<AudioFileInfo> ProbeAudioFile(
        const std::filesystem::path& file) = 0;

    // Fills in fields of a MediaInfo probed by an older version (today:
    // VideoTrack::rotation, VideoTrack::interlaced) from the backend's own cache, without probing
    // again. Returns whether anything changed.
    virtual bool BackfillMediaInfo(const FileRecord& file, MediaInfo& info) = 0;

    // onExit fires once when the conversion ends. An error returned here
    // means nothing was started and onExit never fires.
    [[nodiscard]] virtual std::expected<std::unique_ptr<Conversion>, ConversionError>
    StartConversion(const ConversionRequest& request, ConversionExitCallback onExit) = 0;

    // Writes every listed built-in text track as WebVTT in one pass over the
    // file. nullptr when it can't start (onExit never fires then).
    [[nodiscard]] virtual std::unique_ptr<MediaJob> StartSubtitleExtraction(
        const FileRecord& file, const std::vector<SubtitleOutput>& outputs,
        JobExitCallback onExit) = 0;

    // Converts a standalone subtitle file (already UTF-8) into WebVTT.
    [[nodiscard]] virtual std::unique_ptr<MediaJob> StartSubtitleFileConversion(
        const std::filesystem::path& input, const std::filesystem::path& output,
        JobExitCallback onExit) = 0;

    // Writes the image to output, synchronously. False when it couldn't.
    virtual bool ExtractCover(
        const FileRecord& file, const CoverSource& source, const std::filesystem::path& output) = 0;

    // Starts the backend's own startup work (finding its tools, testing
    // encoders) in the background, so the first stream doesn't wait for it.
    // Calls that need the result wait for it. Later calls do nothing.
    virtual void StartProbe() {}

    // Stops that work at engine shutdown and waits for it to end.
    virtual void StopProbe() {}

    // One line per tool install the backend found, for the console's
    // startup report. Waits for the probe's install stage.
    [[nodiscard]] virtual std::vector<std::string> DescribeTools() { return {}; }

    // Whether the backend found the tools it runs (ffmpeg). False only on
    // Linux with neither a bundled nor a system ffmpeg. Waits for the
    // probe's install stage.
    [[nodiscard]] virtual bool HasTools() { return true; }

    // The encoder a conversion for profile uses: the protocol's first
    // working option whose codec profile.videoCapabilities accepts.
    // std::nullopt when there's none. Waits for the probe's test encodes.
    [[nodiscard]] virtual std::optional<VideoEncoderOption> VideoEncoder(
        const StreamProfile& profile) = 0;

    // The working options for playlist, best first. Waits for the probe.
    [[nodiscard]] virtual std::vector<VideoEncoderOption> ConversionOptions(
        PlaylistType playlist) = 0;
};

class MediaBackends
{
   public:
    // The process's backend, created on first use. Only the ffmpeg one
    // exists today.
    [[nodiscard]] static MediaBackend& Get();

    // Replaces the backend, for tests (see tests/FakeMediaBackend.hpp).
    // Call before anything uses Get().
    static void SetForTesting(std::unique_ptr<MediaBackend> backend);
};
