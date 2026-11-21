#pragma once

#include <format>
#include <mutex>
#include <string_view>
#include <utility>

namespace mango::cli
{

// Where the console's text goes: the terminal, or (Linux) a client attached
// over the control socket (see ControlServer.hpp). Commands print through
// CommandContext::out() instead of stdout, so the same command works for
// both.
class Output
{
   public:
    virtual ~Output() = default;

    // Like std::println: the formatted text, then a newline.
    template <typename... Args>
    void Print(std::format_string<Args...> format, Args&&... args)
    {
        Write(std::format(format, std::forward<Args>(args)...) + "\n");
    }

    // The text as is, newlines included. Safe to call from any thread.
    virtual void Write(std::string_view text) = 0;
};

// The process's own terminal.
class StdoutOutput final : public Output
{
   public:
    void Write(std::string_view text) override;

   private:
    std::mutex m_mutex;
};

}  // namespace mango::cli
