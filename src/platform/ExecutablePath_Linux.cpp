#include "ExecutablePath.hpp"

std::filesystem::path ExecutableDirectory()
{
    // The kernel's link to the running binary, with symlinks (a
    // /usr/bin/mango pointing into /opt/mango) already resolved.
    std::error_code ec;
    auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec)
    {
        return path.parent_path();
    }

    return std::filesystem::current_path();
}
