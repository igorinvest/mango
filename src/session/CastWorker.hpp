#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

namespace mango
{

// One thread for blocking calls (cast I/O, starting a stream, discovery):
// jobs run one at a time, in order, and post their result back to the
// SessionThread themselves. The session decides what may be queued — e.g.
// only one cast command in flight.
class CastWorker
{
   public:
    using Job = std::function<void()>;

    // name is for the log only.
    explicit CastWorker(std::string name);
    ~CastWorker();

    CastWorker(const CastWorker&) = delete;
    CastWorker& operator=(const CastWorker&) = delete;

    // Ignored once Stop() was called.
    void Run(Job job);

    // Drops queued jobs, waits for the running one and joins.
    void Stop();

   private:
    void Loop(std::stop_token stop);

    std::string m_name;
    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::deque<Job> m_jobs;
    bool m_stopped = false;
    std::jthread m_thread;
};

}  // namespace mango
