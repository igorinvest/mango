#pragma once

#include <functional>
#include <mutex>

// One function told that something changed (see Library, PlaybackHistory,
// Settings — the session's way to learn of changes without polling). Notify
// may run with the owner's lock held, so the function must only note the
// change: never call back into the owner.
class ChangeListener
{
   public:
    using Handler = std::function<void()>;

    // An empty handler stops notifications.
    void Set(Handler handler);
    void Notify() const;

   private:
    mutable std::mutex m_mutex;
    Handler m_handler;
};
