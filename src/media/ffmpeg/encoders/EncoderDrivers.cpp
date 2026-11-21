#include "EncoderDrivers.hpp"

#include <memory>

using Platform = StandardEncoderDriver::Platform;

namespace
{
std::unique_ptr<EncoderDriver> Hardware(
    std::string name, std::string encoder, std::vector<Platform> platforms)
{
    return std::make_unique<StandardEncoderDriver>(StandardEncoderDriver::Definition{.name = std::move(name),
        .encoder = std::move(encoder),
        .platforms = std::move(platforms),
        .software = false,
        .options = {},
        .requiredOption = ""});
}

// requiredOption is the option's name in options, checked against the
// install's "-h encoder=" list.
std::unique_ptr<EncoderDriver> Software(std::string name, std::string encoder, Platform platform,
    std::vector<std::string> options = {}, std::string requiredOption = "")
{
    return std::make_unique<StandardEncoderDriver>(StandardEncoderDriver::Definition{.name = std::move(name),
        .encoder = std::move(encoder),
        .platforms = {platform},
        .software = true,
        .options = std::move(options),
        .requiredOption = std::move(requiredOption)});
}
}  // namespace

std::vector<std::unique_ptr<EncoderDriver>> EncoderDrivers::Create()
{
    // h264_videotoolbox without -allow_sw stays hardware-only (-require_sw
    // defaults to 0), so a Mac with a hardware encoder never gets the
    // software one. NVENC and QSV take frames from memory on Windows and
    // Linux alike; VAAPI needs its own driver (GPU upload, scale_vaapi).
    std::vector<std::unique_ptr<EncoderDriver>> drivers;
    drivers.push_back(Hardware("videotoolbox", "h264_videotoolbox", {Platform::macOS}));
    drivers.push_back(std::make_unique<VaapiEncoderDriver>());
    drivers.push_back(Hardware("nvenc", "h264_nvenc", {Platform::windows, Platform::linuxOS}));
    drivers.push_back(Hardware("qsv", "h264_qsv", {Platform::windows, Platform::linuxOS}));
    drivers.push_back(Hardware("amf", "h264_amf", {Platform::windows}));

    drivers.push_back(
        Software("videotoolbox-sw", "h264_videotoolbox", Platform::macOS, {"-allow_sw", "1"}, "allow_sw"));
    drivers.push_back(
        Software("mediafoundation-sw", "h264_mf", Platform::windows, {"-hw_encoding", "0"}, "hw_encoding"));
    drivers.push_back(Software("x264", "libx264", Platform::linuxOS));
    return drivers;
}

const std::vector<std::unique_ptr<EncoderDriver>>& EncoderDrivers::All()
{
    static const auto drivers = Create();
    return drivers;
}

std::vector<const EncoderDriver*> EncoderDrivers::ForThisPlatform()
{
    std::vector<const EncoderDriver*> drivers;
    for (const auto& driver : All())
    {
        if (driver->IsAvailableOnThisPlatform())
        {
            drivers.push_back(driver.get());
        }
    }
    return drivers;
}

const EncoderDriver* EncoderDrivers::Find(std::string_view name)
{
    for (const auto& driver : All())
    {
        if (driver->Name() == name)
        {
            return driver.get();
        }
    }
    return nullptr;
}
