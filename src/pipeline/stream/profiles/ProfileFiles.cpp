#include "ProfileFiles.hpp"

#include <mutex>
#include <utility>
#include <vector>

#include "../../../log/Log.hpp"

namespace ProfileFiles
{

namespace
{

using Reset = std::pair<std::filesystem::path, std::filesystem::path>;

struct QuarantineNotes
{
    std::mutex mutex;
    QuarantineListener listener;
    std::vector<Reset> unreported;
};

QuarantineNotes& Notes()
{
    static QuarantineNotes notes;
    return notes;
}

void Report(const std::filesystem::path& file, const std::filesystem::path& backup)
{
    auto& notes = Notes();
    QuarantineListener listener;
    {
        std::lock_guard lock(notes.mutex);
        if (!notes.listener)
        {
            notes.unreported.emplace_back(file, backup);
            return;
        }
        listener = notes.listener;
    }
    listener(file, backup);
}

}  // namespace

void SetQuarantineListener(QuarantineListener listener)
{
    auto& notes = Notes();
    std::vector<Reset> unreported;
    {
        std::lock_guard lock(notes.mutex);
        notes.listener = listener;
        if (listener)
        {
            unreported = std::exchange(notes.unreported, {});
        }
    }
    for (const auto& [file, backup] : unreported)
    {
        listener(file, backup);
    }
}

void Quarantine(const std::filesystem::path& path)
{
    auto invalidPath = path;
    invalidPath += ".invalid";

    std::error_code ec;
    std::filesystem::remove(invalidPath, ec);
    std::filesystem::rename(path, invalidPath, ec);

    Log::Warning(LogCategory::Settings, "{} is invalid; kept as {} and replaced with the defaults",
        File::ToUtf8(path), File::ToUtf8(invalidPath));
    Report(path, invalidPath);
}

void CreateParent(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
}

void LogWriteFailure(const std::filesystem::path& path)
{
    Log::Error(LogCategory::Settings, "failed to write {}", File::ToUtf8(path));
}

}  // namespace ProfileFiles
