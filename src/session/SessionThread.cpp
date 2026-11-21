#include "SessionThread.hpp"

#include <algorithm>

namespace mango
{

SessionThread::SessionThread()
    : m_thread([this](std::stop_token stop) { Run(stop); })
{
}

SessionThread::~SessionThread() { Stop(); }

void SessionThread::Post(Task task)
{
    {
        std::lock_guard lock(m_mutex);
        if (m_stopped)
        {
            return;
        }
        m_tasks.push_back(std::move(task));
        ++m_generation;
    }
    m_wake.notify_one();
}

SessionThread::TimerId SessionThread::PostAt(Clock::time_point at, Task task)
{
    TimerId timerId = 0;
    {
        std::lock_guard lock(m_mutex);
        if (m_stopped)
        {
            return 0;
        }
        timerId = m_nextTimerId++;
        m_timers.emplace(timerId, Timer{at, std::move(task)});
        ++m_generation;
    }
    m_wake.notify_one();
    return timerId;
}

SessionThread::TimerId SessionThread::PostAfter(Clock::duration delay, Task task)
{
    return PostAt(Clock::now() + delay, std::move(task));
}

void SessionThread::Cancel(TimerId timerId)
{
    std::lock_guard lock(m_mutex);
    m_timers.erase(timerId);
}

void SessionThread::Stop()
{
    {
        std::lock_guard lock(m_mutex);
        m_stopped = true;
        m_tasks.clear();
        m_timers.clear();
        ++m_generation;
    }
    m_wake.notify_all();
    if (m_thread.joinable() && !IsCurrent())
    {
        m_thread.request_stop();
        m_thread.join();
    }
}

bool SessionThread::IsCurrent() const { return m_thread.get_id() == std::this_thread::get_id(); }

void SessionThread::Run(std::stop_token stop)
{
    while (auto task = WaitForTask(stop))
    {
        (*task)();
    }
}

std::optional<SessionThread::Task> SessionThread::WaitForTask(std::stop_token stop)
{
    std::unique_lock lock(m_mutex);

    while (!stop.stop_requested() && !m_stopped)
    {
        if (!m_tasks.empty())
        {
            auto task = std::move(m_tasks.front());
            m_tasks.pop_front();
            return task;
        }
        if (auto timer = TakeDueTimerLocked(Clock::now()))
        {
            return timer;
        }

        // Woken by anything posted meanwhile (it may be due sooner), by
        // the next timer, or by a stop.
        const auto seen = m_generation;
        const auto changed = [this, seen] { return m_generation != seen; };
        if (const auto next = NextTimerLocked())
        {
            m_wake.wait_until(lock, stop, *next, changed);
        }
        else
        {
            m_wake.wait(lock, stop, changed);
        }
    }
    return std::nullopt;
}

std::optional<SessionThread::Task> SessionThread::TakeDueTimerLocked(Clock::time_point now)
{
    // Earliest first; equal times in the order they were posted.
    const auto due = std::ranges::min_element(m_timers, {},
        [](const auto& entry) { return std::pair(entry.second.at, entry.first); });
    if (due == m_timers.end() || due->second.at > now)
    {
        return std::nullopt;
    }

    auto task = std::move(due->second.task);
    m_timers.erase(due);
    return task;
}

std::optional<SessionThread::Clock::time_point> SessionThread::NextTimerLocked() const
{
    const auto next = std::ranges::min_element(m_timers, {}, [](const auto& entry) { return entry.second.at; });
    return next == m_timers.end() ? std::nullopt : std::optional(next->second.at);
}

}  // namespace mango
