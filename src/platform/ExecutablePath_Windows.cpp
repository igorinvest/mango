#include "ExecutablePath.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

std::filesystem::path ExecutableDirectory()
{
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);

    if (length == 0 || length == MAX_PATH)
    {
        return std::filesystem::current_path();
    }

    return std::filesystem::path(buffer).parent_path();
}
