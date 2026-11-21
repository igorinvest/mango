#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

class Timer
{
   public:
    Timer();
    ~Timer();

    void SetTimeout(std::chrono::milliseconds delay, std::function<void()> callback);

    void Cancel();

   private:
    void ThreadLoop();

    std::mutex mutex;
    std::condition_variable cv;

    std::jthread worker;

    bool active = false;
    std::chrono::steady_clock::time_point executeAt;
    std::function<void()> callback;
};
