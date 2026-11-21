#include "FFmpegStartupProbe.hpp"

#include "../../file/File.hpp"
#include "../../log/Log.hpp"
#include "install/FFmpegCapabilityProbe.hpp"
#include "install/FFmpegLocator.hpp"
#include "encoders/EncoderDrivers.hpp"

namespace
{
// The video codecs whose encoders the startup log lists.
constexpr std::string_view kLoggedCodecs[] = {"h264", "hevc"};

std::string JoinWords(const std::vector<std::string>& words)
{
    std::string joined;
    for (const auto& word : words)
    {
        joined += (joined.empty() ? "" : " ") + word;
    }
    return joined;
}

std::string VideoEncoderSummary(const FFmpegInstall& install)
{
    std::string summary;
    for (const auto codec : kLoggedCodecs)
    {
        const auto it = install.encodersByCodec.find(std::string(codec));
        const auto encoders = it != install.encodersByCodec.end() ? JoinWords(it->second) : "none";
        summary += (summary.empty() ? "" : ", ") + std::string(codec) + ": " + encoders;
    }
    return summary;
}
}  // namespace

const std::vector<ConversionOption>& ConversionOptions::For(PlaylistType playlist) const
{
    return playlist == PlaylistType::HLS ? hls : dash;
}

FFmpegStartupProbe& FFmpegStartupProbe::Get()
{
    static FFmpegStartupProbe probe;
    return probe;
}

FFmpegStartupProbe::FFmpegStartupProbe()
    : m_installs(m_installsPromise.get_future().share()),
      m_options(m_optionsPromise.get_future().share())
{
}

FFmpegStartupProbe::~FFmpegStartupProbe() { Stop(); }

void FFmpegStartupProbe::Start()
{
    std::lock_guard lock(m_mutex);
    if (m_started)
    {
        return;
    }
    m_started = true;
    m_thread = std::thread([this] { Run(); });
}

void FFmpegStartupProbe::Stop()
{
    m_stopRequested = true;

    std::lock_guard lock(m_mutex);
    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

const std::vector<FFmpegInstall>& FFmpegStartupProbe::Installs()
{
    Start();
    return m_installs.get();
}

const std::vector<ConversionOption>& FFmpegStartupProbe::Options(PlaylistType playlist)
{
    Start();
    return m_options.get().For(playlist);
}

void FFmpegStartupProbe::Run()
{
    auto installs = FindInstalls();
    LogInstalls(installs);
    m_installsPromise.set_value(std::move(installs));

    m_optionsPromise.set_value(TestEncoders(m_installs.get()));
}

std::vector<FFmpegInstall> FFmpegStartupProbe::FindInstalls() const
{
    std::vector<FFmpegInstall> installs;
    for (const auto& candidate : FFmpegLocator::Candidates())
    {
        if (m_stopRequested)
        {
            break;
        }
        if (auto install = FFmpegCapabilityProbe::Read(candidate))
        {
            installs.push_back(std::move(*install));
        }
    }
    return installs;
}

ConversionOptions FFmpegStartupProbe::TestEncoders(const std::vector<FFmpegInstall>& installs) const
{
    // The test encodes run one after the other: two hardware encoders
    // starting at once can disturb each other.
    VideoEncoderProbe probe(
        installs, EncoderDrivers::ForThisPlatform(), &VideoEncoderProbe::TestEncode, m_stopRequested);

    ConversionOptions options{.dash = probe.OptionsFor(PlaylistType::DASH),
        .hls = probe.OptionsFor(PlaylistType::HLS)};
    LogOptions(PlaylistType::DASH, options.dash);
    LogOptions(PlaylistType::HLS, options.hls);
    return options;
}

void FFmpegStartupProbe::LogOptions(PlaylistType playlist, const std::vector<ConversionOption>& options)
{
    const auto* protocol = playlist == PlaylistType::HLS ? "HLS" : "DASH";
    if (options.empty())
    {
        Log::Warning(LogCategory::Transcode, "{} conversion: no working video encoder", protocol);
        return;
    }
    for (const auto& option : options)
    {
        Log::Info(LogCategory::Transcode, "{} conversion: {} with {} ({}{}, {} ffmpeg)", protocol,
            option.codec, option.driver->Name(), option.driver->Encoder(),
            option.driver->IsSoftware() ? ", software" : "",
            FFmpegInstall::OriginName(option.install->origin));
    }
}

void FFmpegStartupProbe::LogInstalls(const std::vector<FFmpegInstall>& installs)
{
    if (installs.empty())
    {
        Log::Warning(LogCategory::Transcode, "no ffmpeg install found");
        return;
    }

    for (const auto& install : installs)
    {
        Log::Info(LogCategory::Transcode, "ffmpeg {}{}: {} version {}; {}; dash {}, hls {}",
            FFmpegInstall::OriginName(install.origin), &install == &installs.front() ? " (primary)" : "",
            File::ToUtf8(install.ffmpeg), install.version.empty() ? "unknown" : install.version,
            VideoEncoderSummary(install), install.HasMuxer("dash") ? "yes" : "no",
            install.HasMuxer("hls") ? "yes" : "no");
    }
}
