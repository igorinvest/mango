#pragma once

#include <chrono>
#include <cstddef>
#include <map>
#include <string>
#include <string_view>

#include "Notices.hpp"
#include "SessionState.hpp"

namespace mango
{

// The session's "notice" part: the last few notices, newest last, with
// repeats collapsed by the catalog's rule (see Notices.hpp). Not
// thread-safe; the session uses it on its own thread.
class NoticeList
{
   public:
    using Clock = std::chrono::steady_clock;

    explicit NoticeList(Clock::duration repeatWindow, std::size_t capacity = 10);

    // The added notice, or nullptr when code isn't in the catalog or it's a
    // repeat.
    const SessionNotice* Add(
        std::string_view code, NoticeParams params, std::string fastHash, Clock::time_point now);

    [[nodiscard]] const NoticeState& State() const { return m_state; }

   private:
    [[nodiscard]] static std::string RepeatKey(std::string_view code, const NoticeParams& params,
        const std::string& fastHash);
    [[nodiscard]] bool IsRepeat(const NoticeKind& kind, const std::string& key, Clock::time_point now) const;
    void Remember(const NoticeKind& kind, std::string key, Clock::time_point now);
    void ForgetExpired(Clock::time_point now);
    void Append(SessionNotice notice);

    const Clock::duration m_repeatWindow;
    const std::size_t m_capacity;

    NoticeState m_state;
    std::uint64_t m_lastId = 0;

    // When each notice was last shown, by RepeatKey. Once-per-launch ones
    // stay; the others go once their window is over.
    std::map<std::string, Clock::time_point> m_windowed;
    std::map<std::string, Clock::time_point> m_once;
};

// The current time as "2026-11-19T21:04:11Z".
[[nodiscard]] std::string UtcTimestamp();

}  // namespace mango
