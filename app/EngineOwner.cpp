#include "EngineOwner.hpp"

#include <format>
#include <future>
#include <print>
#include <string>
#include <vector>

#include "console/ConsoleText.hpp"
#include "media/VideoEncoderOption.hpp"

// The part of GetSupportedFormatsJson the startup report reads. Outside the
// anonymous namespace: glaze's reflection needs a type with linkage.
struct ConversionList
{
    std::vector<VideoEncoderOption> conversion;
};

namespace mango::cli
{

namespace
{

// By default the port opens only while something streams; `play` and
// `stream` print the address then.
void PrintBanner(const mango::Engine& engine)
{
    if (engine.GetListenOnlyWhileStreaming())
    {
        std::println("Mango media server: listens only while streaming (`set listen always` to change).");
    }
    else
    {
        const auto status = engine.GetStatus();
        std::println("Mango media server: http://{}:{}", status.ip, status.port);
    }
    std::println("Type help for commands.");
}

// The "conversion" list of Engine::GetSupportedFormatsJson, one line.
void PrintConversionOptions(const mango::Engine& engine, PlaylistType playlist)
{
    const auto list = ParseJson<ConversionList>(engine.GetSupportedFormatsJson(playlist));
    const auto* protocol = playlist == PlaylistType::HLS ? "AirPlay (HLS)" : "Chromecast (DASH)";
    if (!list || list->conversion.empty())
    {
        std::println("{} conversion: no working video encoder", protocol);
        return;
    }

    std::string text;
    for (const auto& option : list->conversion)
    {
        text += std::format("{}{} with {}{}{}", text.empty() ? "" : ", ", option.codec, option.encoder,
            option.software ? " (software)" : "", option.external ? " (system ffmpeg)" : "");
    }
    std::println("{} conversion: {}", protocol, text);
}

// ffmpeg is found in the background (see Engine::GetMediaTools): its lines
// are printed when ready, without holding up the prompt. With no ffmpeg at
// all, the session's ffmpegMissing notice says so instead.
std::thread PrintMediaToolsWhenReady(const mango::Engine& engine)
{
    return std::thread(
        [&engine]
        {
            const auto tools = engine.GetMediaTools();
            if (tools.empty())
            {
                return;
            }
            for (const auto& line : tools)
            {
                std::println("ffmpeg {}", line);
            }
            PrintConversionOptions(engine, PlaylistType::DASH);
            PrintConversionOptions(engine, PlaylistType::HLS);
        });
}

}  // namespace

EngineOwner::EngineOwner(Output& events)
    : m_watcher(m_engine.GetSession(), events),
      m_remote(m_engine.GetRemote()),
      m_context(m_engine, m_watcher, m_remote, events),
      m_console(m_context)
{
}

EngineOwner::~EngineOwner()
{
    if (m_toolsReport.joinable())
    {
        m_toolsReport.join();
    }
}

// Also starts the session: devices, the TV's status, playback reports.
void EngineOwner::Start()
{
    m_engine.Start();
    PrintBanner(m_engine);
    m_toolsReport = PrintMediaToolsWhenReady(m_engine);
}

void EngineOwner::Stop()
{
    if (m_toolsReport.joinable())
    {
        m_toolsReport.join();
    }
    m_engine.Stop();
}

void WaitUntilStopped()
{
    std::promise<void>().get_future().wait();
}

}  // namespace mango::cli
