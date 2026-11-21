#pragma once

#include <functional>
#include <string>
#include <vector>

// Shared by the test files (see MangoTests.cpp's main).
void Check(bool condition, const std::string& what);

struct TestCase
{
    const char* name;
    std::function<void()> run;
};

// tests/SessionTests.cpp
std::vector<TestCase> SessionTestCases();
// tests/RemoteTests.cpp
std::vector<TestCase> RemoteTestCases();
// tests/ForwardingTests.cpp
std::vector<TestCase> ForwardingTestCases();
// tests/TrackSelectionTests.cpp
std::vector<TestCase> TrackSelectionTestCases();
// tests/FFmpegInstallTests.cpp
std::vector<TestCase> FFmpegInstallTestCases();
// tests/EncoderOptionTests.cpp
std::vector<TestCase> EncoderOptionTestCases();
#if defined(__linux__)
// tests/ControlTests_Linux.cpp
std::vector<TestCase> ControlTestCases();
#endif
