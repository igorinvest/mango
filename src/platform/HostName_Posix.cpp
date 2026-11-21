#include "HostName.hpp"

#include <unistd.h>

#include <array>

std::string HostName()
{
    std::array<char, 256> buffer{};
    if (gethostname(buffer.data(), buffer.size() - 1) != 0 || buffer[0] == '\0')
    {
        return "Mango";
    }

    std::string name(buffer.data());
    if (const auto dot = name.find('.'); dot != std::string::npos && dot > 0)
    {
        name.resize(dot);
    }
    return name;
}
