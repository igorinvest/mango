#include "NoticeList.hpp"

#include <format>

namespace mango
{

NoticeList::NoticeList(Clock::duration repeatWindow, std::size_t capacity)
    : m_repeatWindow(repeatWindow), m_capacity(capacity)
{
}

const SessionNotice* NoticeList::Add(
    std::string_view code, NoticeParams params, std::string fastHash, Clock::time_point now)
{
    const auto* kind = Notices::Find(code);
    if (kind == nullptr)
    {
        return nullptr;
    }

    ForgetExpired(now);
    auto key = RepeatKey(code, params, fastHash);
    if (IsRepeat(*kind, key, now))
    {
        return nullptr;
    }
    Remember(*kind, std::move(key), now);

    Append(SessionNotice{
        .noticeId = ++m_lastId,
        .code = std::string(code),
        .severity = std::string(Notices::ToString(kind->severity)),
        .params = std::move(params),
        .fastHash = std::move(fastHash),
        .time = UtcTimestamp(),
        .systemNotification = kind->systemNotification,
    });
    return &m_state.notices.back();
}

std::string NoticeList::RepeatKey(std::string_view code, const NoticeParams& params, const std::string& fastHash)
{
    auto key = std::format("{}|{}", code, fastHash);
    for (const auto& [name, value] : params)
    {
        key += std::format("|{}={}", name, value);
    }
    return key;
}

bool NoticeList::IsRepeat(const NoticeKind& kind, const std::string& key, Clock::time_point now) const
{
    if (kind.repeat == NoticeRepeat::OncePerLaunch)
    {
        return m_once.contains(key);
    }
    const auto it = m_windowed.find(key);
    return it != m_windowed.end() && now - it->second < m_repeatWindow;
}

void NoticeList::Remember(const NoticeKind& kind, std::string key, Clock::time_point now)
{
    auto& shown = kind.repeat == NoticeRepeat::OncePerLaunch ? m_once : m_windowed;
    shown[std::move(key)] = now;
}

void NoticeList::ForgetExpired(Clock::time_point now)
{
    std::erase_if(m_windowed, [&](const auto& item) { return now - item.second >= m_repeatWindow; });
}

void NoticeList::Append(SessionNotice notice)
{
    m_state.notices.push_back(std::move(notice));
    if (m_state.notices.size() > m_capacity)
    {
        m_state.notices.erase(m_state.notices.begin(),
            m_state.notices.begin() + static_cast<std::ptrdiff_t>(m_state.notices.size() - m_capacity));
    }
}

std::string UtcTimestamp()
{
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("{:%FT%TZ}", now);
}

}  // namespace mango
