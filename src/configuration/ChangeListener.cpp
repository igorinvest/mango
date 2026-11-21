#include "ChangeListener.hpp"

void ChangeListener::Set(Handler handler)
{
    std::lock_guard lock(m_mutex);
    m_handler = std::move(handler);
}

void ChangeListener::Notify() const
{
    Handler handler;
    {
        std::lock_guard lock(m_mutex);
        handler = m_handler;
    }
    if (handler)
    {
        handler();
    }
}
