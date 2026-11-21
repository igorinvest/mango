#include "StreamActivity.hpp"

std::atomic<std::chrono::steady_clock::rep> StreamActivity::s_lastRequestTicks{kNever};

void StreamActivity::NoteRequest()
{
    s_lastRequestTicks.store(std::chrono::steady_clock::now().time_since_epoch().count());
}

std::optional<std::chrono::steady_clock::duration> StreamActivity::TimeSinceLastRequest()
{
    const auto ticks = s_lastRequestTicks.load();
    if (ticks == kNever)
    {
        return std::nullopt;
    }

    const std::chrono::steady_clock::time_point lastRequest{std::chrono::steady_clock::duration{ticks}};
    return std::chrono::steady_clock::now() - lastRequest;
}
