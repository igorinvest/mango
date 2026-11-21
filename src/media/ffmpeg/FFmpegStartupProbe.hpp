#pragma once

#include <atomic>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../pipeline/stream/profiles/StreamProfile.hpp"
#include "install/FFmpegInstall.hpp"
#include "encoders/VideoEncoderProbe.hpp"

// What the test encodes found: the working conversion options per protocol.
struct ConversionOptions
{
    std::vector<ConversionOption> dash;
    std::vector<ConversionOption> hls;

    [[nodiscard]] const std::vector<ConversionOption>& For(PlaylistType playlist) const;
};

// Finds the ffmpeg installs, reads their lists and runs the test encodes once
// per process, on a background thread started with the engine, so the first
// stream doesn't wait for them. The two stages finish separately: a library
// scan needs only an install, not the test encodes.
class FFmpegStartupProbe
{
   public:
    static FFmpegStartupProbe& Get();

    ~FFmpegStartupProbe();

    FFmpegStartupProbe(const FFmpegStartupProbe&) = delete;
    FFmpegStartupProbe& operator=(const FFmpegStartupProbe&) = delete;

    // Starts the background thread; later calls do nothing.
    void Start();

    // Asks the thread to finish after its current ffmpeg run, and joins it.
    // Whatever is unfinished stays empty.
    void Stop();

    // The accepted installs in locator order. Starts the probe if it isn't
    // running yet and waits for its install stage.
    [[nodiscard]] const std::vector<FFmpegInstall>& Installs();

    // The working options for playlist, best first. Starts the probe if
    // needed and waits for the test encodes.
    [[nodiscard]] const std::vector<ConversionOption>& Options(PlaylistType playlist);

   private:
    FFmpegStartupProbe();

    void Run();

    [[nodiscard]] std::vector<FFmpegInstall> FindInstalls() const;

    [[nodiscard]] ConversionOptions TestEncoders(const std::vector<FFmpegInstall>& installs) const;

    static void LogInstalls(const std::vector<FFmpegInstall>& installs);
    static void LogOptions(PlaylistType playlist, const std::vector<ConversionOption>& options);

    std::mutex m_mutex;
    bool m_started{false};
    std::thread m_thread;
    std::atomic<bool> m_stopRequested{false};

    std::promise<std::vector<FFmpegInstall>> m_installsPromise;
    std::shared_future<std::vector<FFmpegInstall>> m_installs;

    std::promise<ConversionOptions> m_optionsPromise;
    std::shared_future<ConversionOptions> m_options;
};
