#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../../../pipeline/stream/profiles/StreamProfile.hpp"
#include "EncoderDriver.hpp"
#include "../install/FFmpegInstall.hpp"

// One way to convert video for a protocol: a codec, the driver that encodes
// it and the install that runs it.
struct ConversionOption
{
    std::string codec;  // "h264"
    const EncoderDriver* driver{nullptr};
    const FFmpegInstall* install{nullptr};
};

// Works out the conversion options for each protocol by test-encoding. A
// build listing an encoder doesn't prove it works (no GPU, a VM, a missing
// driver), so each candidate is verified with a real one-frame encode.
//
// For each codec the protocol allows, the drivers are tried in priority
// order (hardware, then software), each in every install, in install order,
// that lists the driver's encoder for that codec and has the protocol's
// muxer. The first that works is the option for that codec. A pair tested
// once isn't tested again for the other protocol.
class VideoEncoderProbe
{
   public:
    // Whether driver works in install; the real one is TestEncode.
    using Tester = std::function<bool(const EncoderDriver&, const FFmpegInstall&)>;

    VideoEncoderProbe(const std::vector<FFmpegInstall>& installs,
        std::vector<const EncoderDriver*> drivers, Tester tester,
        const std::atomic<bool>& stopRequested);

    // The working options for playlist, best first, at most one per codec.
    [[nodiscard]] std::vector<ConversionOption> OptionsFor(PlaylistType playlist);

    // The driver's "-h encoder=" option check, then its test encode.
    [[nodiscard]] static bool TestEncode(const EncoderDriver& driver, const FFmpegInstall& install);

    // The muxer an install needs for playlist: "dash" or "hls".
    [[nodiscard]] static const char* MuxerFor(PlaylistType playlist);

    // The options FFmpegStartupProbe found for playlist; waits for them.
    [[nodiscard]] static const std::vector<ConversionOption>& Options(PlaylistType playlist);

   private:
    [[nodiscard]] std::optional<ConversionOption> FirstWorking(
        const std::string& codec, const char* muxer);

    // Tests once per pair.
    [[nodiscard]] bool Works(const EncoderDriver& driver, const FFmpegInstall& install);

    [[nodiscard]] static bool RunsEncode(const EncoderDriver& driver, const FFmpegInstall& install);

    const std::vector<FFmpegInstall>& m_installs;
    std::vector<const EncoderDriver*> m_drivers;
    Tester m_tester;
    const std::atomic<bool>& m_stopRequested;
    std::map<std::pair<const EncoderDriver*, const FFmpegInstall*>, bool> m_tested;
};
