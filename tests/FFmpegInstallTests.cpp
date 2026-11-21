// ffmpeg installs: parsing what a build prints, finding installs, and the
// background probe on the bundled ffmpeg next to this executable.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "TestCheck.hpp"
#include "media/ffmpeg/install/FFmpegCapabilityProbe.hpp"
#include "media/ffmpeg/install/FFmpegInstalls.hpp"
#include "media/ffmpeg/install/FFmpegLocator.hpp"
#include "platform/ExecutablePath.hpp"

namespace fs = std::filesystem;

namespace
{

// ---- Saved output of real builds ------------------------------------------

// Bundled macOS build (ffmpeg 9).
constexpr std::string_view kMacCodecs = R"(Codecs:
 D..... = Decoding supported
 .E.... = Encoding supported
 ..V... = Video codec
 -------
 D.VI.S 012v                 Uncompressed 4:2:2 10-bit
 DEV.LS h264                 H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 (encoders: h264_videotoolbox)
 DEV.LS hevc                 H.265 / HEVC (High Efficiency Video Coding) (encoders: hevc_videotoolbox)
 DEV.L. mpeg4                MPEG-4 part 2
 DEA.L. aac                  AAC (Advanced Audio Coding) (decoders: aac aac_fixed aac_at) (encoders: aac aac_at)
 D.A.L. eac3                 ATSC A/52B (AC-3, E-AC-3)
)";

// Ubuntu 24.04's ffmpeg 6.1 (decoder and encoder lists end with a space).
constexpr std::string_view kUbuntuCodecs =
    "Codecs:\n"
    " D..... = Decoding supported\n"
    " -------\n"
    " DEV.LS h264                 H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 (decoders: h264 "
    "h264_v4l2m2m h264_qsv h264_cuvid ) (encoders: libx264 libx264rgb h264_nvenc h264_qsv "
    "h264_v4l2m2m h264_vaapi )\r\n"
    " DEV.L. vp9                  Google VP9 (decoders: vp9 libvpx-vp9) (encoders: libvpx-vp9 vp9_vaapi)\n";

constexpr std::string_view kNewMuxers = R"(Formats:
 D.. = Demuxing supported
 .E. = Muxing supported
 ..d = Is a device
 ---
  E  3g2             3GP2 (3GPP2 file format)
  E  dash            DASH Muxer
  E  hls             Apple HTTP Live Streaming
  E  webvtt          WebVTT subtitle
)";

constexpr std::string_view kOldMuxers = R"(File formats:
 D. = Demuxing supported
 .E = Muxing supported
 --
  E dash            DASH Muxer
  E mp4             MP4 (MPEG-4 Part 14)
)";

constexpr std::string_view kVideoToolboxHelp = R"(Encoder h264_videotoolbox [VideoToolbox H.264 Encoder]:
    General capabilities: delay hardware
h264_videotoolbox AVOptions:
  -profile           <int>        E..V....... Profile (default 0)
  -allow_sw          <boolean>    E..V....... Allow software encoding (default false)
  -require_sw        <boolean>    E..V....... Require software encoding (default false)
)";

// ---- Version ---------------------------------------------------------------

bool VersionIs(std::string_view text, int major, int minor)
{
    const auto version = FFmpegCapabilityProbe::ParseVersion(text);
    return version && !version->gitBuild && version->major == major && version->minor == minor;
}

void VersionParsing()
{
    Check(FFmpegCapabilityProbe::ParseVersionOutput(
              "ffmpeg version 9.0.2 Copyright (c) 2000-2026 the FFmpeg developers\nbuilt with") ==
            "9.0.2",
        "the version word after \"version\"");
    Check(FFmpegCapabilityProbe::ParseVersionOutput("nothing here").empty(), "no version line");

    Check(VersionIs("8.1", 8, 1), "8.1");
    Check(VersionIs("6.1.1-3ubuntu5", 6, 1), "a distro version");
    Check(VersionIs("n7.0.2", 7, 0), "a tag build");
    Check(VersionIs("7", 7, 0), "a major-only version");

    const auto git = FFmpegCapabilityProbe::ParseVersion("N-112345-g1a2b3c");
    Check(git && git->gitBuild && FFmpegCapabilityProbe::IsNewEnough(*git), "a git build is new enough");
    Check(!FFmpegCapabilityProbe::ParseVersion("garbage"), "garbage is no version");

    Check(FFmpegCapabilityProbe::IsNewEnough({.major = 5, .minor = 1, .gitBuild = false}), "5.1 is enough");
    Check(FFmpegCapabilityProbe::IsNewEnough({.major = 6, .minor = 0, .gitBuild = false}), "6.0 is enough");
    Check(!FFmpegCapabilityProbe::IsNewEnough({.major = 4, .minor = 4, .gitBuild = false}), "4.4 is too old");
    Check(!FFmpegCapabilityProbe::IsNewEnough({.major = 5, .minor = 0, .gitBuild = false}), "5.0 is too old");
}

// ---- Lists -----------------------------------------------------------------

void CodecsParsing()
{
    const auto mac = FFmpegCapabilityProbe::ParseCodecs(kMacCodecs);
    Check(mac.at("h264") == std::vector<std::string>{"h264_videotoolbox"}, "listed encoders");
    Check(mac.at("mpeg4") == std::vector<std::string>{"mpeg4"}, "an encoder named after its codec");
    Check(mac.at("aac") == std::vector<std::string>{"aac", "aac_at"}, "decoders aren't encoders");
    Check(!mac.contains("eac3") && !mac.contains("012v"), "decode-only rows are left out");

    const auto ubuntu = FFmpegCapabilityProbe::ParseCodecs(kUbuntuCodecs);
    Check(ubuntu.at("h264") ==
            std::vector<std::string>{"libx264", "libx264rgb", "h264_nvenc", "h264_qsv",
                "h264_v4l2m2m", "h264_vaapi"},
        "a distro build's encoders, in ffmpeg's order");
    Check(ubuntu.at("vp9") == std::vector<std::string>{"libvpx-vp9", "vp9_vaapi"}, "vp9 encoders");
}

void MuxersParsing()
{
    const auto newer = FFmpegCapabilityProbe::ParseMuxers(kNewMuxers);
    Check(newer.contains("dash") && newer.contains("hls") && newer.contains("webvtt"),
        "muxers from a three-column flags list");
    Check(newer.size() == 4, "legend rows aren't muxers");

    const auto older = FFmpegCapabilityProbe::ParseMuxers(kOldMuxers);
    Check(older.contains("dash") && older.contains("mp4") && !older.contains("hls"),
        "muxers from a two-column flags list");
}

void EncoderOptionParsing()
{
    Check(FFmpegCapabilityProbe::HelpHasOption(kVideoToolboxHelp, "allow_sw"), "allow_sw listed");
    Check(!FFmpegCapabilityProbe::HelpHasOption(kVideoToolboxHelp, "allow"), "only whole names");
    Check(!FFmpegCapabilityProbe::HelpHasOption(kVideoToolboxHelp, "hw_encoding"),
        "an option the encoder lacks");
}

// ---- Locator ---------------------------------------------------------------

struct ToolFolder
{
    fs::path path;

    explicit ToolFolder(const std::string& name, bool withProbe = true)
    {
        path = fs::temp_directory_path() /
            ("mango-tools-" + name + "-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path);
        std::ofstream(path / ExecutableFileName("ffmpeg")) << "x";
        if (withProbe)
        {
            std::ofstream(path / ExecutableFileName("ffprobe")) << "x";
        }
    }
    ~ToolFolder()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void LocatorSearchPath()
{
#if defined(_WIN32)
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    const ToolFolder onlyFfmpeg("half", false);
    const ToolFolder both("both");
    const ToolFolder later("later");

    const auto searchPath = std::string("/no/such/folder") + separator + onlyFfmpeg.path.string() +
        separator + separator + both.path.string() + separator + later.path.string();
    const auto found = FFmpegLocator::InSearchPath(searchPath);
    Check(found && found->ffmpeg.parent_path() == both.path,
        "the first PATH folder with both tools");
    Check(found && found->origin == FFmpegInstall::Origin::systemPath, "found in PATH");
    Check(!FFmpegLocator::InSearchPath("/no/such/folder"), "nothing in PATH");
}

void LocatorDeduplicates()
{
    const ToolFolder folder("dedup");
    const ToolFolder other("other");

    auto bundled = FFmpegLocator::InFolder(FFmpegInstall::Origin::bundled, folder.path);
    auto configured =
        FFmpegLocator::InFolder(FFmpegInstall::Origin::configured, folder.path / "." / "");
    auto fromPath = FFmpegLocator::InFolder(FFmpegInstall::Origin::systemPath, other.path);
    Check(bundled && configured && fromPath, "both tools found in each folder");
    if (!bundled || !configured || !fromPath)
    {
        return;
    }

    const auto unique = FFmpegLocator::Deduplicate({*bundled, *configured, *fromPath});
    Check(unique.size() == 2, "the same file counts once");
    Check(unique.size() == 2 && unique[0].origin == FFmpegInstall::Origin::bundled &&
            unique[1].origin == FFmpegInstall::Origin::systemPath,
        "the earlier candidate is kept");
}

// ---- Bundled install -------------------------------------------------------

void BundledInstallIsFound()
{
    const auto* primary = FFmpegInstalls::Primary();
    Check(primary != nullptr, "the bundled ffmpeg is found");
    if (!primary)
    {
        return;
    }

    Check(primary->origin == FFmpegInstall::Origin::bundled, "the bundled one is primary");
    Check(primary->ffmpeg.parent_path() == ExecutableDirectory(), "next to the executable");
    Check(!primary->version.empty(), "its version is read");
    Check(primary->HasMuxer("dash") && primary->HasMuxer("hls"), "it has both stream muxers");
    Check(primary->encodersByCodec.contains("aac"), "it lists the AAC encoder");
#if defined(__APPLE__)
    Check(primary->ListsEncoder("h264", "h264_videotoolbox"), "it lists VideoToolbox");
    Check(primary->CodecOf("h264_videotoolbox") == "h264", "VideoToolbox makes H.264");
#endif
}

}  // namespace

std::vector<TestCase> FFmpegInstallTestCases()
{
    return {
        {"ffmpeg version parsing", VersionParsing},
        {"ffmpeg codecs parsing", CodecsParsing},
        {"ffmpeg muxers parsing", MuxersParsing},
        {"ffmpeg encoder option parsing", EncoderOptionParsing},
        {"ffmpeg locator PATH search", LocatorSearchPath},
        {"ffmpeg locator de-duplication", LocatorDeduplicates},
        {"ffmpeg bundled install", BundledInstallIsFound},
    };
}
