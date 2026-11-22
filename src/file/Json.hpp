#pragma once

#include <filesystem>
#include <fstream>
#include <glaze/glaze.hpp>
#include <optional>
#include <print>
#include <string>

#include "Serialization.hpp"  // IWYU pragma: keep

class Json
{
   public:
    // Opts lets a caller write, for example, a prettified file.
    template <auto Opts = glz::opts{}, typename T>
    static bool SaveJsonToFile(const std::filesystem::path &path, const T &data);

    template <typename T>
    static std::optional<T> ReadJsonFromFile(const std::filesystem::path &path);

    // Same as ReadJsonFromFile, but ignores JSON object keys that aren't
    // declared on T instead of erroring (Glaze's default is strict). Use this
    // for externally-produced JSON we only partially model, e.g. ffprobe's
    // report, which includes many fields no caller here cares about.
    template <typename T>
    static std::optional<T> ReadJsonFromFileLenient(const std::filesystem::path &path);

    template <typename T>
    [[nodiscard]] static std::expected<std::string, glz::error_ctx> WriteJson(
        const T &data) noexcept
    {
        return glz::write_json(data);  // Glaze сам выделит строку и вернет её
    }

    template <typename T>
    [[nodiscard]] static std::expected<T, glz::error_ctx> ReadJson(
        const std::string &buffer) noexcept
    {
        return glz::read_json<T>(buffer);  // Glaze сам создаст и вернет объект T
    }


    

    // Исправлено: Возвращает красивую JSON-строку с отступами через шаблонные опции Glaze
    template <typename T>
    [[nodiscard]] static std::expected<std::string, glz::error_ctx> WriteJsonPrettified(
        const T &data) noexcept
    {
        return glz::write<glz::opts{.prettify = true}>(data);
    }

    // Красиво печатает любой объект/структуру прямо в стандартный вывод
    template <typename T>
    static void Print(const T &data) noexcept
    {
        try
        {
            auto result = glz::write<glz::opts{.prettify = true}>(data);
            if (result)
            {
                std::println("{}", *result);
            }
            else
            {
                // Исправлено: используем glz::format_error для получения понятной строки с ошибкой
                std::string error_msg = glz::format_error(result.error());
                std::println(stderr, "Json::Print Error: {}", error_msg);
            }
        }
        catch (...)
        {
            // std::println throws if the underlying stream write fails (e.g. no
            // writable stdout when running as a sandboxed GUI app). This is a
            // debug aid, not a functional requirement, and it's declared
            // noexcept — so a write failure must not crash the whole process.
        }
    }
};

template <auto Opts, typename T>
bool Json::SaveJsonToFile(const std::filesystem::path &path, const T &data)
{
    std::string buffer;

    auto error = glz::write<Opts>(data, buffer);

    if (error)
    {
        return false;
    }

    // Written next to the target and renamed over it, so a crash or power
    // loss mid-write leaves the previous file intact instead of a truncated
    // one. rename replaces an existing target on both platforms (MSVC's
    // std::filesystem::rename uses MOVEFILE_REPLACE_EXISTING).
    auto tempPath = path;
    tempPath += ".tmp";

    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);

        if (!file)
        {
            return false;
        }

        file << buffer;
        file.close();

        if (!file)
        {
            std::error_code ignored;
            std::filesystem::remove(tempPath, ignored);
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::rename(tempPath, path, ec);

    if (ec)
    {
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    return true;
}

template <typename T>
std::optional<T> Json::ReadJsonFromFile(const std::filesystem::path &path)
{
    std::ifstream file(path);

    if (!file)
    {
        return std::nullopt;
    }

    std::string buffer(std::istreambuf_iterator<char>(file), {});

    T value;

    auto error = glz::read_json(value, buffer);

    if (error)
    {
        return std::nullopt;
    }

    return value;
}

template <typename T>
std::optional<T> Json::ReadJsonFromFileLenient(const std::filesystem::path &path)
{
    std::ifstream file(path);

    if (!file)
    {
        return std::nullopt;
    }

    std::string buffer(std::istreambuf_iterator<char>(file), {});

    T value;

    constexpr glz::opts lenientOpts{.error_on_unknown_keys = false};
    auto error = glz::read<lenientOpts>(value, buffer);

    if (error)
    {
        return std::nullopt;
    }

    return value;
}
