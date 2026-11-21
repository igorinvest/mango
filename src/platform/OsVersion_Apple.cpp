#include "OsVersion.hpp"

#include <sys/sysctl.h>

#include "KernelVersion.hpp"

namespace
{

std::string MacProductVersion()
{
    char version[64] = {};
    size_t size = sizeof(version);
    if (sysctlbyname("kern.osproductversion", version, &size, nullptr, 0) != 0)
    {
        return {};
    }
    return version;
}

}  // namespace

std::string OsVersion()
{
    if (const auto version = MacProductVersion(); !version.empty())
    {
        return "macOS " + version;
    }
    return KernelVersion();
}
