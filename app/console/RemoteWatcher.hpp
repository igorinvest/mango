#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>

#include "remote/RemoteStreaming.hpp"

namespace mango::cli
{

// The console's side of remote streaming's change handler: lets a command
// wait for a state (the invite's link, a ping's answer). Its notices are
// printed with the session's (see SessionWatcher).
class RemoteWatcher
{
   public:
    explicit RemoteWatcher(mango::RemoteStreaming& remote);
    ~RemoteWatcher();

    RemoteWatcher(const RemoteWatcher&) = delete;
    RemoteWatcher& operator=(const RemoteWatcher&) = delete;

    [[nodiscard]] mango::RemoteStreaming& Remote() { return m_remote; }
    [[nodiscard]] mango::RemoteState State() const;

    // Waits until condition holds, re-checking at every change. Returns
    // whether it did before timeout.
    bool WaitUntil(const std::function<bool()>& condition, std::chrono::milliseconds timeout);

   private:
    void OnChanged();

    mango::RemoteStreaming& m_remote;
    std::mutex m_mutex;
    std::condition_variable m_changed;
};

}  // namespace mango::cli
