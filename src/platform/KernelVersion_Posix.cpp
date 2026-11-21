#include "KernelVersion.hpp"

#include <sys/utsname.h>

std::string KernelVersion()
{
    utsname name{};
    if (uname(&name) != 0)
    {
        return "unknown OS";
    }
    return std::string(name.sysname) + " " + name.release;
}
