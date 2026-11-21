#include "ChangeNotifier.hpp"

#include <algorithm>

namespace mango
{

bool SessionSnapshots::Store(SessionPart part, std::string json)
{
    std::lock_guard lock(m_mutex);
    auto& stored = m_json[static_cast<std::size_t>(part)];
    if (stored == json)
    {
        return false;
    }
    stored = std::move(json);
    return true;
}

std::string SessionSnapshots::Get(SessionPart part) const
{
    std::lock_guard lock(m_mutex);
    return m_json[static_cast<std::size_t>(part)];
}

ChangeNotifier::ChangeNotifier(SessionThread& thread, std::chrono::milliseconds interval)
    : m_thread(thread), m_interval(interval)
{
}

void ChangeNotifier::SetHandler(SessionChangedHandler handler)
{
    std::lock_guard lock(m_mutex);
    m_handler = std::move(handler);
}

void ChangeNotifier::Mark(SessionParts parts)
{
    if (parts.Empty())
    {
        return;
    }

    std::lock_guard lock(m_mutex);
    m_dirty |= parts;
    if (m_flushScheduled)
    {
        return;
    }

    m_flushScheduled = true;
    const auto at = std::max(SessionThread::Clock::now(), m_lastFlush + m_interval);
    m_thread.PostAt(at, [this] { Flush(); });
}

void ChangeNotifier::Flush()
{
    SessionChangedHandler handler;
    SessionParts changed;
    {
        std::lock_guard lock(m_mutex);
        changed = m_dirty;
        m_dirty = {};
        m_flushScheduled = false;
        m_lastFlush = SessionThread::Clock::now();
        handler = m_handler;
    }

    if (handler && !changed.Empty())
    {
        handler(changed);
    }
}

}  // namespace mango
