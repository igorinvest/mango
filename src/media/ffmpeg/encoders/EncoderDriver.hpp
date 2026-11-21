#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../../../pipeline/stream/target/StreamTargetSpecs.hpp"

// One encoder family's way of running ffmpeg: which encoder, what it needs
// on the input side (devices, hwaccel), its video filter and its encode
// arguments. The probe test-encodes with the same arguments a conversion
// uses. See EncoderDrivers for each platform's list.
class EncoderDriver
{
   public:
    virtual ~EncoderDriver() = default;

    // "videotoolbox", "videotoolbox-sw", "nvenc", ...
    [[nodiscard]] virtual std::string Name() const = 0;

    // ffmpeg's encoder name: "h264_videotoolbox".
    [[nodiscard]] virtual std::string Encoder() const = 0;

    // Encodes on the CPU (a last-resort fallback).
    [[nodiscard]] virtual bool IsSoftware() const = 0;

    // An encoder option the driver relies on ("allow_sw"), which the
    // install's "-h encoder=" must list; empty when none.
    [[nodiscard]] virtual std::string RequiredOption() const = 0;

    [[nodiscard]] virtual bool IsAvailableOnThisPlatform() const = 0;

    // The whole test encode: one small generated frame to the null muxer.
    [[nodiscard]] virtual std::vector<std::string> ProbeArgs() const;

    // Before the first -i: devices, hwaccel. Empty for frames in memory.
    [[nodiscard]] virtual std::vector<std::string> InputArgs() const;

    // "-vf" value: deinterlace and scale; std::nullopt when neither applies.
    [[nodiscard]] virtual std::optional<std::string> VideoFilter(const VideoTargetSpec& spec) const;

    // "-c:v <encoder>", the driver's own options, pixel format and bitrate.
    [[nodiscard]] virtual std::vector<std::string> EncodeArgs(const VideoTargetSpec& spec) const;

   protected:
    // Options after "-c:v <encoder>" that the test encode needs too
    // ("-allow_sw 1").
    [[nodiscard]] virtual std::vector<std::string> EncoderOptions() const;

    // Filter scaling the output down into spec's maxWidth x maxHeight box;
    // std::nullopt when the spec has no box.
    [[nodiscard]] static std::optional<std::string> ScaleFilter(const VideoTargetSpec& spec);

    // ScaleFilter's size, "w='...':h='...'", for a scaler with another name
    // (scale_vaapi). std::nullopt when the spec has no box.
    [[nodiscard]] static std::optional<std::string> ScaleSize(const VideoTargetSpec& spec);

    // Joins filters into one "-vf" chain; std::nullopt when there are none.
    [[nodiscard]] static std::optional<std::string> FilterChain(const std::vector<std::string>& filters);

    // "bwdif=..." when spec asks for deinterlacing.
    [[nodiscard]] static std::optional<std::string> DeinterlaceFilter(const VideoTargetSpec& spec);
};

// A driver for encoders that take ordinary frames from memory, with a fixed
// option list: every encoder Mango used before drivers existed, and the
// software fallbacks.
class StandardEncoderDriver final : public EncoderDriver
{
   public:
    enum class Platform
    {
        macOS,
        windows,
        linuxOS,  // not "linux": GNU modes predefine it as a macro
        any
    };

    struct Definition
    {
        std::string name;
        std::string encoder;
        // Where the driver is tried; one of them must match.
        std::vector<Platform> platforms{Platform::any};
        bool software{false};
        std::vector<std::string> options;
        std::string requiredOption;
    };

    explicit StandardEncoderDriver(Definition definition);

    [[nodiscard]] std::string Name() const override;
    [[nodiscard]] std::string Encoder() const override;
    [[nodiscard]] bool IsSoftware() const override;
    [[nodiscard]] std::string RequiredOption() const override;
    [[nodiscard]] bool IsAvailableOnThisPlatform() const override;

   protected:
    [[nodiscard]] std::vector<std::string> EncoderOptions() const override;

   private:
    Definition m_definition;
};

// VAAPI (Intel and AMD on Linux): the encoder takes frames in GPU memory, so
// they are uploaded after any software filter, scaled there with
// scale_vaapi, and no "-pix_fmt" is set (the frames are already NV12 on the
// GPU). The device is the first render node.
class VaapiEncoderDriver final : public EncoderDriver
{
   public:
    [[nodiscard]] std::string Name() const override;
    [[nodiscard]] std::string Encoder() const override;
    [[nodiscard]] bool IsSoftware() const override;
    [[nodiscard]] std::string RequiredOption() const override;
    [[nodiscard]] bool IsAvailableOnThisPlatform() const override;

    [[nodiscard]] std::vector<std::string> ProbeArgs() const override;
    [[nodiscard]] std::vector<std::string> InputArgs() const override;
    [[nodiscard]] std::optional<std::string> VideoFilter(const VideoTargetSpec& spec) const override;
    [[nodiscard]] std::vector<std::string> EncodeArgs(const VideoTargetSpec& spec) const override;

   private:
    // Converts to 8-bit NV12 (10-bit sources too) and moves it to the GPU.
    static constexpr const char* kUploadFilter = "format=nv12,hwupload";
};
