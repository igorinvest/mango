#pragma once

#include <expected>
#include <filesystem>
#include <string>

namespace mango::cli
{

// Linux: makes sure only one Mango runs the engine on a data folder, so two
// never write the library and settings.json over each other. An exclusive
// flock on a file in that folder, held while this object lives. The kernel
// releases it when the process ends, even on SIGKILL, so a crash leaves no
// stale lock behind.
class InstanceLock
{
   public:
    // The error says why: another Mango holds it, or the file couldn't be
    // opened.
    [[nodiscard]] static std::expected<InstanceLock, std::string> TryAcquire(const std::filesystem::path& file);

    InstanceLock(InstanceLock&& other) noexcept;
    InstanceLock& operator=(InstanceLock&& other) noexcept;
    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;
    ~InstanceLock();

   private:
    explicit InstanceLock(int fd);

    int m_fd = -1;
};

}  // namespace mango::cli
