#pragma once

#include <expected>
#include <functional>
#include <optional>
#include <string>

// Why a conversion failed - the part of a backend's failure the rest of the
// engine (and the apps) can act on, instead of a tool-specific exit code.
enum class ConversionError
{
    NoVideoEncoder,
    // No ffmpeg install at all (Linux without a bundled or system ffmpeg).
    FfmpegMissing,
    UnsupportedCodec,
    FileUnreadable,
    OutputFailed,
    // Stopped on purpose (MediaJob::Stop) rather than failed.
    Cancelled,
    Unknown
};

class ConversionErrors
{
   public:
    // "noVideoEncoder", "ffmpegMissing", "unsupportedCodec", "fileUnreadable",
    // "outputFailed", "cancelled" or "unknown" - what the Engine API hands
    // to the apps.
    [[nodiscard]] static std::string ToString(ConversionError error);
};

// How a conversion ended: a value when it finished writing every output.
using ConversionResult = std::expected<void, ConversionError>;

// Fires exactly once, from a backend thread, the moment the job ends
// (naturally or through Stop()).
using JobExitCallback = std::function<void(bool completedSuccessfully)>;
using ConversionExitCallback = std::function<void(ConversionResult)>;

// One running piece of backend work: a conversion, a subtitle extraction.
class MediaJob
{
   public:
    virtual ~MediaJob() = default;

    // Ends the job and blocks until its exit callback has run.
    virtual void Stop() = 0;

    // Blocks until the job ends on its own.
    virtual void Wait() = 0;
};

// The main video/audio conversion of a stream.
class Conversion : public MediaJob
{
   public:
    // How far into the source the conversion has fully written its
    // outputs, in seconds. std::nullopt before the first report.
    [[nodiscard]] virtual std::optional<double> ProgressSeconds() const = 0;
};
