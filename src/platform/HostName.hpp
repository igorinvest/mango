#pragma once

#include <string>

// This computer's name, for the name paired Mango devices show it under:
// "Igors-MacBook-Pro" (without ".local"), "DESKTOP-1234". "Mango" when it
// can't be read. Backed by HostName_Posix.cpp / HostName_Windows.cpp.
std::string HostName();
