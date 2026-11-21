#include "commands/LanguageCommands.hpp"

#include <chrono>
#include <string_view>

#include "console/ConsoleText.hpp"
#include "pipeline/playback/TrackPreferences.hpp"

namespace mango::cli
{

// One item of Engine::GetLanguagesJson. Not in the anonymous namespace:
// glaze's reflection needs a type with linkage.
struct KnownLanguage
{
    std::string code;
    std::string name;
};

namespace
{

using namespace std::chrono_literals;

constexpr auto kUsage =
    "Usage: languages [audio <codes>|subtitles foreign|always|forced|off|subtitle-languages <codes>|forget|list]\n"
    "       <codes>: comma-separated, best first (eng,jpn), or none";

mango::SettingsState Settings(const CommandContext& context)
{
    return context.watcher.Read<mango::SettingsState>(mango::SessionPart::Settings);
}

std::string JoinCodes(const std::vector<std::string>& codes)
{
    std::string text;
    for (const auto& code : codes)
    {
        text += (text.empty() ? "" : ",") + code;
    }
    return text.empty() ? "none" : text;
}

// "eng,jpn" → {"eng", "jpn"}; "none" → {}.
std::vector<std::string> ParseCodes(const std::string& text)
{
    return text == "none" ? std::vector<std::string>{} : ParseLanguageList(text);
}

void ShowPreferences(const CommandContext& context)
{
    const auto settings = Settings(context);
    context.out().Print("  audio               {}   (empty: the file's default)", JoinCodes(settings.audioLanguages));
    context.out().Print("  subtitles           {}   (foreign, always, forced, off)", settings.subtitleMode);
    context.out().Print("  subtitle-languages  {}   (empty: the audio languages)", JoinCodes(settings.subtitleLanguages));
}

void ListLanguages(const CommandContext& context)
{
    const auto languages = ParseJson<std::vector<KnownLanguage>>(context.engine.GetLanguagesJson());
    for (const auto& language : languages.value_or(std::vector<KnownLanguage>{}))
    {
        context.out().Print("  {}  {}", language.code, language.name);
    }
}

// The session applies commands on its own thread: show the result once
// the settings changed.
void ShowWhenApplied(CommandContext& context, const mango::SettingsState& before)
{
    context.watcher.WaitUntil(
        [&]
        {
            const auto now = Settings(context);
            return now.audioLanguages != before.audioLanguages || now.subtitleMode != before.subtitleMode ||
                   now.subtitleLanguages != before.subtitleLanguages;
        },
        1s);
    ShowPreferences(context);
}

bool SetSubtitleMode(CommandContext& context, const std::string& value)
{
    const auto mode = SubtitleModes::Parse(value);
    if (!mode)
    {
        return false;
    }
    context.session().SetSubtitleMode(*mode);
    return true;
}

// Commands with a value: audio, subtitles, subtitle-languages.
bool SetPreference(CommandContext& context, const std::string& name, const std::string& value)
{
    if (name == "audio")
    {
        context.session().SetAudioLanguages(ParseCodes(value));
        return true;
    }
    if (name == "subtitle-languages")
    {
        context.session().SetSubtitleLanguages(ParseCodes(value));
        return true;
    }
    return name == "subtitles" && SetSubtitleMode(context, value);
}

void Languages(CommandContext& context, const Args& args)
{
    if (args.empty())
    {
        ShowPreferences(context);
    }
    else if (args.size() == 1 && args[0] == "list")
    {
        ListLanguages(context);
    }
    else if (args.size() == 1 && args[0] == "forget")
    {
        context.session().ForgetFolderTracks();
        context.out().Print("Folder track choices forgotten.");
    }
    else if (const auto before = Settings(context); args.size() == 2 && SetPreference(context, args[0], args[1]))
    {
        ShowWhenApplied(context, before);
    }
    else
    {
        context.out().Print("{}", kUsage);
    }
}

}  // namespace

void RegisterLanguageCommands(CommandTable& table, CommandContext& context)
{
    table.Add({"languages", "[audio|subtitles|subtitle-languages <value>|forget|list]",
        "Show or set the track language preferences", [&context](const Args& args) { Languages(context, args); }});
}

}  // namespace mango::cli
