#include "ExecutablePath.hpp"

#include <string>

namespace
{
#if defined(_WIN32)
constexpr std::string_view kExecutableSuffix = ".exe";
#else
constexpr std::string_view kExecutableSuffix = "";
#endif
}  // namespace

std::string ExecutableFileName(std::string_view toolName)
{
    return std::string(toolName) + std::string(kExecutableSuffix);
}

std::filesystem::path ResolveBundledTool(std::string_view toolName)
{
    return ExecutableDirectory() / ExecutableFileName(toolName);
}
