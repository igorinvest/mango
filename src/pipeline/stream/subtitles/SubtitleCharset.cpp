#include "SubtitleCharset.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>

#include "../../../configuration/Languages.hpp"
#include "../../../file/File.hpp"

namespace
{
// Subtitle files are small, but a guess doesn't need all of a big one.
constexpr std::streamsize kMaxBytesRead = 1024 * 1024;

// Unicode code points of bytes 0x80-0xFF, generated from Python's codecs.
constexpr std::array<std::uint16_t, 128> kCp1250 = {
    0x20AC, 0xFFFD, 0x201A, 0xFFFD, 0x201E, 0x2026, 0x2020, 0x2021,
    0xFFFD, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
    0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0xFFFD, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
    0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7,
    0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
    0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
    0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
    0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
    0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
    0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
    0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
    0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
    0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
    0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
    0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9,
};

constexpr std::array<std::uint16_t, 128> kCp1251 = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
    0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0xFFFD, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
    0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
    0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
    0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
    0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
    0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
};

constexpr std::array<std::uint16_t, 128> kCp1252 = {
    0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
    0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178,
    0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7,
    0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
    0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
    0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,
    0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7,
    0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
    0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7,
    0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF,
    0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7,
    0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
    0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7,
    0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF,
};

const std::array<std::uint16_t, 128>& CodePageTable(const std::string& charset)
{
    if (charset == "CP1250") return kCp1250;
    if (charset == "CP1251") return kCp1251;
    return kCp1252;
}

bool IsHigh(unsigned char c) { return c >= 0x80; }

bool IsAsciiLetter(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

// Length of the UTF-8 sequence led by c, or 0 when c can't lead one.
int Utf8SequenceLength(unsigned char c)
{
    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF) return 2;
    if (c >= 0xE0 && c <= 0xEF) return 3;
    if (c >= 0xF0 && c <= 0xF4) return 4;
    return 0;
}
}  // namespace

std::string SubtitleCharset::Detect(
    const std::filesystem::path& path, const std::string& language)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return {};
    }

    std::string bytes(static_cast<std::size_t>(kMaxBytesRead), '\0');
    file.read(bytes.data(), kMaxBytesRead);
    bytes.resize(static_cast<std::size_t>(file.gcount()));

    return DetectFromBytes(bytes, language);
}

std::string SubtitleCharset::DetectFromBytes(std::string_view bytes, const std::string& language)
{
    if (HasUnicodeBom(bytes) || IsValidUtf8(bytes))
    {
        return {};
    }

    if (LooksLikeCp1251(bytes))
    {
        return "CP1251";
    }

    // Latin-looking text: only the language can tell CP1250 from CP1252.
    const auto fallback = FallbackForLanguage(language);
    return fallback == "CP1251" ? "CP1252" : fallback;
}

bool SubtitleCharset::HasUnicodeBom(std::string_view bytes)
{
    return bytes.starts_with("\xEF\xBB\xBF") || bytes.starts_with("\xFF\xFE") ||
        bytes.starts_with("\xFE\xFF");
}

bool SubtitleCharset::IsValidUtf8(std::string_view bytes)
{
    std::size_t i = 0;
    while (i < bytes.size())
    {
        const int length = Utf8SequenceLength(static_cast<unsigned char>(bytes[i]));
        if (length == 0)
        {
            return false;
        }

        // A sequence cut off by kMaxBytesRead still counts as valid.
        if (i + static_cast<std::size_t>(length) > bytes.size())
        {
            return true;
        }

        for (int k = 1; k < length; ++k)
        {
            if ((static_cast<unsigned char>(bytes[i + static_cast<std::size_t>(k)]) & 0xC0) != 0x80)
            {
                return false;
            }
        }

        i += static_cast<std::size_t>(length);
    }

    return true;
}

bool SubtitleCharset::LooksLikeCp1251(std::string_view bytes)
{
    std::size_t asciiLetters = 0;
    std::size_t cyrillicLetters = 0;  // 0xC0-0xFF, plus Ё/ё (0xA8/0xB8)

    for (const char ch : bytes)
    {
        const auto c = static_cast<unsigned char>(ch);
        if (IsAsciiLetter(c))
        {
            ++asciiLetters;
        }
        else if (IsHigh(c) && (c >= 0xC0 || c == 0xA8 || c == 0xB8))
        {
            ++cyrillicLetters;
        }
    }

    // Subtitle files have some ASCII letters regardless (tags, the odd
    // name), so Cyrillic text shows as high bytes outnumbering them.
    return cyrillicLetters > 0 && cyrillicLetters * 2 >= asciiLetters;
}

std::string SubtitleCharset::FallbackForLanguage(const std::string& language)
{
    static constexpr std::array kCyrillic = {"rus", "ukr", "bel", "bul", "srp", "mkd"};
    static constexpr std::array kCentralEuropean = {
        "pol", "ces", "slk", "hun", "slv", "hrv", "bos", "ron"};

    const auto code = Languages::Normalize(language).value_or("");

    if (std::ranges::find(kCyrillic, code) != kCyrillic.end())
    {
        return "CP1251";
    }
    if (std::ranges::find(kCentralEuropean, code) != kCentralEuropean.end())
    {
        return "CP1250";
    }
    return "CP1252";
}

std::string SubtitleCharset::ToUtf8(std::string_view bytes, const std::string& charset)
{
    const auto& table = CodePageTable(charset);

    std::string utf8;
    utf8.reserve(bytes.size() * 2);
    for (const char ch : bytes)
    {
        const auto c = static_cast<unsigned char>(ch);
        AppendUtf8(utf8, c < 0x80 ? char32_t{c} : char32_t{table[c - 0x80]});
    }
    return utf8;
}

std::optional<std::filesystem::path> SubtitleCharset::PrepareFfmpegInput(
    const ExternalSubtitle& subtitle, const std::filesystem::path& cacheFolder)
{
    const auto source = File::FromUtf8(subtitle.path);
    if (subtitle.charset.empty())
    {
        return source;
    }

    const auto copy = cacheFolder /
        (std::to_string(subtitle.index) + "-" + std::to_string(subtitle.fileSizeBytes) + "-" +
            std::to_string(subtitle.lastWriteUnixSeconds) + File::ToUtf8(source.extension()));
    if (File::Exists(copy))
    {
        return copy;
    }

    const auto bytes = ReadFile(source);
    if (!bytes || !File::CreateDirectoryPath(cacheFolder) ||
        !WriteFileAtomically(copy, ToUtf8(*bytes, subtitle.charset)))
    {
        return std::nullopt;
    }
    return copy;
}

std::optional<std::string> SubtitleCharset::ReadFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(file), {});
}

bool SubtitleCharset::WriteFileAtomically(
    const std::filesystem::path& path, std::string_view content)
{
    auto tempPath = path;
    tempPath += ".tmp";

    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!file)
        {
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

void SubtitleCharset::AppendUtf8(std::string& out, char32_t codePoint)
{
    // Code pages only map into the Basic Multilingual Plane.
    if (codePoint < 0x80)
    {
        out.push_back(static_cast<char>(codePoint));
    }
    else if (codePoint < 0x800)
    {
        out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}
