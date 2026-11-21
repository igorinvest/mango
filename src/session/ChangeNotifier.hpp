#pragma once

#include <array>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>

#include "SessionState.hpp"
#include "SessionThread.hpp"

namespace mango
{

// Each part's latest JSON. Written on the session thread, read from any.
class SessionSnapshots
{
   public:
    // Returns whether json differs from what part held.
    bool Store(SessionPart part, std::string json);
    [[nodiscard]] std::string Get(SessionPart part) const;

   private:
    mutable std::mutex m_mutex;
    std::array<std::string, static_cast<std::size_t>(SessionPart::Count)> m_json;
};

// Coalesces changes: Mark() only notes the parts, and the handler gets every
// part marked since its last call, at most once per interval.
class ChangeNotifier
{
   public:
    ChangeNotifier(SessionThread& thread, std::chrono::milliseconds interval);

    void SetHandler(SessionChangedHandler handler);

    // Any thread.
    void Mark(SessionParts parts);

   private:
    void Flush();

    SessionThread& m_thread;
    const std::chrono::milliseconds m_interval;

    std::mutex m_mutex;
    SessionChangedHandler m_handler;
    SessionParts m_dirty;
    bool m_flushScheduled = false;
    SessionThread::Clock::time_point m_lastFlush{};
};

}  // namespace mango
