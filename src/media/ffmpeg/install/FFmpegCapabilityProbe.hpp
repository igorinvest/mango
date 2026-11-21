#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "FFmpegInstall.hpp"
#include "FFmpegLocator.hpp"

// A version number read from "ffmpeg -version". Git builds ("N-112345-g...")
// have no number and count as new enough.
struct FFmpegVersion
{
    int major{0};
    int minor{0};
    bool gitBuild{false};
};

// Reads what a candidate install has, with short ffmpeg runs: -version,
// -codecs and -muxers. Lists alone don't prove an encoder works (that's the
// test encode's job), but they say which ones are worth testing.
class FFmpegCapabilityProbe
{
   public:
    // External installs older than this are rejected.
    static constexpr FFmpegVersion kMinimumVersion{.major = 5, .minor = 1, .gitBuild = false};

    // The install with its lists, or std::nullopt when an external one is
    // rejected (logged with the reason). The bundled one is never rejected.
    [[nodiscard]] static std::optional<FFmpegInstall> Read(const FFmpegCandidate& candidate);

    // Whether "ffmpeg -h encoder=<encoder>" lists option ("allow_sw",
    // "hw_encoding") for that encoder.
    [[nodiscard]] static bool EncoderHasOption(
        const FFmpegInstall& install, const std::string& encoder, std::string_view option);

    // ---- Parsers (public for tests) ----

    // "9.0.2" from "ffmpeg version 9.0.2 Copyright ..."; empty if absent.
    [[nodiscard]] static std::string ParseVersionOutput(std::string_view output);

    // "6.1.1-3ubuntu5" -> 6.1, "n7.0.2" -> 7.0, "N-112345-g..." -> git build.
    [[nodiscard]] static std::optional<FFmpegVersion> ParseVersion(std::string_view version);

    [[nodiscard]] static bool IsNewEnough(const FFmpegVersion& version);

    // "ffmpeg -codecs": codec -> encoders, for rows that can encode.
    [[nodiscard]] static std::map<std::string, std::vector<std::string>> ParseCodecs(
        std::string_view output);

    // "ffmpeg -muxers": the names of rows that can mux.
    [[nodiscard]] static std::set<std::string> ParseMuxers(std::string_view output);

    // Whether "ffmpeg -h encoder=..." output has "-<option> ".
    [[nodiscard]] static bool HelpHasOption(std::string_view helpOutput, std::string_view option);

   private:
    // Fills version, encodersByCodec and muxers from the install's ffmpeg.
    static void ReadLists(FFmpegInstall& install);

    // The reason an external install can't be used, or empty.
    [[nodiscard]] static std::string RejectionReason(const FFmpegInstall& install);

    [[nodiscard]] static bool ProbeRuns(const FFmpegInstall& install);

    // The rows of an ffmpeg list (-codecs, -muxers), after its "---" line.
    [[nodiscard]] static std::vector<std::string_view> TableRows(std::string_view output);

    // The space-separated words of text.
    [[nodiscard]] static std::vector<std::string_view> Words(std::string_view text);

    // The words inside "(<label>: ...)" in a row, or empty.
    [[nodiscard]] static std::vector<std::string_view> Parenthesized(
        std::string_view row, std::string_view label);
};
