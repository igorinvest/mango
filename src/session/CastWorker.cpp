#include "CastWorker.hpp"

#include <exception>

#include "log/Log.hpp"

namespace mango
{

CastWorker::CastWorker(std::string name)
    : m_name(std::move(name)), m_thread([this](std::stop_token stop) { Loop(stop); })
{
}

CastWorker::~CastWorker() { Stop(); }

void CastWorker::Run(Job job)
{
    {
        std::lock_guard lock(m_mutex);
        if (m_stopped)
        {
            return;
        }
        m_jobs.push_back(std::move(job));
    }
    m_wake.notify_one();
}

void CastWorker::Stop()
{
    {
        std::lock_guard lock(m_mutex);
        m_stopped = true;
        m_jobs.clear();
    }
    m_wake.notify_all();
    if (m_thread.joinable() && m_thread.get_id() != std::this_thread::get_id())
    {
        m_thread.request_stop();
        m_thread.join();
    }
}

void CastWorker::Loop(std::stop_token stop)
{
    while (!stop.stop_requested())
    {
        Job job;
        {
            std::unique_lock lock(m_mutex);
            if (!m_wake.wait(lock, stop, [this] { return m_stopped || !m_jobs.empty(); }) || m_stopped)
            {
                return;
            }
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }

        // A throwing engine call must not take the worker down with it.
        try
        {
            job();
        }
        catch (const std::exception& e)
        {
            Log::Error(LogCategory::App, "{} worker: job failed: {}", m_name, e.what());
        }
    }
}

}  // namespace mango
