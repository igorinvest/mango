#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <ios>
#include <optional>
#include <string>
#include <vector>

class File
{
public:
    // securityBookmark is a base64-encoded macOS security-scoped bookmark
    // for filePath (empty when not applicable, e.g. cancelled or non-macOS).
    using Callback = std::function<void(const std::string& filePath, const std::string& securityBookmark)>;

    // Shows the native picker registered in CallbackRegister. With none
    // registered, logs a warning and calls back with an empty path, the
    // same as a cancelled dialog.
    static void OpenFileAsync(
        const std::string& title,
        const std::vector<std::string>& filters,
        Callback callback);

    // Folder counterpart of OpenFileAsync — callback gets the chosen
    // folder's path (empty if cancelled) and its security-scoped bookmark.
    static void OpenFolderAsync(const std::string& title, Callback callback);

    static std::optional<std::string> Hash(const std::filesystem::path& filePath);

    static std::optional<std::string> FastHash(const std::filesystem::path& filePath);

    static std::optional<std::string> FileSize(const std::filesystem::path& filePath);

    static std::optional<std::vector<char>> ReadChunk(
        const std::filesystem::path& filePath, std::streamsize offset, std::streamsize chunkSize);

    //static const std::filesystem::path& getApplicationDataPath();
    [[nodiscard]] static const std::filesystem::path& getApplicationDataPath();

    static bool Exists(const std::filesystem::path& path);
    static std::string ToUtf8(const std::filesystem::path& path);

    // Inverse of ToUtf8.
    static std::filesystem::path FromUtf8(const std::string& utf8);

    // path's last modification time as Unix seconds; std::nullopt when it
    // can't be read.
    static std::optional<std::int64_t> LastWriteUnixSeconds(const std::filesystem::path& path);

    static std::optional<std::filesystem::path> CreateDirectoryPath(
        const std::filesystem::path& folderPath);
    static bool RemoveDirectoryPath(const std::filesystem::path& folderPath);

   private:
    static constexpr const char *kApplicationName = "Mango";

    static std::filesystem::path calculateApplicationDataPath();
};
