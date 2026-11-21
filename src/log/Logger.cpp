#include "Logger.hpp"

#include <algorithm>
#include <ctime>
#include <format>
#include <iostream>
#include <iterator>
#include <system_error>
#include <utility>

namespace
{

constexpr std::string_view kCarryOverStart = "--- before this stream ---";
constexpr std::string_view kCarryOverEnd = "--- this stream ---";

std::tm LocalTime(std::time_t time)
{
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    return local;
}

std::string SingleLine(std::string_view message)
{
    std::string line(message);
    std::ranges::replace(line, '\n', ' ');
    std::ranges::replace(line, '\r', ' ');
    return line;
}

std::string FormatNow(LogLevel level, std::string_view message)
{
    return FormatLogLine(std::chrono::system_clock::now(), level, LogCategory::Engine, message);
}

}  // namespace

char LogLevelLetter(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Error: return 'E';
        case LogLevel::Warning: return 'W';
        case LogLevel::Info: return 'I';
        case LogLevel::Debug: break;
    }
    return 'D';
}

std::string FormatLogLine(std::chrono::system_clock::time_point time, LogLevel level,
    std::string_view category, std::string_view message)
{
    const auto seconds = std::chrono::floor<std::chrono::seconds>(time);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(time - seconds).count();
    const auto local = LocalTime(std::chrono::system_clock::to_time_t(seconds));

    return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} {} [{}] {}", local.tm_year + 1900,
        local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, millis,
        LogLevelLetter(level), category, SingleLine(message));
}

Logger::Logger(LoggerOptions options) : m_options(options), m_writer([this] { WriterLoop(); }) {}

Logger::~Logger()
{
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_one();
    m_writer.join();
}

bool Logger::IsEnabled(LogLevel level) const
{
    return level != LogLevel::Debug || m_options.debugEnabled;
}

void Logger::Write(LogLevel level, std::string_view category, std::string_view message)
{
    if (!IsEnabled(level))
    {
        return;
    }

    auto line = FormatLogLine(std::chrono::system_clock::now(), level, category, message);

    std::lock_guard lock(m_mutex);
    RememberLocked(level, line);

    if (!MakeRoomLocked(level))
    {
        ++m_dropped;
        return;
    }

    Entry entry;
    entry.level = level;
    entry.text = std::move(line);
    m_queue.push_back(std::move(entry));
    ++m_queuedLines;
    m_wake.notify_one();
}

void Logger::Open(const std::filesystem::path& path, std::vector<std::string> headerLines)
{
    std::lock_guard lock(m_mutex);

    // Taken under the same lock that orders the queue: every line before
    // this point is in the carry-over, every line after it goes to the new
    // file.
    auto carryOver = TakeCarryOverLocked();
    std::ranges::move(carryOver, std::back_inserter(headerLines));

    Entry entry;
    entry.kind = EntryKind::Open;
    entry.path = path;
    entry.lines = std::move(headerLines);
    m_queue.push_back(std::move(entry));
    m_wake.notify_one();
}

void Logger::Close()
{
    Entry entry;
    entry.kind = EntryKind::Close;
    Enqueue(std::move(entry));
    Flush();
}

void Logger::Flush()
{
    std::unique_lock lock(m_mutex);
    m_wake.notify_one();
    m_idle.wait(lock, [this] { return m_queue.empty() && !m_writing; });
}

void Logger::Enqueue(Entry entry)
{
    {
        std::lock_guard lock(m_mutex);
        m_queue.push_back(std::move(entry));
    }
    m_wake.notify_one();
}

bool Logger::MakeRoomLocked(LogLevel level)
{
    if (m_queuedLines < m_options.queueCapacity)
    {
        return true;
    }

    if (level == LogLevel::Debug)
    {
        return false;
    }

    const auto debugLine = std::ranges::find_if(m_queue, [](const Entry& entry)
        { return entry.kind == EntryKind::Line && entry.level == LogLevel::Debug; });
    if (debugLine == m_queue.end())
    {
        return false;
    }

    m_queue.erase(debugLine);
    --m_queuedLines;
    ++m_dropped;
    return true;
}

void Logger::RememberLocked(LogLevel level, const std::string& line)
{
    if (level == LogLevel::Debug || m_options.carryOverLines == 0)
    {
        return;
    }

    if (m_carryOver.size() >= m_options.carryOverLines)
    {
        m_carryOver.pop_front();
    }
    m_carryOver.push_back(line);
}

std::vector<std::string> Logger::TakeCarryOverLocked()
{
    std::vector<std::string> lines;
    if (m_carryOver.empty())
    {
        return lines;
    }

    lines.emplace_back(kCarryOverStart);
    std::ranges::move(m_carryOver, std::back_inserter(lines));
    lines.emplace_back(kCarryOverEnd);
    m_carryOver.clear();
    return lines;
}

void Logger::WriterLoop()
{
    std::unique_lock lock(m_mutex);

    while (true)
    {
        m_wake.wait(lock, [this] { return m_stop || !m_queue.empty(); });
        if (m_queue.empty())
        {
            break;
        }

        std::vector<Entry> batch(std::make_move_iterator(m_queue.begin()), std::make_move_iterator(m_queue.end()));
        m_queue.clear();
        m_queuedLines = 0;
        const auto dropped = std::exchange(m_dropped, 0);
        m_writing = true;

        lock.unlock();
        WriteBatch(batch, dropped);
        lock.lock();

        m_writing = false;
        m_idle.notify_all();
    }

    CloseFile();
}

void Logger::WriteBatch(std::vector<Entry>& batch, std::size_t dropped)
{
    if (dropped > 0)
    {
        WriteLine(FormatNow(LogLevel::Warning, std::format("{} lines dropped", dropped)));
    }

    for (auto& entry : batch)
    {
        Process(entry);
    }

    // Flushed per batch rather than per line: the log is read while the
    // app runs, and batches grow by themselves under load.
    if (m_file.is_open())
    {
        m_file.flush();
    }
}

void Logger::Process(Entry& entry)
{
    switch (entry.kind)
    {
        case EntryKind::Line: WriteLine(entry.text); break;
        case EntryKind::Open: OpenFile(entry); break;
        case EntryKind::Close: CloseFile(); break;
    }
}

void Logger::OpenFile(const Entry& entry)
{
    CloseFile();

    std::error_code error;
    const auto existingSize = std::filesystem::file_size(entry.path, error);
    m_fileBytes = error ? 0 : static_cast<std::size_t>(existingSize);
    m_truncated = false;

    m_file.open(entry.path, std::ios::binary | std::ios::app);
    if (!m_file.is_open() && m_options.mirrorToStderr)
    {
        std::cerr << "[Log] can't open " << entry.path.string() << '\n';
    }

    for (const auto& line : entry.lines)
    {
        WriteToFile(line);
    }
}

void Logger::CloseFile()
{
    if (m_file.is_open())
    {
        m_file.close();
    }
    m_file.clear();
}

void Logger::WriteLine(std::string_view line)
{
    if (m_options.mirrorToStderr)
    {
        std::cerr << line << '\n';
    }
    WriteToFile(line);
}

void Logger::WriteToFile(std::string_view line)
{
    if (!m_file.is_open() || m_truncated)
    {
        return;
    }

    if (m_fileBytes + line.size() + 1 > m_options.maxFileBytes)
    {
        m_file << FormatNow(LogLevel::Warning, "log truncated") << '\n';
        m_truncated = true;
        return;
    }

    m_file << line << '\n';
    m_fileBytes += line.size() + 1;
}
