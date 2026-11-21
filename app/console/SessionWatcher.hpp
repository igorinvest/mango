#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string_view>

#include "console/ConsoleText.hpp"
#include "console/Output.hpp"
#include "session/Session.hpp"

namespace mango::cli
{

// The console's side of the session's change handler: prints what happens
// on its own (a movie finished, the TV stopped answering) to events, and
// lets a command wait until the session reaches some state (discovery done,
// the stream on the TV). events is the terminal, or on Linux every attached
// client too (see ControlServer.hpp).
class SessionWatcher
{
   public:
    SessionWatcher(mango::Session& session, Output& events);
    ~SessionWatcher();

    SessionWatcher(const SessionWatcher&) = delete;
    SessionWatcher& operator=(const SessionWatcher&) = delete;

    [[nodiscard]] mango::Session& Session() { return m_session; }

    // A part's current snapshot.
    template <typename T>
    [[nodiscard]] T Read(mango::SessionPart part) const
    {
        return ParseJson<T>(m_session.GetPartJson(part)).value_or(T{});
    }

    // Waits until condition holds, re-checking at every change. Returns
    // whether it did before timeout.
    bool WaitUntil(const std::function<bool()>& condition, std::chrono::milliseconds timeout);

    // The latest notice's noticeId, so a command can tell a new one from it.
    [[nodiscard]] std::uint64_t LastNoticeId() const;

    // The first notice with code after noticeId afterId, if any.
    [[nodiscard]] std::optional<mango::SessionNotice> FindNotice(std::uint64_t afterId, std::string_view code) const;

   private:
    void OnChanged(mango::SessionParts parts);
    void PrintNewNotices();

    mango::Session& m_session;
    Output& m_events;
    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::uint64_t m_printedNoticeId = 0;
};

}  // namespace mango::cli
