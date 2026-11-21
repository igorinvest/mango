#include "EncoderDriver.hpp"

#include <algorithm>
#include <iterator>

std::vector<std::string> EncoderDriver::ProbeArgs() const
{
    // 64x64 used to be the test size, but that's below the minimum frame
    // size several hardware encoders (e.g. NVENC) require, which failed
    // every hardware candidate whether it worked or not. 320x240 clears
    // that floor for all current candidates.
    std::vector<std::string> args = {"-loglevel", "quiet"};
    std::ranges::copy(InputArgs(), std::back_inserter(args));
    args.insert(args.end(),
        {"-f", "lavfi", "-i", "color=black:s=320x240:d=0.1", "-frames:v", "1", "-c:v", Encoder()});
    std::ranges::copy(EncoderOptions(), std::back_inserter(args));
    args.insert(args.end(), {"-f", "null", "-"});
    return args;
}

std::vector<std::string> EncoderDriver::InputArgs() const { return {}; }

std::optional<std::string> EncoderDriver::VideoFilter(const VideoTargetSpec& spec) const
{
    std::vector<std::string> filters;
    if (auto deinterlace = DeinterlaceFilter(spec))
    {
        filters.push_back(std::move(*deinterlace));
    }
    if (auto scaleFilter = ScaleFilter(spec))
    {
        filters.push_back(std::move(*scaleFilter));
    }
    return FilterChain(filters);
}

std::vector<std::string> EncoderDriver::EncodeArgs(const VideoTargetSpec& spec) const
{
    std::vector<std::string> args = {"-c:v", Encoder()};
    std::ranges::copy(EncoderOptions(), std::back_inserter(args));

    if (auto videoFilter = VideoFilter(spec))
    {
        args.insert(args.end(), {"-vf", std::move(*videoFilter)});
    }
    if (!spec.pix_fmt.empty())
    {
        args.insert(args.end(), {"-pix_fmt", spec.pix_fmt});
    }
    if (!spec.bitrate.empty())
    {
        args.insert(args.end(), {"-b:v", spec.bitrate});
    }
    return args;
}

std::vector<std::string> EncoderDriver::EncoderOptions() const { return {}; }

std::optional<std::string> EncoderDriver::ScaleFilter(const VideoTargetSpec& spec)
{
    auto size = ScaleSize(spec);
    return size ? std::optional<std::string>("scale=" + *size) : std::nullopt;
}

std::optional<std::string> EncoderDriver::ScaleSize(const VideoTargetSpec& spec)
{
    if (spec.maxWidth <= 0 || spec.maxHeight <= 0)
    {
        return std::nullopt;
    }

    // Fits the frame into the box long side to long side, so it needs no
    // orientation info: ffmpeg applies a source's display rotation before
    // -vf runs, so iw/ih are already upright. min(1, ...) never scales up,
    // so the filter is safe to add on every conversion, and trunc(.../2)*2
    // keeps both sides even, which H.264 needs. ffmpeg's min() takes exactly
    // two arguments, hence the nesting.
    const auto longSide = std::to_string(std::max(spec.maxWidth, spec.maxHeight));
    const auto shortSide = std::to_string(std::min(spec.maxWidth, spec.maxHeight));
    const auto factor = "min(1,min(" + longSide + "/max(iw,ih)," + shortSide + "/min(iw,ih)))";

    return "w='trunc(iw*" + factor + "/2)*2':h='trunc(ih*" + factor + "/2)*2'";
}

std::optional<std::string> EncoderDriver::FilterChain(const std::vector<std::string>& filters)
{
    if (filters.empty())
    {
        return std::nullopt;
    }

    std::string chain = filters.front();
    for (size_t i = 1; i < filters.size(); ++i)
    {
        chain += "," + filters[i];
    }
    return chain;
}

std::optional<std::string> EncoderDriver::DeinterlaceFilter(const VideoTargetSpec& spec)
{
    if (!spec.deinterlace)
    {
        return std::nullopt;
    }
    // One frame per frame (25i stays 25p); deint=interlaced leaves frames
    // not flagged as interlaced untouched.
    return "bwdif=mode=send_frame:deint=interlaced";
}

StandardEncoderDriver::StandardEncoderDriver(Definition definition)
    : m_definition(std::move(definition))
{
}

std::string StandardEncoderDriver::Name() const { return m_definition.name; }

std::string StandardEncoderDriver::Encoder() const { return m_definition.encoder; }

bool StandardEncoderDriver::IsSoftware() const { return m_definition.software; }

std::string StandardEncoderDriver::RequiredOption() const { return m_definition.requiredOption; }

namespace
{
constexpr StandardEncoderDriver::Platform kThisPlatform =
#if defined(__APPLE__)
    StandardEncoderDriver::Platform::macOS;
#elif defined(_WIN32)
    StandardEncoderDriver::Platform::windows;
#elif defined(__linux__)
    StandardEncoderDriver::Platform::linuxOS;
#else
    StandardEncoderDriver::Platform::any;
#endif
}  // namespace

bool StandardEncoderDriver::IsAvailableOnThisPlatform() const
{
    return std::ranges::any_of(m_definition.platforms, [](Platform platform)
        { return platform == Platform::any || platform == kThisPlatform; });
}

std::vector<std::string> StandardEncoderDriver::EncoderOptions() const
{
    return m_definition.options;
}

// ---- VAAPI -----------------------------------------------------------------

std::string VaapiEncoderDriver::Name() const { return "vaapi"; }

std::string VaapiEncoderDriver::Encoder() const { return "h264_vaapi"; }

bool VaapiEncoderDriver::IsSoftware() const { return false; }

std::string VaapiEncoderDriver::RequiredOption() const { return {}; }

bool VaapiEncoderDriver::IsAvailableOnThisPlatform() const
{
#if defined(__linux__)
    return true;
#else
    return false;
#endif
}

std::vector<std::string> VaapiEncoderDriver::ProbeArgs() const
{
    std::vector<std::string> args = {"-loglevel", "quiet"};
    std::ranges::copy(InputArgs(), std::back_inserter(args));
    args.insert(args.end(), {"-f", "lavfi", "-i", "color=black:s=320x240:d=0.1", "-frames:v", "1",
                                "-vf", kUploadFilter, "-c:v", Encoder(), "-f", "null", "-"});
    return args;
}

std::vector<std::string> VaapiEncoderDriver::InputArgs() const
{
    return {"-vaapi_device", "/dev/dri/renderD128"};
}

std::optional<std::string> VaapiEncoderDriver::VideoFilter(const VideoTargetSpec& spec) const
{
    // Deinterlacing stays in software (bwdif), before the upload.
    std::vector<std::string> filters;
    if (auto deinterlace = DeinterlaceFilter(spec))
    {
        filters.push_back(std::move(*deinterlace));
    }
    filters.emplace_back(kUploadFilter);
    if (auto size = ScaleSize(spec))
    {
        filters.push_back("scale_vaapi=" + *size);
    }
    return FilterChain(filters);
}

std::vector<std::string> VaapiEncoderDriver::EncodeArgs(const VideoTargetSpec& spec) const
{
    std::vector<std::string> args = {"-c:v", Encoder()};
    if (auto videoFilter = VideoFilter(spec))
    {
        args.insert(args.end(), {"-vf", std::move(*videoFilter)});
    }
    if (!spec.bitrate.empty())
    {
        args.insert(args.end(), {"-b:v", spec.bitrate});
    }
    return args;
}
