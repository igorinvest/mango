#include "ExecutablePath.hpp"

#include <mach-o/dyld.h>

#include <cstdint>

std::filesystem::path ExecutableDirectory()
{
    char buffer[1024];
    uint32_t size = sizeof(buffer);

    if (_NSGetExecutablePath(buffer, &size) == 0)
    {
        std::error_code ec;
        auto path = std::filesystem::canonical(buffer, ec);
        if (!ec)
        {
            return path.parent_path();
        }
    }

    return std::filesystem::current_path();
}
