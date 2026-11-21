#include "console/RemoteWatcher.hpp"

#include "console/ConsoleText.hpp"

namespace mango::cli
{

RemoteWatcher::RemoteWatcher(mango::RemoteStreaming& remote) : m_remote(remote)
{
    m_remote.SetChangedHandler([this] { OnChanged(); });
}

RemoteWatcher::~RemoteWatcher() { m_remote.SetChangedHandler({}); }

mango::RemoteState RemoteWatcher::State() const
{
    return ParseJson<mango::RemoteState>(m_remote.GetStateJson()).value_or(mango::RemoteState{});
}

bool RemoteWatcher::WaitUntil(const std::function<bool()>& condition, std::chrono::milliseconds timeout)
{
    std::unique_lock lock(m_mutex);
    return m_changed.wait_for(lock, timeout, condition);
}

// On the remote thread: wakes waiting commands, nothing else. Notices are
// printed from the session's list (see SessionWatcher).
void RemoteWatcher::OnChanged()
{
    {
        std::lock_guard lock(m_mutex);
    }
    m_changed.notify_all();
}

}  // namespace mango::cli
