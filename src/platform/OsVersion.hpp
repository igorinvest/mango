#pragma once

#include <string>

// The OS name and version for the log header, e.g. "macOS 15.7.1",
// "Windows 10.0.26100" or "Ubuntu 24.04.1 LTS (Linux 6.8.0-45-generic)".
// Backed by OsVersion_Apple.cpp, OsVersion_Linux.cpp and OsVersion_Windows.cpp.
std::string OsVersion();
