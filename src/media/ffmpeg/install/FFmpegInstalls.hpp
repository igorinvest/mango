#pragma once

#include <string_view>
#include <vector>

#include "FFmpegInstall.hpp"

// The accepted ffmpeg installs, found once per process by
// FFmpegStartupProbe. Both calls wait for the probe's install stage.
class FFmpegInstalls
{
   public:
    // Accepted installs, in locator order (bundled first).
    [[nodiscard]] static const std::vector<FFmpegInstall>& All();

    // The first install - the one that probes, copies and extracts - or
    // nullptr when there's none.
    [[nodiscard]] static const FFmpegInstall* Primary();

    // The primary install when it has muxer, otherwise the first one that
    // does; nullptr when none has it.
    [[nodiscard]] static const FFmpegInstall* WithMuxer(std::string_view muxer);
};
