#include "OsVersion.hpp"

#include <format>

#include <windows.h>

namespace
{

// GetVersionEx lies to apps without a compatibility manifest; ntdll's
// RtlGetVersion doesn't.
using RtlGetVersionFunction = LONG(WINAPI*)(OSVERSIONINFOW*);

}  // namespace

std::string OsVersion()
{
    const auto ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto rtlGetVersion = ntdll
        ? reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(ntdll, "RtlGetVersion"))
        : nullptr;

    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (!rtlGetVersion || rtlGetVersion(&info) != 0)
    {
        return "Windows";
    }

    return std::format("Windows {}.{}.{}", info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber);
}
