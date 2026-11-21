#pragma once

#include <chrono>

// How long the native apps wait after a stream starts before handing it to a
// player — the macOS AirPlay sheet or a Chromecast LOAD — so ffmpeg has the
// first segments ready by the time they're requested. Stored in
// settings.json as "streamStartDelayMs"; edit it there.
class StreamStartDelaySetting
{
   public:
    static constexpr std::chrono::milliseconds defaultDelay{3000};

    // Clamped to [0, maxDelay] — a typo in settings.json shouldn't leave
    // Play stuck on its loader for minutes.
    static constexpr std::chrono::milliseconds maxDelay{30000};

    static std::chrono::milliseconds Get();

    // Once the conversion is this far along (0-100), the wait ends early —
    // the first segments are there, so there's nothing left to wait for.
    // Stored in settings.json as "streamStartReadyPercent".
    static constexpr int defaultReadyPercent = 10;

    static int ReadyPercent();
};
