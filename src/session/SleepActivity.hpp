#pragma once

#include <chrono>
#include <optional>

namespace mango
{

// Whether the computer should stay awake: something was served, converted
// or about to start within the grace period. That covers a pause (resume
// works anyway) and a conversion that stalls without reaching 100%. The
// apps make the OS calls (see NowPlayingState::shouldPreventSleep).
class SleepActivity
{
   public:
    using Clock = std::chrono::steady_clock;

    static constexpr auto gracePeriod = std::chrono::minutes(5);

    void Note(Clock::time_point at);
    [[nodiscard]] bool ShouldPrevent(Clock::time_point now) const;

   private:
    std::optional<Clock::time_point> m_lastActivity;
};

}  // namespace mango
