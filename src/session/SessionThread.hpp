#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

namespace mango
{

// The one thread the session's state lives on: tasks posted from anywhere
// run here one at a time, in order, and timers fire here too. Nothing on it
// blocks on the network — blocking calls go to a CastWorker, which posts the
// result back.
class SessionThread
{
   public:
    using Task = std::function<void()>;
    using Clock = std::chrono::steady_clock;
    using TimerId = std::uint64_t;

    SessionThread();
    ~SessionThread();

    SessionThread(const SessionThread&) = delete;
    SessionThread& operator=(const SessionThread&) = delete;

    // Ignored once Stop() was called.
    void Post(Task task);

    // Runs task at (or after) at, unless cancelled first. Never 0.
    TimerId PostAt(Clock::time_point at, Task task);
    TimerId PostAfter(Clock::duration delay, Task task);

    // A timer that already fired or was cancelled is ignored; 0 too.
    void Cancel(TimerId timerId);

    // Drops what's still queued and joins. Not from the thread itself.
    void Stop();

    [[nodiscard]] bool IsCurrent() const;

   private:
    struct Timer
    {
        Clock::time_point at;
        Task task;
    };

    void Run(std::stop_token stop);

    // The next task to run, waiting for one; nullopt once stopping.
    std::optional<Task> WaitForTask(std::stop_token stop);

    // Callers hold m_mutex.
    std::optional<Task> TakeDueTimerLocked(Clock::time_point now);
    std::optional<Clock::time_point> NextTimerLocked() const;

    mutable std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::deque<Task> m_tasks;
    std::map<TimerId, Timer> m_timers;
    TimerId m_nextTimerId = 1;

    // Bumped by every Post, PostAt and Stop, so a wait ends when there's
    // something new to look at.
    std::uint64_t m_generation = 0;
    bool m_stopped = false;
    std::jthread m_thread;
};

}  // namespace mango
