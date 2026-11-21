#include "Languages.hpp"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <vector>

#include <glaze/glaze.hpp>

// One item of GetLanguagesJson. Not in the anonymous namespace: glaze's
// reflection needs a type with linkage.
struct LanguageListItem
{
    std::string code;
    std::string name;
};

namespace
{
struct LanguageEntry
{
    const char* code;  // ISO 639-2/T
    const char* name;  // English
    // ISO 639-1, ISO 639-2/B (where it differs) and other English names.
    // The name itself is accepted too.
    std::initializer_list<const char*> aliases;
};

const std::initializer_list<LanguageEntry> kLanguages = {
    {"eng", "English", {"en"}},
    {"rus", "Russian", {"ru"}},
    {"ukr", "Ukrainian", {"uk"}},
    {"bel", "Belarusian", {"be"}},
    {"bul", "Bulgarian", {"bg"}},
    {"srp", "Serbian", {"sr", "scc"}},
    {"hrv", "Croatian", {"hr", "scr"}},
    {"bos", "Bosnian", {"bs"}},
    {"slv", "Slovenian", {"sl", "slovene"}},
    {"mkd", "Macedonian", {"mk", "mac"}},
    {"pol", "Polish", {"pl"}},
    {"ces", "Czech", {"cs", "cze"}},
    {"slk", "Slovak", {"sk", "slo"}},
    {"hun", "Hungarian", {"hu"}},
    {"ron", "Romanian", {"ro", "rum"}},
    {"deu", "German", {"de", "ger"}},
    {"fra", "French", {"fr", "fre"}},
    {"spa", "Spanish", {"es"}},
    {"por", "Portuguese", {"pt"}},
    {"ita", "Italian", {"it"}},
    {"nld", "Dutch", {"nl", "dut"}},
    {"swe", "Swedish", {"sv"}},
    {"nor", "Norwegian", {"no"}},
    {"nob", "Norwegian Bokmål", {"nb", "bokmal"}},
    {"dan", "Danish", {"da"}},
    {"fin", "Finnish", {"fi"}},
    {"isl", "Icelandic", {"is", "ice"}},
    {"est", "Estonian", {"et"}},
    {"lav", "Latvian", {"lv"}},
    {"lit", "Lithuanian", {"lt"}},
    {"ell", "Greek", {"el", "gre"}},
    {"tur", "Turkish", {"tr"}},
    {"ara", "Arabic", {"ar"}},
    {"heb", "Hebrew", {"he"}},
    {"fas", "Persian", {"fa", "per"}},
    {"hin", "Hindi", {"hi"}},
    {"tha", "Thai", {"th"}},
    {"vie", "Vietnamese", {"vi"}},
    {"ind", "Indonesian", {"id"}},
    {"msa", "Malay", {"ms", "may"}},
    {"zho", "Chinese", {"zh", "chi"}},
    {"jpn", "Japanese", {"ja"}},
    {"kor", "Korean", {"ko"}},
    {"kat", "Georgian", {"ka", "geo"}},
    {"hye", "Armenian", {"hy", "arm"}},
    {"kaz", "Kazakh", {"kk"}},
    {"cat", "Catalan", {"ca"}},
    {"eus", "Basque", {"eu", "baq"}},
    {"glg", "Galician", {"gl"}},
};

std::string ToLower(std::string_view value)
{
    std::string lower(value);
    std::ranges::transform(lower, lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}
}  // namespace

const std::unordered_map<std::string, std::string>& Languages::GetAliasMap()
{
    static const auto aliases = []
    {
        std::unordered_map<std::string, std::string> map;
        for (const auto& language : kLanguages)
        {
            map.emplace(language.code, language.code);
            map.emplace(ToLower(language.name), language.code);
            for (const char* alias : language.aliases)
            {
                map.emplace(alias, language.code);
            }
        }
        return map;
    }();

    return aliases;
}

const std::unordered_map<std::string, std::string>& Languages::GetNameMap()
{
    static const auto names = []
    {
        std::unordered_map<std::string, std::string> map;
        for (const auto& language : kLanguages)
        {
            map.emplace(language.code, language.name);
        }
        return map;
    }();

    return names;
}

std::optional<std::string> Languages::Normalize(std::string_view token)
{
    const auto& aliases = GetAliasMap();
    const auto it = aliases.find(ToLower(token));
    return it != aliases.end() ? std::optional(it->second) : std::nullopt;
}

bool Languages::IsSupported(std::string_view code) { return Normalize(code).has_value(); }

std::string Languages::GetName(const std::string& code)
{
    const auto canonical = Normalize(code);
    return canonical ? GetNameMap().at(*canonical) : code;
}

bool Languages::IsUndetermined(std::string_view code) { return code.empty() || ToLower(code) == "und"; }

std::string Languages::Canonical(const std::string& code) { return Normalize(code).value_or(ToLower(code)); }

bool Languages::SameLanguage(const std::string& a, const std::string& b)
{
    return !IsUndetermined(a) && !IsUndetermined(b) && Canonical(a) == Canonical(b);
}

std::string Languages::GetLanguagesJson()
{
    std::vector<LanguageListItem> items;
    for (const auto& language : kLanguages)
    {
        items.push_back({language.code, language.name});
    }
    std::ranges::sort(items, {}, &LanguageListItem::name);
    return glz::write_json(items).value_or("[]");
}
