#include "File.hpp"
#include "../configuration/CallbackRegister.hpp"
#include "../log/Log.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <optional>

#include <blake3.h>

namespace fs = std::filesystem;

#ifdef _WIN32
namespace
{
// Wide-char lookup keeps non-ASCII user folders intact, and _wdupenv_s
// avoids the CRT deprecation warning on getenv.
fs::path GetEnvPath(const wchar_t* name)
{
    wchar_t* value = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&value, &length, name) != 0 || value == nullptr)
    {
        return {};
    }

    fs::path result(value);
    std::free(value);
    return result;
}
} // namespace
#endif

void File::OpenFileAsync(
    const std::string& title,
    const std::vector<std::string>& filters,
    Callback callback)
{
    const auto& handler = CallbackRegister::GetInstance().GetOpenFileHandler();

    if (!handler)
    {
        Log::Warning(LogCategory::Library, "open file: no native picker registered, nothing to show");
        callback("", "");
        return;
    }

    Log::Debug(LogCategory::Library, "open file: using the native picker");
    handler(title, filters, std::move(callback));
}

void File::OpenFolderAsync(const std::string& title, Callback callback)
{
    const auto& handler = CallbackRegister::GetInstance().GetOpenFolderHandler();

    if (!handler)
    {
        Log::Warning(LogCategory::Library, "open folder: no native picker registered, nothing to show");
        callback("", "");
        return;
    }

    handler(title, std::move(callback));
}

std::optional<std::string> File::Hash(const std::filesystem::path& filePath)
{
    std::ifstream file(filePath, std::ios::binary);

    if (!file)
        return std::nullopt;

    blake3_hasher hasher;
    blake3_hasher_init(&hasher);

    constexpr size_t BufferSize = 1024 * 1024;

    std::vector<char> buffer(BufferSize);

    while (file)
    {
        file.read(buffer.data(), buffer.size());

        std::streamsize bytesRead = file.gcount();

        if (bytesRead > 0)
        {
            blake3_hasher_update(
                &hasher,
                buffer.data(),
                static_cast<size_t>(bytesRead));
        }
    }

    uint8_t hash[BLAKE3_OUT_LEN];

    blake3_hasher_finalize(
        &hasher,
        hash,
        BLAKE3_OUT_LEN);

    std::ostringstream result;

    result << std::hex << std::setfill('0');

    for (uint8_t byte : hash)
    {
        result << std::setw(2)
               << static_cast<unsigned>(byte);
    }

    return result.str();
}

std::optional<std::string> File::FastHash(const std::filesystem::path& filePath)
{
    std::ifstream file(
        filePath,
        std::ios::binary | std::ios::ate);

    if (!file)
        return std::nullopt;

    std::streamsize fileSize = file.tellg();

    if (fileSize < 0)
        return std::nullopt;

    constexpr size_t ChunkSize = 8 * 1024 * 1024; // 8 MB

    std::vector<char> buffer(ChunkSize);

    blake3_hasher hasher;
    blake3_hasher_init(&hasher);

    // Include file size in hash
    blake3_hasher_update(
        &hasher,
        &fileSize,
        sizeof(fileSize));

    // Read beginning
    file.seekg(0);

    file.read(
        buffer.data(),
        buffer.size());

    auto bytesRead = file.gcount();

    blake3_hasher_update(
        &hasher,
        buffer.data(),
        static_cast<size_t>(bytesRead));

    // Read end
    if (fileSize > static_cast<std::streamsize>(ChunkSize))
    {
        file.seekg(
            fileSize - ChunkSize);
    }

    file.read(
        buffer.data(),
        buffer.size());

    bytesRead = file.gcount();

    blake3_hasher_update(
        &hasher,
        buffer.data(),
        static_cast<size_t>(bytesRead));

    // Finalize
    uint8_t digest[BLAKE3_OUT_LEN];

    blake3_hasher_finalize(
        &hasher,
        digest,
        BLAKE3_OUT_LEN);

    std::ostringstream result;

    result << std::hex << std::setfill('0');

    for (auto byte : digest)
    {
        result << std::setw(2)
               << static_cast<int>(byte);
    }

    return result.str();
}

std::optional<std::string> File::FileSize(const std::filesystem::path& filePath)
{
    std::error_code ec;
    // 2. Получаем размер файла через std::filesystem
    auto size = std::filesystem::file_size(filePath, ec);

    // 3. Если возникла ошибка (файл не найден, нет прав доступа), возвращаем empty optional
    if (ec)
    {
        return std::nullopt;
    }

    // 4. Переводим число (байты) в строку std::string
    return std::to_string(size);
}


// std::vector<char> — стандартный контейнер для бинарных данных
std::optional<std::vector<char>> File::ReadChunk(
    const std::filesystem::path& filePath, std::streamsize offset, std::streamsize chunkSize)
{
    // Открываем файл в бинарном режиме (ios::binary) и перемещаем указатель в конец (ios::ate),
    // чтобы сразу узнать реальный размер файла и проверить границы.
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);

    if (!file.is_open())
    {
        return std::nullopt; // Файл не удалось открыть
    }

    std::streamsize fileSize = file.tellg();

    // Проверяем, не выходит ли запрос за границы файла
    if (offset < 0 || offset >= fileSize)
    {
        return std::nullopt;
    }

    // Если запрошенный чанк больше, чем осталось до конца файла,
    // уменьшаем размер чанка, чтобы прочитать только доступные байты
    if (offset + chunkSize > fileSize)
    {
        chunkSize = fileSize - offset;
    }

    // Перемещаем указатель чтения на нужную позицию от начала файла
    file.seekg(offset, std::ios::beg);

    // Выделяем память под буфер
    std::vector<char> buffer(chunkSize);

    // Читаем бинарные данные напрямую в буфер
    if (file.read(buffer.data(), chunkSize))
    {
        return buffer; // Успешно прочитали
    }

    return std::nullopt; // Ошибка чтения
}

const std::filesystem::path& File::getApplicationDataPath()
{
    static const std::filesystem::path working_dir = calculateApplicationDataPath();
    return working_dir;
}

fs::path File::calculateApplicationDataPath()
{
    fs::path path;

#ifdef _WIN32

    if (const fs::path appData = GetEnvPath(L"APPDATA"); !appData.empty())
    {
        path = appData / kApplicationName;
    }

#elif defined(__APPLE__)

    if (const char *home = std::getenv("HOME"))
    {
        path = fs::path(home) / "Library" / "Application Support" / kApplicationName;
    }

#else

    if (const char *xdg = std::getenv("XDG_DATA_HOME"))
    {
        path = fs::path(xdg) / kApplicationName;
    }
    else if (const char *home = std::getenv("HOME"))
    {
        path = fs::path(home) / ".local" / "share" / kApplicationName;
    }

#endif

    if (!path.empty())
    {
        fs::create_directories(path);
    }

    return path;
}

std::filesystem::path File::FromUtf8(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

// Converted via the two clocks' current offset rather than clock_cast /
// file_clock::to_sys, which libc++ and MSVC's STL don't both provide.
std::optional<std::int64_t> File::LastWriteUnixSeconds(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto fileTime = std::filesystem::last_write_time(path, ec);
    if (ec)
    {
        return std::nullopt;
    }

    const auto systemTime = std::chrono::system_clock::now() +
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            fileTime - std::filesystem::file_time_type::clock::now());
    return std::chrono::duration_cast<std::chrono::seconds>(systemTime.time_since_epoch()).count();
}

bool File::Exists(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
}

std::string File::ToUtf8(const std::filesystem::path& path)
{
    const auto utf8 = path.u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

std::optional<std::filesystem::path> File::CreateDirectoryPath(
    const std::filesystem::path& folderPath)
{
    if (folderPath.empty())
    {
        return std::nullopt;
    }

    std::error_code ec;
    std::filesystem::create_directories(folderPath, ec);

    if (ec)
    {
        return std::nullopt;
    }

    return folderPath;
}

bool File::RemoveDirectoryPath(const std::filesystem::path& folderPath)
{
    if (folderPath.empty())
    {
        return false;
    }

    std::error_code ec;
    std::filesystem::remove_all(folderPath, ec);

    return !ec;
}
