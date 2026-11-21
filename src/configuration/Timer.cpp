#include "Timer.hpp"

Timer::Timer()
{
    worker = std::jthread([this] { ThreadLoop(); });
}

Timer::~Timer() = default;

void Timer::SetTimeout(std::chrono::milliseconds delay, std::function<void()> cb)
{
    {
        std::lock_guard lock(mutex);

        executeAt = std::chrono::steady_clock::now() + delay;

        callback = std::move(cb);

        active = true;
    }

    cv.notify_one();
}

void Timer::Cancel()
{
    {
        std::lock_guard lock(mutex);

        active = false;
    }

    cv.notify_one();
}

void Timer::ThreadLoop()
{
    while (!worker.get_stop_token().stop_requested())
    {
        std::unique_lock lock(mutex);

        cv.wait(lock, [this] { return active; });

        while (active)
        {
            if (cv.wait_until(lock, executeAt, [this] { return !active; }))
            {
                break;
            }

            auto callbackCopy = callback;

            active = false;

            lock.unlock();

            if (callbackCopy)
            {
                callbackCopy();
            }

            break;
        }
    }
}
