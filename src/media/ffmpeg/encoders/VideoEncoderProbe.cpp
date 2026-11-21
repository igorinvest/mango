#include "VideoEncoderProbe.hpp"

#include "../../../file/File.hpp"
#include "../../../log/Log.hpp"
#include "../../ConversionCodecs.hpp"
#include "../install/FFmpegCapabilityProbe.hpp"
#include "../process/FFmpegProcess.hpp"
#include "../FFmpegStartupProbe.hpp"

VideoEncoderProbe::VideoEncoderProbe(const std::vector<FFmpegInstall>& installs,
    std::vector<const EncoderDriver*> drivers, Tester tester, const std::atomic<bool>& stopRequested)
    : m_installs(installs),
      m_drivers(std::move(drivers)),
      m_tester(std::move(tester)),
      m_stopRequested(stopRequested)
{
}

std::vector<ConversionOption> VideoEncoderProbe::OptionsFor(PlaylistType playlist)
{
    std::vector<ConversionOption> options;
    for (const auto& codec : ConversionCodecs::ForPlaylist(playlist))
    {
        if (auto option = FirstWorking(codec, MuxerFor(playlist)))
        {
            options.push_back(*option);
        }
    }
    return options;
}

std::optional<ConversionOption> VideoEncoderProbe::FirstWorking(
    const std::string& codec, const char* muxer)
{
    // Driver first, install second: a hardware encoder in an external
    // ffmpeg beats the software fallback in the bundled one.
    for (const auto* driver : m_drivers)
    {
        for (const auto& install : m_installs)
        {
            if (m_stopRequested)
            {
                return std::nullopt;
            }
            if (install.ListsEncoder(codec, driver->Encoder()) && install.HasMuxer(muxer) &&
                Works(*driver, install))
            {
                return ConversionOption{.codec = codec, .driver = driver, .install = &install};
            }
        }
    }
    return std::nullopt;
}

bool VideoEncoderProbe::Works(const EncoderDriver& driver, const FFmpegInstall& install)
{
    const auto key = std::pair(&driver, &install);
    if (const auto it = m_tested.find(key); it != m_tested.end())
    {
        return it->second;
    }

    const bool works = m_tester(driver, install);
    m_tested.emplace(key, works);
    return works;
}

bool VideoEncoderProbe::TestEncode(const EncoderDriver& driver, const FFmpegInstall& install)
{
    const auto option = driver.RequiredOption();
    if (!option.empty() && !FFmpegCapabilityProbe::EncoderHasOption(install, driver.Encoder(), option))
    {
        Log::Warning(LogCategory::Transcode, "{} not used with {}: {} has no -{} option",
            driver.Name(), File::ToUtf8(install.ffmpeg), driver.Encoder(), option);
        return false;
    }

    const bool works = RunsEncode(driver, install);
    Log::Info(LogCategory::Transcode, "test encode with {} ({}): {}", driver.Name(),
        FFmpegInstall::OriginName(install.origin), works ? "works" : "failed");
    return works;
}

bool VideoEncoderProbe::RunsEncode(const EncoderDriver& driver, const FFmpegInstall& install)
{
    bool succeeded = false;
    auto process = FFmpegProcess::Start(install, driver.ProbeArgs(),
        [&succeeded](bool completedSuccessfully) { succeeded = completedSuccessfully; });
    if (!process)
    {
        return false;
    }

    process->Wait();
    return succeeded;
}

const char* VideoEncoderProbe::MuxerFor(PlaylistType playlist)
{
    return playlist == PlaylistType::HLS ? "hls" : "dash";
}

const std::vector<ConversionOption>& VideoEncoderProbe::Options(PlaylistType playlist)
{
    return FFmpegStartupProbe::Get().Options(playlist);
}
