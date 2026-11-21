#pragma once

#include <optional>
#include <string>
#include <vector>

#include <glaze/glaze.hpp>

namespace mango::cli
{

// Splits a console line into words. "double" or 'single' quotes keep spaces
// inside one word; outside Windows a backslash escapes the next character,
// so a folder dragged into a macOS Terminal ("My\ Movies") works as is.
[[nodiscard]] std::vector<std::string> Tokenize(const std::string& line);

// "1:02:03", "62:03" or "3723" → seconds. nullopt if it isn't a time.
[[nodiscard]] std::optional<double> ParseTime(const std::string& text);

// Seconds → "1:02:03" (or "2:03" under an hour).
[[nodiscard]] std::string FormatTime(double seconds);

[[nodiscard]] std::optional<int> ParseInt(const std::string& text);

// "on"/"off", "true"/"false", "yes"/"no", "1"/"0". nullopt for anything else.
[[nodiscard]] std::optional<bool> ParseBool(const std::string& text);

// For key=value options: the value of the first "key=..." in args, if any.
[[nodiscard]] std::optional<std::string> FindOption(
    const std::vector<std::string>& args, const std::string& key);

// "~/Movies" → "/Users/me/Movies", then made absolute. Returned as UTF-8,
// which is what the engine expects for paths.
[[nodiscard]] std::string ResolveUserPath(const std::string& text);

// Engine JSON results → its public structs (Engine.hpp). Unknown keys are
// ignored, so an engine that grows a field doesn't break the console.
template <typename T>
[[nodiscard]] std::optional<T> ParseJson(const std::string& json)
{
    T value{};
    if (glz::read<glz::opts{.error_on_unknown_keys = false}>(value, json))
    {
        return std::nullopt;
    }
    return value;
}

}  // namespace mango::cli
