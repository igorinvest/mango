#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "../../MediaInfo.hpp"

// Guesses the text encoding of a subtitle file and converts it to UTF-8.
// ffmpeg reads text subtitles as UTF-8, and older .srt files are often in a
// Windows code page instead. The bundled ffmpeg is built without iconv, so
// its -sub_charenc can't be used — files are converted here instead.
class SubtitleCharset
{
   public:
    // "" when the file is UTF-8 (with or without a BOM) or UTF-16 with a BOM
    // — ffmpeg reads those as they are. Otherwise "CP1251", "CP1250" or
    // "CP1252"; language (ISO 639) breaks the tie when the bytes alone
    // don't settle it.
    [[nodiscard]] static std::string Detect(
        const std::filesystem::path& path, const std::string& language);

    // Same, for content already in memory.
    [[nodiscard]] static std::string DetectFromBytes(
        std::string_view bytes, const std::string& language);

    // bytes, in charset ("CP1250"/"CP1251"/"CP1252"), as UTF-8. Bytes a
    // code page leaves undefined become U+FFFD.
    [[nodiscard]] static std::string ToUtf8(std::string_view bytes, const std::string& charset);

    // The file ffmpeg should read for subtitle: its own path when it's
    // UTF-8, otherwise a UTF-8 copy in cacheFolder, written on first use and
    // named after the file's size and mtime so an edited file gets a fresh
    // one. std::nullopt when the copy can't be written.
    [[nodiscard]] static std::optional<std::filesystem::path> PrepareFfmpegInput(
        const ExternalSubtitle& subtitle, const std::filesystem::path& cacheFolder);

   private:
    [[nodiscard]] static bool HasUnicodeBom(std::string_view bytes);
    [[nodiscard]] static bool IsValidUtf8(std::string_view bytes);

    // Whether the high bytes look like Cyrillic letters in CP1251 rather
    // than accented Latin letters in CP1252: in Cyrillic text nearly every
    // letter is a high byte, in Western European text only a few are.
    [[nodiscard]] static bool LooksLikeCp1251(std::string_view bytes);

    [[nodiscard]] static std::string FallbackForLanguage(const std::string& language);

    [[nodiscard]] static std::optional<std::string> ReadFile(const std::filesystem::path& path);
    static bool WriteFileAtomically(const std::filesystem::path& path, std::string_view content);
    static void AppendUtf8(std::string& out, char32_t codePoint);
};
