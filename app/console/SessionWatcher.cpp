#include "console/SessionWatcher.hpp"

#include "console/NoticeText.hpp"

namespace mango::cli
{

SessionWatcher::SessionWatcher(mango::Session& session, Output& events) : m_session(session), m_events(events)
{
    m_printedNoticeId = LastNoticeId();
    m_session.SetChangedHandler([this](mango::SessionParts parts) { OnChanged(parts); });
}

SessionWatcher::~SessionWatcher() { m_session.SetChangedHandler({}); }

bool SessionWatcher::WaitUntil(const std::function<bool()>& condition, std::chrono::milliseconds timeout)
{
    std::unique_lock lock(m_mutex);
    return m_changed.wait_for(lock, timeout, condition);
}

std::uint64_t SessionWatcher::LastNoticeId() const
{
    const auto notices = Read<mango::NoticeState>(mango::SessionPart::Notice).notices;
    return notices.empty() ? 0 : notices.back().noticeId;
}

std::optional<mango::SessionNotice> SessionWatcher::FindNotice(std::uint64_t afterId, std::string_view code) const
{
    for (const auto& notice : Read<mango::NoticeState>(mango::SessionPart::Notice).notices)
    {
        if (notice.noticeId > afterId && notice.code == code)
        {
            return notice;
        }
    }
    return std::nullopt;
}

// On the session thread: prints and wakes waiting commands, nothing else.
void SessionWatcher::OnChanged(mango::SessionParts parts)
{
    if (parts.Has(mango::SessionPart::Notice))
    {
        PrintNewNotices();
    }

    {
        std::lock_guard lock(m_mutex);
    }
    m_changed.notify_all();
}

// Every notice once, in order, as one line.
void SessionWatcher::PrintNewNotices()
{
    for (const auto& notice : Read<mango::NoticeState>(mango::SessionPart::Notice).notices)
    {
        if (notice.noticeId > m_printedNoticeId)
        {
            m_printedNoticeId = notice.noticeId;
            m_events.Write("\n" + NoticeLine(notice) + "\n");
        }
    }
}

}  // namespace mango::cli
