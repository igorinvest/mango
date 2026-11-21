#include "ToolOutput.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>

#include "../../../file/File.hpp"
#include "../../../platform/ChildProcess.hpp"

std::optional<std::string> ToolOutput::Run(
    const std::filesystem::path& executable, const std::vector<std::string>& arguments)
{
    const auto outputPath = TemporaryPath();

    ChildProcess::RedirectOptions redirect;
    redirect.stdoutPath = outputPath;

    std::optional<std::string> output;
    if (auto process = ChildProcess::Start(executable, arguments, redirect); process && process->Wait())
    {
        output = ReadAll(outputPath);
    }

    std::error_code ec;
    std::filesystem::remove(outputPath, ec);
    return output;
}

std::filesystem::path ToolOutput::TemporaryPath()
{
    static std::atomic<unsigned> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();

    std::error_code ec;
    const auto folder = std::filesystem::temp_directory_path(ec);
    return (ec ? File::getApplicationDataPath() : folder) /
        ("mango-tool-" + std::to_string(stamp) + "-" + std::to_string(counter++) + ".txt");
}

std::string ToolOutput::ReadAll(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
