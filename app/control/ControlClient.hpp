#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "console/Output.hpp"
#include "control/ControlMessages.hpp"
#include "control/ControlSocket.hpp"

namespace mango::cli
{

// Linux: `mango` in a terminal while another Mango runs the engine (the
// service, or another terminal). The same `mango>` prompt, but each line
// runs in that Mango (see ControlServer.hpp). No engine here, and nothing
// in the data folder is opened. `quit` and Ctrl+C only detach it.
class ControlClient
{
   public:
    // nullptr when no Mango listens on socketPath.
    [[nodiscard]] static std::unique_ptr<ControlClient> Connect(const std::filesystem::path& socketPath);

    explicit ControlClient(std::unique_ptr<LineSocket> socket);

    // The prompt, until `quit` or the end of input. Returns the exit code.
    // When the server goes away, says so and ends the process.
    int Run();

   private:
    bool ReadHello();
    void ReadReplies();
    void Handle(const ControlMessage& message);
    void SendAndWait(const std::string& line);
    [[noreturn]] void ServerGone();

    std::unique_ptr<LineSocket> m_socket;
    StdoutOutput m_out;
    std::mutex m_mutex;
    std::condition_variable m_done;
    std::uint64_t m_lastId = 0;
    std::uint64_t m_doneId = 0;
    std::atomic<bool> m_detaching = false;
};

// `quit` or `exit`: the words of a line, from Tokenize.
[[nodiscard]] bool IsQuitCommand(const std::vector<std::string>& words);

// A line as the server should get it. add-file and add-folder paths are
// made absolute here, since `~` and relative paths mean this terminal's
// home and folder, not the server's.
[[nodiscard]] std::string PrepareLineForServer(const std::string& line);

}  // namespace mango::cli
