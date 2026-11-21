#include "InstanceLock.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <format>
#include <utility>

namespace mango::cli
{

std::expected<InstanceLock, std::string> InstanceLock::TryAcquire(const std::filesystem::path& file)
{
    // O_CLOEXEC: ffmpeg and the other child processes don't inherit it, so
    // an orphaned ffmpeg doesn't keep the lock.
    const int fd = open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0)
    {
        return std::unexpected(std::format("Couldn't open {}: {}", file.string(), std::strerror(errno)));
    }

    // flock locks belong to the open file, so a second open in this same
    // process conflicts too, as another process would.
    if (flock(fd, LOCK_EX | LOCK_NB) != 0)
    {
        const int error = errno;
        close(fd);
        if (error == EWOULDBLOCK)
        {
            return std::unexpected(std::string("Mango is already running (the service or another terminal)."));
        }
        return std::unexpected(std::format("Couldn't lock {}: {}", file.string(), std::strerror(error)));
    }
    return InstanceLock(fd);
}

InstanceLock::InstanceLock(int fd) : m_fd(fd) {}

InstanceLock::InstanceLock(InstanceLock&& other) noexcept : m_fd(std::exchange(other.m_fd, -1)) {}

InstanceLock& InstanceLock::operator=(InstanceLock&& other) noexcept
{
    if (this != &other)
    {
        if (m_fd >= 0)
        {
            close(m_fd);
        }
        m_fd = std::exchange(other.m_fd, -1);
    }
    return *this;
}

// Closing the file releases the lock.
InstanceLock::~InstanceLock()
{
    if (m_fd >= 0)
    {
        close(m_fd);
    }
}

}  // namespace mango::cli
