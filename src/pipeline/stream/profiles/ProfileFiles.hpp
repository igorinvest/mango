#pragma once

#include <filesystem>
#include <functional>
#include <optional>

#include "../../../file/File.hpp"
#include "../../../file/Json.hpp"

// Read and write rules shared by the user-editable JSON files in the app
// data folder (stream-profiles.json, encoding-defaults.json,
// device-profiles.json): a file that exists but can't be used is kept as
// "<name>.invalid", so the user's edits survive, and the caller falls back
// to its defaults.
namespace ProfileFiles
{

// Moves path to "<name>.invalid", replacing any earlier one, and tells the
// quarantine listener.
void Quarantine(const std::filesystem::path& path);

// Told about every quarantined file (file, then its backup), on the thread
// that read it, without a lock held. Files quarantined before a listener is
// set are kept and told to the next one, so a reset at engine start isn't
// lost. An empty function stops it.
using QuarantineListener =
    std::function<void(const std::filesystem::path& file, const std::filesystem::path& backup)>;
void SetQuarantineListener(QuarantineListener listener);

void CreateParent(const std::filesystem::path& path);
void LogWriteFailure(const std::filesystem::path& path);

// The file's contents when it parses and passes isValid; otherwise
// std::nullopt, after quarantining a file that exists but is invalid.
template <typename T, typename IsValid>
std::optional<T> ReadValid(const std::filesystem::path& path, IsValid&& isValid)
{
    if (!File::Exists(path))
    {
        return std::nullopt;
    }

    if (auto value = Json::ReadJsonFromFile<T>(path); value && isValid(*value))
    {
        return value;
    }

    Quarantine(path);
    return std::nullopt;
}

// Opts lets a file meant for hand editing be written prettified.
template <auto Opts = glz::opts{}, typename T>
void Write(const std::filesystem::path& path, const T& value)
{
    CreateParent(path);
    if (!Json::SaveJsonToFile<Opts>(path, value))
    {
        LogWriteFailure(path);
    }
}

}  // namespace ProfileFiles
