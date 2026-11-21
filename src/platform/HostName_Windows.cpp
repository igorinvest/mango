#include "HostName.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

std::string HostName()
{
    wchar_t buffer[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (!GetComputerNameW(buffer, &size) || size == 0)
    {
        return "Mango";
    }

    const int length = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(size), nullptr, 0, nullptr, nullptr);
    std::string name(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(size), name.data(), length, nullptr, nullptr);
    return name;
}
