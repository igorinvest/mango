#pragma once

#include <string>

// "Darwin 24.6.0", "Linux 6.8.0-45-generic" from uname(), or "unknown OS".
// The fallback of OsVersion() on POSIX systems (KernelVersion_Posix.cpp).
std::string KernelVersion();
