#include "console/ConsoleText.hpp"

#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <ranges>

namespace fs = std::filesystem;

namespace mango::cli
{

namespace
{

#ifdef _WIN32
constexpr bool kBackslashEscapes = false;
#else
constexpr bool kBackslashEscapes = true;
#endif

std::string GetEnv(const char* name)
{
#ifdef _WIN32
    // _dupenv_s avoids the CRT deprecation warning on getenv.
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
    {
        return {};
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
#endif
}

std::string HomeDirectory()
{
    std::string home = GetEnv("HOME");
#ifdef _WIN32
    if (home.empty())
    {
        home = GetEnv("USERPROFILE");
    }
#endif
    return home;
}

std::string ExpandHome(const std::string& text)
{
    if (text == "~" || text.starts_with("~/"))
    {
        return HomeDirectory() + text.substr(1);
    }
    return text;
}

std::string ToUtf8(const fs::path& path)
{
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

// Non-negative decimal number. strtod rather than from_chars: the double
// overload needs macOS 26.
std::optional<double> ParseNumber(const std::string& text)
{
    if (text.empty() || text.front() == '-' || text.front() == '+' || std::isspace(text.front()))
    {
        return std::nullopt;
    }
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return end == text.c_str() + text.size() ? std::optional(value) : std::nullopt;
}

}  // namespace

std::vector<std::string> Tokenize(const std::string& line)
{
    std::vector<std::string> words;
    std::string current;
    bool inWord = false;
    char quote = 0;

    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const char c = line[i];
        if (kBackslashEscapes && c == '\\' && quote != '\'' && i + 1 < line.size())
        {
            current += line[++i];
            inWord = true;
        }
        else if (quote != 0 && c == quote)
        {
            quote = 0;
        }
        else if (quote != 0)
        {
            current += c;
        }
        else if (c == '"' || c == '\'')
        {
            quote = c;
            inWord = true;
        }
        else if (c == ' ' || c == '\t')
        {
            if (inWord)
            {
                words.push_back(std::move(current));
                current.clear();
                inWord = false;
            }
        }
        else
        {
            current += c;
            inWord = true;
        }
    }

    if (inWord)
    {
        words.push_back(std::move(current));
    }
    return words;
}

std::optional<double> ParseTime(const std::string& text)
{
    double total = 0.0;
    int parts = 0;

    for (const auto part : std::views::split(text, ':'))
    {
        const auto value = ParseNumber(std::string(part.begin(), part.end()));
        if (!value)
        {
            return std::nullopt;
        }
        total = total * 60.0 + *value;
        ++parts;
    }

    return parts >= 1 && parts <= 3 ? std::optional(total) : std::nullopt;
}

std::string FormatTime(double seconds)
{
    const auto total = static_cast<long long>(std::max(0.0, std::round(seconds)));
    const auto h = total / 3600;
    const auto m = (total / 60) % 60;
    const auto s = total % 60;
    return h > 0 ? std::format("{}:{:02}:{:02}", h, m, s) : std::format("{}:{:02}", m, s);
}

std::optional<int> ParseInt(const std::string& text)
{
    int value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || ec != std::errc() || end != text.data() + text.size())
    {
        return std::nullopt;
    }
    return value;
}

std::optional<bool> ParseBool(const std::string& text)
{
    if (text == "on" || text == "true" || text == "yes" || text == "1")
    {
        return true;
    }
    if (text == "off" || text == "false" || text == "no" || text == "0")
    {
        return false;
    }
    return std::nullopt;
}

std::optional<std::string> FindOption(
    const std::vector<std::string>& args, const std::string& key)
{
    const std::string prefix = key + "=";
    for (const auto& arg : args)
    {
        if (arg.starts_with(prefix))
        {
            return arg.substr(prefix.size());
        }
    }
    return std::nullopt;
}

std::string ResolveUserPath(const std::string& text)
{
    const auto expanded = ExpandHome(text);
    const fs::path path(std::u8string(expanded.begin(), expanded.end()));

    std::error_code ec;
    const auto absolute = fs::absolute(path, ec);
    return ToUtf8(ec ? path : absolute);
}

}  // namespace mango::cli
