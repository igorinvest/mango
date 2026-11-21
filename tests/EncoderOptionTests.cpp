// Encoder drivers and conversion options per protocol: the probe's order
// with fake installs and a fake test encode, the choice per stream profile,
// driver arguments, and the platform's software fallback on the bundled
// ffmpeg.

#include <algorithm>
#include <atomic>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "TestCheck.hpp"
#include "media/ffmpeg/encoders/EncoderDrivers.hpp"
#include "media/ffmpeg/install/FFmpegInstalls.hpp"
#include "media/ffmpeg/FfmpegBackend.hpp"
#include "media/ffmpeg/encoders/VideoEncoderProbe.hpp"

namespace
{

using Origin = FFmpegInstall::Origin;

FFmpegInstall FakeInstall(
    Origin origin, std::vector<std::string> h264Encoders, std::set<std::string> muxers = {"dash", "hls"})
{
    FFmpegInstall install;
    install.origin = origin;
    install.ffmpeg = origin == Origin::bundled ? "/app/ffmpeg" : "/usr/bin/ffmpeg";
    install.encodersByCodec["h264"] = std::move(h264Encoders);
    install.muxers = std::move(muxers);
    return install;
}

const StandardEncoderDriver kHardware({.name = "hw", .encoder = "h264_hw"});
const StandardEncoderDriver kOtherHardware({.name = "hw2", .encoder = "h264_hw2"});
const StandardEncoderDriver kSoftware({.name = "sw", .encoder = "libx264", .software = true});

// A fake test encode: drivers in `working` work in every install that lists
// them. Records every pair it was asked about.
struct FakeTester
{
    std::set<std::string> working;
    std::vector<std::pair<std::string, Origin>> tested;

    VideoEncoderProbe::Tester Bind()
    {
        return [this](const EncoderDriver& driver, const FFmpegInstall& install)
        {
            tested.emplace_back(driver.Name(), install.origin);
            return working.contains(driver.Name());
        };
    }
};

std::atomic<bool> notStopped{false};

bool IsOption(const std::vector<ConversionOption>& options, const std::string& driver, Origin origin)
{
    return options.size() == 1 && options[0].codec == "h264" && options[0].driver->Name() == driver &&
        options[0].install->origin == origin;
}

// ---- Probe order -----------------------------------------------------------

void HardwareBeatsExternalSoftware()
{
    const std::vector installs = {
        FakeInstall(Origin::bundled, {"h264_hw"}), FakeInstall(Origin::systemPath, {"libx264", "h264_hw"})};
    FakeTester tester{.working = {"hw", "sw"}, .tested = {}};
    VideoEncoderProbe probe(installs, {&kHardware, &kSoftware}, tester.Bind(), notStopped);

    Check(IsOption(probe.OptionsFor(PlaylistType::DASH), "hw", Origin::bundled),
        "hardware in the bundled ffmpeg is chosen");
    Check(tester.tested.size() == 1, "the first working driver ends the search");
}

void ExternalHardwareBeatsBundledSoftware()
{
    const std::vector installs = {
        FakeInstall(Origin::bundled, {"libx264"}), FakeInstall(Origin::systemPath, {"h264_hw"})};
    FakeTester tester{.working = {"hw", "sw"}, .tested = {}};
    VideoEncoderProbe probe(installs, {&kHardware, &kSoftware}, tester.Bind(), notStopped);

    Check(IsOption(probe.OptionsFor(PlaylistType::DASH), "hw", Origin::systemPath),
        "driver first, install second");
}

void SoftwareWhenHardwareFails()
{
    const std::vector installs = {
        FakeInstall(Origin::bundled, {"h264_hw", "h264_hw2"}), FakeInstall(Origin::systemPath, {"libx264"})};
    FakeTester tester{.working = {"sw"}, .tested = {}};
    VideoEncoderProbe probe(installs, {&kHardware, &kOtherHardware, &kSoftware}, tester.Bind(), notStopped);

    Check(IsOption(probe.OptionsFor(PlaylistType::DASH), "sw", Origin::systemPath),
        "the software fallback after every hardware driver fails");
    Check(tester.tested.size() == 3, "each listed driver tested once");
}

void UnlistedEncodersAreNotTested()
{
    const std::vector installs = {FakeInstall(Origin::bundled, {"h264_hw"})};
    FakeTester tester{.working = {"hw", "hw2", "sw"}, .tested = {}};
    VideoEncoderProbe probe(installs, {&kOtherHardware, &kSoftware, &kHardware}, tester.Bind(), notStopped);

    Check(IsOption(probe.OptionsFor(PlaylistType::DASH), "hw", Origin::bundled),
        "only the listed encoder can be chosen");
    Check(tester.tested.size() == 1 && tester.tested[0].first == "hw",
        "encoders the install doesn't list aren't test-encoded");
}

void ProtocolsShareTests()
{
    const std::vector installs = {FakeInstall(Origin::bundled, {"h264_hw"})};
    FakeTester tester{.working = {"hw"}, .tested = {}};
    VideoEncoderProbe probe(installs, {&kHardware}, tester.Bind(), notStopped);

    const auto dash = probe.OptionsFor(PlaylistType::DASH);
    const auto hls = probe.OptionsFor(PlaylistType::HLS);
    Check(IsOption(dash, "hw", Origin::bundled) && IsOption(hls, "hw", Origin::bundled),
        "the same option for both protocols");
    Check(tester.tested.size() == 1, "a pair tested for DASH isn't tested again for HLS");
}

void MissingMuxerLimitsProtocol()
{
    const std::vector installs = {FakeInstall(Origin::bundled, {"h264_hw"}, {"dash"})};
    FakeTester tester{.working = {"hw"}, .tested = {}};
    VideoEncoderProbe probe(installs, {&kHardware}, tester.Bind(), notStopped);

    Check(IsOption(probe.OptionsFor(PlaylistType::DASH), "hw", Origin::bundled), "DASH converts");
    Check(probe.OptionsFor(PlaylistType::HLS).empty(), "no HLS option without the hls muxer");
}

void NothingWorks()
{
    const std::vector installs = {FakeInstall(Origin::bundled, {"h264_hw"})};
    FakeTester tester{.working = {}, .tested = {}};
    VideoEncoderProbe probe(installs, {&kHardware}, tester.Bind(), notStopped);

    Check(probe.OptionsFor(PlaylistType::DASH).empty(), "no option when no test encode works");
}

void StopEndsTheSearch()
{
    const std::vector installs = {FakeInstall(Origin::bundled, {"h264_hw"})};
    FakeTester tester{.working = {"hw"}, .tested = {}};
    const std::atomic<bool> stopped{true};
    VideoEncoderProbe probe(installs, {&kHardware}, tester.Bind(), stopped);

    Check(probe.OptionsFor(PlaylistType::DASH).empty() && tester.tested.empty(),
        "a stopped probe runs no test encode");
}

// ---- Choice per stream profile ---------------------------------------------

void ProfileLimitsTheChoice()
{
    FFmpegInstall install = FakeInstall(Origin::bundled, {"h264_hw"});
    const std::vector<ConversionOption> options = {
        {.codec = "h264", .driver = &kHardware, .install = &install}};

    StreamProfile accepts;
    accepts.videoCapabilities = {{.codec = "hevc"}, {.codec = "h264"}};
    Check(FfmpegBackend::Choose(options, accepts) == &options[0], "a codec the profile accepts");

    StreamProfile refuses;
    refuses.videoCapabilities = {{.codec = "hevc"}};
    Check(FfmpegBackend::Choose(options, refuses) == nullptr,
        "a codec the profile turned off isn't chosen");
}

// ---- Drivers ---------------------------------------------------------------

bool Contains(const std::vector<std::string>& args, const std::vector<std::string>& run)
{
    return !std::ranges::search(args, run).empty();
}

// VAAPI uploads after the software deinterlacer, scales on the GPU and
// leaves the pixel format to the upload.
void VaapiArguments(const VideoTargetSpec& spec)
{
    const auto* vaapi = EncoderDrivers::Find("vaapi");
    Check(vaapi != nullptr && !vaapi->IsSoftware(), "vaapi driver exists");
    if (!vaapi)
    {
        return;
    }

    const auto args = vaapi->EncodeArgs(spec);
    Check(args.size() == 6 && args[0] == "-c:v" && args[1] == "h264_vaapi" && args[2] == "-vf" &&
            args[3].starts_with("bwdif=") && args[3].contains(",format=nv12,hwupload,scale_vaapi=w=") &&
            Contains(args, {"-b:v", "4M"}) && !Contains(args, {"-pix_fmt"}),
        "vaapi converts on the GPU");
    Check(Contains(vaapi->InputArgs(), {"-vaapi_device", "/dev/dri/renderD128"}), "vaapi opens the render node");
    Check(Contains(vaapi->ProbeArgs(), {"-vaapi_device", "/dev/dri/renderD128"}) &&
            Contains(vaapi->ProbeArgs(), {"-vf", "format=nv12,hwupload", "-c:v", "h264_vaapi"}),
        "and test-encodes the same way");
}

void DriverArguments()
{
    VideoTargetSpec spec{.copy = false, .codec = "", .preset = "", .bitrate = "4M",
        .pix_fmt = "yuv420p", .maxWidth = 1920, .maxHeight = 1080, .deinterlace = true, .driver = ""};

    const auto* hardware = EncoderDrivers::Find("nvenc");
    Check(hardware != nullptr, "nvenc driver exists");
    if (hardware)
    {
        const auto args = hardware->EncodeArgs(spec);
        Check(args.size() == 8 && args[0] == "-c:v" && args[1] == "h264_nvenc" && args[2] == "-vf" &&
                args[3].starts_with("bwdif=") && args[3].contains(",scale=") &&
                Contains(args, {"-pix_fmt", "yuv420p"}) && Contains(args, {"-b:v", "4M"}),
            "a hardware driver keeps today's arguments");
        Check(hardware->InputArgs().empty() && !hardware->IsSoftware(), "nvenc takes frames from memory");
    }

    const auto* videoToolboxSw = EncoderDrivers::Find("videotoolbox-sw");
    Check(videoToolboxSw && videoToolboxSw->IsSoftware() && videoToolboxSw->RequiredOption() == "allow_sw",
        "VideoToolbox's software fallback");
    if (videoToolboxSw)
    {
        Check(Contains(videoToolboxSw->EncodeArgs(spec), {"-c:v", "h264_videotoolbox", "-allow_sw", "1"}),
            "it converts with -allow_sw 1");
        Check(Contains(videoToolboxSw->ProbeArgs(), {"-c:v", "h264_videotoolbox", "-allow_sw", "1"}),
            "and test-encodes with it");
    }

    const auto* mediaFoundation = EncoderDrivers::Find("mediafoundation-sw");
    Check(mediaFoundation &&
            Contains(mediaFoundation->EncodeArgs(spec), {"-c:v", "h264_mf", "-hw_encoding", "0"}),
        "Media Foundation's software fallback");

    const auto* videoToolbox = EncoderDrivers::Find("videotoolbox");
    Check(videoToolbox && !Contains(videoToolbox->ProbeArgs(), {"-allow_sw"}),
        "the hardware VideoToolbox driver stays hardware-only");

    VaapiArguments(spec);
}

void PlatformDriverOrder()
{
    std::vector<std::string> names;
    for (const auto* driver : EncoderDrivers::ForThisPlatform())
    {
        names.push_back(driver->Name());
    }
#if defined(__APPLE__)
    Check(names == std::vector<std::string>{"videotoolbox", "videotoolbox-sw"}, "macOS drivers");
#elif defined(_WIN32)
    Check(names == std::vector<std::string>{"nvenc", "qsv", "amf", "mediafoundation-sw"}, "Windows drivers");
#elif defined(__linux__)
    Check(names == std::vector<std::string>{"vaapi", "nvenc", "qsv", "x264"}, "Linux drivers");
#endif
}

// The OS software encoder works in the bundled ffmpeg, whatever the GPU.
void BundledSoftwareFallbackWorks()
{
    const auto* install = FFmpegInstalls::Primary();
#if defined(__APPLE__)
    const auto* driver = EncoderDrivers::Find("videotoolbox-sw");
#elif defined(_WIN32)
    const auto* driver = EncoderDrivers::Find("mediafoundation-sw");
#else
    const EncoderDriver* driver = nullptr;
#endif
    if (!install || !driver)
    {
        return;
    }

    Check(install->ListsEncoder("h264", driver->Encoder()), "the bundled ffmpeg lists it");
    Check(VideoEncoderProbe::TestEncode(*driver, *install), "its test encode works");
}

}  // namespace

std::vector<TestCase> EncoderOptionTestCases()
{
    return {
        {"encoder options: hardware beats external software", HardwareBeatsExternalSoftware},
        {"encoder options: external hardware beats bundled software", ExternalHardwareBeatsBundledSoftware},
        {"encoder options: software fallback", SoftwareWhenHardwareFails},
        {"encoder options: unlisted encoders", UnlistedEncodersAreNotTested},
        {"encoder options: tests shared across protocols", ProtocolsShareTests},
        {"encoder options: missing muxer", MissingMuxerLimitsProtocol},
        {"encoder options: nothing works", NothingWorks},
        {"encoder options: stop", StopEndsTheSearch},
        {"encoder options: profile codecs", ProfileLimitsTheChoice},
        {"encoder drivers: arguments", DriverArguments},
        {"encoder drivers: platform order", PlatformDriverOrder},
        {"encoder drivers: bundled software fallback", BundledSoftwareFallbackWorks},
    };
}
