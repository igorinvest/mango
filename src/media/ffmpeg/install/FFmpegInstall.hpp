#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// A matching ffmpeg + ffprobe pair in one folder, and what that build has.
// The two tools are never mixed across installs.
struct FFmpegInstall
{
    enum class Origin
    {
        bundled,     // next to the executable
        configured,  // settings.json "ffmpegFolder" (Linux only)
        systemPath   // found in PATH (Linux only)
    };

    Origin origin{Origin::bundled};
    std::filesystem::path ffmpeg;
    std::filesystem::path ffprobe;

    // As ffmpeg prints it: "8.1", "6.1.1-3ubuntu5", "N-112345-g1a2b3c".
    std::string version{};

    // From "ffmpeg -codecs": codec -> its encoders, in ffmpeg's order.
    // {"h264": {"libx264", "h264_nvenc"}, "aac": {"aac"}, ...}
    std::map<std::string, std::vector<std::string>> encodersByCodec{};

    // From "ffmpeg -muxers": "dash", "hls", ...
    std::set<std::string> muxers{};

    [[nodiscard]] bool HasMuxer(std::string_view muxer) const;

    // Whether the build lists encoder as one producing codec.
    [[nodiscard]] bool ListsEncoder(std::string_view codec, std::string_view encoder) const;

    // The codec encoder produces, or empty when the build doesn't list it.
    [[nodiscard]] std::string CodecOf(std::string_view encoder) const;

    // "bundled", "configured", "PATH".
    [[nodiscard]] static std::string_view OriginName(Origin origin);
};
