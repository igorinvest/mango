#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

// The languages the engine knows: one table of canonical ISO 639-2/T codes,
// their other spellings and English names. Track languages, preferences
// and folder memory all go through Normalize, so "ger" and "deu" are one
// language everywhere.
class Languages
{
   public:
    // Maps an ISO 639-1 code ("en"), either ISO 639-2 form ("ger"/"deu") or
    // an English language name ("English"), case-insensitively, to one
    // canonical ISO 639-2/T code ("eng", "deu"). std::nullopt when token
    // isn't a known language — also for "" and "und".
    [[nodiscard]] static std::optional<std::string> Normalize(std::string_view token);

    // Whether code is a known language (see Normalize).
    [[nodiscard]] static bool IsSupported(std::string_view code);

    // code's English name ("English"), or code as given when it isn't a
    // known language.
    [[nodiscard]] static std::string GetName(const std::string& code);

    // Whether two language codes name the same language — "ger" and "deu"
    // do. Empty and "und" never match anything.
    [[nodiscard]] static bool SameLanguage(const std::string& a, const std::string& b);

    // Every known language as [{"code": "eng", "name": "English"}, ...],
    // sorted by name, for language pickers.
    [[nodiscard]] static std::string GetLanguagesJson();

   private:
    // Every accepted spelling (lowercase) → canonical ISO 639-2/T code.
    static const std::unordered_map<std::string, std::string>& GetAliasMap();

    // Canonical code → English name.
    static const std::unordered_map<std::string, std::string>& GetNameMap();

    // Empty or "und": no language at all.
    [[nodiscard]] static bool IsUndetermined(std::string_view code);

    // code, normalized when it's a known language, otherwise lowercased.
    [[nodiscard]] static std::string Canonical(const std::string& code);
};
