#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "LogLevel.hpp"

struct LoggerOptions
{
    // A file stops growing here, with a final "log truncated" line.
    std::size_t maxFileBytes = 20 * 1024 * 1024;

    // Info+ lines kept in memory and written at the top of the next file
    // opened (the "carry-over").
    std::size_t carryOverLines = 200;

    // Lines waiting for the writer thread. On overflow Debug lines are
    // dropped first, and a "N lines dropped" marker is written.
    std::size_t queueCapacity = 10'000;

    bool debugEnabled = false;
    bool mirrorToStderr = false;
};

// One line of the log: local time with milliseconds, level letter,
// category, message. Newlines in the message become spaces.
std::string FormatLogLine(std::chrono::system_clock::time_point time, LogLevel level,
    std::string_view category, std::string_view message);

char LogLevelLetter(LogLevel level);

// Writes log lines into at most one file at a time, from a background
// thread: callers only take a short lock to push. Open/Close go through the
// same queue, so every line lands in the file that was open when it was
// logged.
class Logger
{
   public:
    explicit Logger(LoggerOptions options = {});
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    [[nodiscard]] bool IsEnabled(LogLevel level) const;

    void Write(LogLevel level, std::string_view category, std::string_view message);

    // Closes the current file (if any) and opens path for appending. The
    // header lines come first, then the carry-over buffer.
    void Open(const std::filesystem::path& path, std::vector<std::string> headerLines);

    // Writes everything logged so far, then closes the file. Returns once
    // it's closed, so its folder can be deleted.
    void Close();

    // Returns once everything logged so far has been written.
    void Flush();

   private:
    enum class EntryKind
    {
        Line,
        Open,
        Close,
    };

    struct Entry
    {
        EntryKind kind = EntryKind::Line;
        LogLevel level = LogLevel::Info;
        std::string text;
        std::filesystem::path path;
        std::vector<std::string> lines;
    };

    // Queue side, under m_mutex.
    void Enqueue(Entry entry);
    bool MakeRoomLocked(LogLevel level);
    void RememberLocked(LogLevel level, const std::string& line);
    std::vector<std::string> TakeCarryOverLocked();

    // Writer side, on m_writer only.
    void WriterLoop();
    void WriteBatch(std::vector<Entry>& batch, std::size_t dropped);
    void Process(Entry& entry);
    void OpenFile(const Entry& entry);
    void CloseFile();
    void WriteLine(std::string_view line);
    void WriteToFile(std::string_view line);

    const LoggerOptions m_options;

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_idle;
    std::deque<Entry> m_queue;
    std::deque<std::string> m_carryOver;
    std::size_t m_queuedLines = 0;
    std::size_t m_dropped = 0;
    bool m_writing = false;
    bool m_stop = false;

    // Writer thread only.
    std::ofstream m_file;
    std::size_t m_fileBytes = 0;
    bool m_truncated = false;

    std::thread m_writer;
};
