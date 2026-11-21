#include "OsVersion.hpp"

#include <fstream>
#include <string_view>

#include "KernelVersion.hpp"

namespace
{

// A value of os-release(5): KEY="value", KEY='value' or KEY=value.
std::string Unquote(std::string_view value)
{
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
    {
        value = value.substr(1, value.size() - 2);
    }
    return std::string(value);
}

// PRETTY_NAME from /etc/os-release (or /usr/lib/os-release), e.g.
// "Ubuntu 24.04.1 LTS". Empty when neither file has it.
std::string DistributionName()
{
    constexpr std::string_view kKey = "PRETTY_NAME=";
    for (const auto* path : {"/etc/os-release", "/usr/lib/os-release"})
    {
        std::ifstream file(path);
        std::string line;
        while (std::getline(file, line))
        {
            if (line.starts_with(kKey))
            {
                return Unquote(std::string_view(line).substr(kKey.size()));
            }
        }
    }
    return {};
}

}  // namespace

std::string OsVersion()
{
    const auto kernel = KernelVersion();
    if (const auto distribution = DistributionName(); !distribution.empty())
    {
        return distribution + " (" + kernel + ")";
    }
    return kernel;
}
