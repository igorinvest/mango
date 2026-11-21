// Track selection (docs/scope/track-selection-scope.md, Testing): the pure
// TrackSelector rule by rule, the language table and the folder memory.

#include <filesystem>
#include <fstream>
#include <glaze/glaze.hpp>
#include <set>
#include <unordered_map>
#include <variant>

#include "TestCheck.hpp"
#include "configuration/Languages.hpp"
#include "pipeline/playback/FolderTrackMemory.hpp"
#include "pipeline/playback/TrackSelector.hpp"

namespace fs = std::filesystem;

// One item of Languages::GetLanguagesJson.
struct TestLanguageItem
{
    std::string code;
    std::string name;
};

namespace
{

AudioTrack Audio(int index, const std::string& language, bool isDefault = false, bool commentary = false)
{
    return AudioTrack{index, "aac", 2, 48000, 0, language, "", isDefault, commentary};
}

SubtitleTrack Subtitle(int index, const std::string& language, bool forced = false, bool isDefault = false,
    const std::string& codec = "subrip")
{
    return SubtitleTrack{index, codec, language, "", isDefault, forced};
}

FileRecord File(std::vector<AudioTrack> audios, std::vector<SubtitleTrack> subtitles = {})
{
    FileRecord file;
    file.fastHash = "f";
    file.filePath = "/media/Show/Episode 1.mkv";
    file.mediaInfo.videos = {VideoTrack{0, "h264", 1920, 1080, 24.0, 0, "", "", 0, false}};
    file.mediaInfo.audios = std::move(audios);
    file.mediaInfo.subtitles = std::move(subtitles);
    return file;
}

TrackContext Prefs(std::vector<std::string> audio, SubtitleMode mode = SubtitleMode::foreignAudioOnly,
    std::vector<std::string> subtitles = {})
{
    return TrackContext{.preferences = {std::move(audio), mode, std::move(subtitles)}};
}

StreamTrackSelection Select(const FileRecord& file, const TrackContext& context, TrackRequest request = {})
{
    return TrackSelector::SelectTracks(file, request, context);
}

// ---- Rule 1: chosen --------------------------------------------------------

void ChosenBeatsEverything()
{
    const auto file = File({Audio(1, "eng"), Audio(2, "jpn")}, {Subtitle(3, "eng"), Subtitle(4, "jpn")});
    auto context = Prefs({"eng"}, SubtitleMode::always);
    context.played = PlaybackRecord{.audioTrackId = 1, .subtitleTrackId = 3};
    context.folder = FolderTrackChoice{.audioLanguage = "eng", .subtitleLanguage = "eng"};

    const auto chosen = Select(file, context, {2, 4});
    Check(chosen.audioTrackId == 2 && chosen.subtitleTrackId == 4, "a chosen id beats every other rule");
    Check(!Select(file, context, {std::nullopt, -1}).subtitleTrackId, "chosen subtitles -1 gives none");

    const auto audioOnly = Select(file, context, {2, std::nullopt});
    Check(audioOnly.subtitleTrackId == 3, "an unchosen kind ignores the other kind's choice");
}

// ---- Rules 2 and 3: memory -------------------------------------------------

void FileMemoryBeatsFolderMemory()
{
    const auto file = File({Audio(1, "eng"), Audio(2, "jpn")}, {Subtitle(3, "eng"), Subtitle(4, "jpn")});
    auto context = Prefs({"eng"});
    context.played = PlaybackRecord{.audioTrackId = 1, .subtitleTrackId = -1};
    context.folder = FolderTrackChoice{.audioLanguage = "jpn", .subtitleLanguage = "jpn"};

    const auto picked = Select(file, context);
    Check(picked.audioTrackId == 1 && !picked.subtitleTrackId, "per-file memory beats folder memory, off included");

    context.played = PlaybackRecord{.audioTrackId = 9, .subtitleTrackId = 8};
    const auto fallen = Select(file, context);
    Check(fallen.audioTrackId == 2 && fallen.subtitleTrackId == 4, "a remembered track that's gone falls through");
}

void FolderMemoryFollowsLanguage()
{
    // Episode 2: Japanese is the third track, not the second.
    const auto file = File({Audio(1, "eng"), Audio(2, "fre"), Audio(5, "ja")}, {Subtitle(6, "eng")});
    auto context = Prefs({"eng"}, SubtitleMode::always);
    context.folder = FolderTrackChoice{.audioLanguage = "jpn", .subtitlesOff = true};

    const auto picked = Select(file, context);
    Check(picked.audioTrackId == 5, "folder memory in jpn picks the Japanese track at a different index");
    Check(!picked.subtitleTrackId, "folder memory turns subtitles off");

    context.folder = FolderTrackChoice{.subtitleLanguage = "ger"};
    Check(Select(file, context).subtitleTrackId == 6, "a remembered language with no track falls through");
}

// ---- Rules 4–6: preferences, default flag, first ---------------------------

void AudioPreferencesThenDefaultThenFirst()
{
    const auto englishOnly = File({Audio(1, "fra"), Audio(2, "eng")});
    Check(Select(englishOnly, Prefs({"jpn", "eng"})).audioTrackId == 2, "the first preferred language that has a track");

    const auto flagged = File({Audio(1, "fra"), Audio(2, "deu", true)});
    Check(Select(flagged, Prefs({"jpn", "eng"})).audioTrackId == 2, "with neither, the default disposition");

    const auto plain = File({Audio(1, "fra"), Audio(2, "deu")});
    Check(Select(plain, Prefs({})).audioTrackId == 1, "with no flag, the first track");
}

void NormalTracksBeatSecondaryOnes()
{
    const auto commentary = File({Audio(1, "eng", false, true), Audio(2, "eng")});
    Check(Select(commentary, Prefs({"eng"})).audioTrackId == 2, "commentary loses to a normal track");

    auto withExternal = File({Audio(1, "jpn"), Audio(2, "eng")});
    withExternal.externalAudios = {ExternalAudio{.index = 1000, .language = "eng"}};
    Check(Select(withExternal, Prefs({"eng"})).audioTrackId == 2, "built-in beats external");

    const auto bitmap = File({Audio(1, "jpn")}, {Subtitle(2, "eng", false, false, "hdmv_pgs_subtitle"), Subtitle(3, "eng")});
    Check(Select(bitmap, Prefs({"eng"})).subtitleTrackId == 3, "bitmap subtitles are never auto-picked");
}

// ---- Subtitle modes --------------------------------------------------------

void ForeignAudioOnly()
{
    const auto english = File({Audio(1, "eng")}, {Subtitle(2, "eng"), Subtitle(3, "eng", true)});
    Check(Select(english, Prefs({"eng"})).subtitleTrackId == 3,
        "English audio, English preferred: no full subtitles, but the forced English track");

    const auto noForced = File({Audio(1, "eng")}, {Subtitle(2, "eng")});
    Check(!Select(noForced, Prefs({"eng"})).subtitleTrackId, "and none without one");

    const auto japanese = File({Audio(1, "jpn")}, {Subtitle(2, "fra"), Subtitle(3, "eng")});
    Check(Select(japanese, Prefs({"eng"})).subtitleTrackId == 3, "Japanese audio shows English subtitles");

    const auto undetermined = File({Audio(1, "und")}, {Subtitle(2, "eng")});
    Check(Select(undetermined, Prefs({"eng"})).subtitleTrackId == 2, "audio with und counts as foreign");

    Check(!Select(japanese, Prefs({})).subtitleTrackId, "with no languages at all, only forced subtitles show");
}

void OffAlwaysForcedOnly()
{
    const auto file = File({Audio(1, "eng")}, {Subtitle(2, "fra"), Subtitle(3, "eng", true), Subtitle(4, "deu", false, true)});
    Check(!Select(file, Prefs({"eng"}, SubtitleMode::off)).subtitleTrackId, "off shows nothing, not even forced");
    Check(Select(file, Prefs({"eng"}, SubtitleMode::forcedOnly)).subtitleTrackId == 3, "forcedOnly shows only forced");
    Check(Select(file, Prefs({"eng"}, SubtitleMode::always, {"fra"})).subtitleTrackId == 2,
        "always: a preferred language");
    Check(Select(file, Prefs({"eng"}, SubtitleMode::always, {"jpn"})).subtitleTrackId == 4, "else the default one");

    const auto noFlag = File({Audio(1, "eng")}, {Subtitle(2, "fra"), Subtitle(5, "deu")});
    Check(Select(noFlag, Prefs({"eng"}, SubtitleMode::always, {"jpn"})).subtitleTrackId == 2, "else the first text track");
}

// ---- Languages --------------------------------------------------------------

void LanguageTable()
{
    Check(Languages::Normalize("en") == "eng" && Languages::Normalize("English") == "eng" &&
              Languages::Normalize("ENG") == "eng",
        "aliases and names normalize to the code");
    Check(Languages::Normalize("ger") == "deu" && Languages::Normalize("de") == "deu", "ger and de are deu");
    Check(Languages::SameLanguage("ger", "deu"), "ger and deu match");
    Check(!Languages::Normalize("und") && !Languages::Normalize("") && !Languages::IsSupported("und"),
        "und and empty are unknown");
    Check(!Languages::SameLanguage("und", "und") && !Languages::SameLanguage("", ""), "und and empty never match");
    Check(Languages::IsSupported("jpn") && Languages::GetName("ja") == "Japanese", "jpn is known, with its name");

    std::vector<TestLanguageItem> items;
    constexpr glz::opts lenient{.error_on_unknown_keys = false};
    Check(!glz::read<lenient>(items, Languages::GetLanguagesJson()) && items.size() > 40, "GetLanguagesJson parses");
    Check(std::ranges::is_sorted(items, {}, &TestLanguageItem::name), "sorted by name");
    std::set<std::string> codes;
    for (const auto& item : items)
    {
        codes.insert(item.code);
        Check(Languages::Normalize(item.code) == item.code && Languages::Normalize(item.name) == item.code,
            "code and name normalize to the code: " + item.code);
    }
    Check(codes.size() == items.size(), "no duplicate codes");
}

// ---- Folder memory ----------------------------------------------------------

TrackList Playing(const std::string& audioLanguage, std::optional<std::string> subtitleLanguage)
{
    TrackList list;
    list.audioTrack = Audio(2, audioLanguage);
    if (subtitleLanguage)
    {
        list.subtitleTrack = Subtitle(4, *subtitleLanguage);
    }
    return list;
}

void FolderMemoryWritesChosenKindsOnly()
{
    FolderTrackMemory memory({});
    const fs::path episode = "/media/Show/Episode 1.mkv";

    memory.RememberChosen(episode, {}, Playing("jpn", "eng"));
    Check(!memory.Get(episode), "an engine pick writes nothing");

    memory.RememberChosen(episode, {2, std::nullopt}, Playing("ja", "eng"));
    auto choice = memory.Get("/media/Show/Episode 2.mkv");
    Check(choice && choice->audioLanguage == "jpn" && !choice->subtitleLanguage && !choice->subtitlesOff,
        "a chosen audio writes its language only, for the whole folder");
    Check(!memory.Get("/media/Show/Season 2/Episode 1.mkv"), "not for subfolders");

    memory.RememberChosen(episode, {std::nullopt, -1}, Playing("eng", std::nullopt));
    choice = memory.Get(episode);
    Check(choice && choice->audioLanguage == "jpn" && choice->subtitlesOff, "subtitles off is kept, audio stays");

    memory.RememberChosen(episode, {std::nullopt, 4}, Playing("eng", "fre"));
    choice = memory.Get(episode);
    Check(choice && choice->subtitleLanguage == "fra" && !choice->subtitlesOff, "a chosen subtitle turns it back on");

    memory.Clear();
    Check(memory.Size() == 0, "forget clears every folder");
}

void FolderMemoryCapAndCorruptFile()
{
    FolderTrackMemory memory({});
    for (std::size_t i = 0; i <= FolderTrackMemory::maxFolders; ++i)
    {
        memory.RememberAudio("/media/" + std::to_string(i) + "/a.mkv", "eng");
    }
    Check(memory.Size() == FolderTrackMemory::maxFolders, "at most 500 folders");
    Check(memory.Get("/media/500/a.mkv").has_value(), "the newest one is kept");

    const auto file = fs::temp_directory_path() / "mango-test-folder_tracks.json";
    {
        std::ofstream out(file);
        out << "{ not json";
    }
    {
        FolderTrackMemory corrupt(file);
        Check(corrupt.Size() == 0, "a corrupt file starts over empty");
        corrupt.RememberAudio("/media/x/a.mkv", "jpn");
    }
    FolderTrackMemory reloaded(file);
    Check(reloaded.Get("/media/x/a.mkv").has_value(), "and is written fresh");
    fs::remove(file);
}

// settings.json's value type (Settings::SettingValue) with the preference
// lists in it.
void SettingsListsRoundTrip()
{
    using Value = std::optional<std::variant<bool, int, std::string, std::vector<std::string>>>;
    std::unordered_map<std::string, Value> settings;
    const auto text = R"({"audioLanguages":["jpn","eng"],"subtitleMode":"always","autoplay":true,"delay":3000,)"
                      R"("subtitleLanguages":[],"lastUsedDevice":null})";
    Check(!glz::read_json(settings, text), "settings with lists parse");
    const auto* audio = std::get_if<std::vector<std::string>>(&*settings["audioLanguages"]);
    Check(audio && *audio == std::vector<std::string>{"jpn", "eng"}, "a list reads as a list");
    Check(std::holds_alternative<std::string>(*settings["subtitleMode"]) &&
              std::holds_alternative<bool>(*settings["autoplay"]) && std::holds_alternative<int>(*settings["delay"]) &&
              !settings["lastUsedDevice"],
        "the other kinds are unchanged");

    // Null values are left out on write, as before (Settings::Load adds
    // the defaults back).
    settings.erase("lastUsedDevice");
    std::unordered_map<std::string, Value> back;
    Check(!glz::read_json(back, glz::write_json(settings).value_or("")) && back == settings, "and writes back the same");
}

}  // namespace

std::vector<TestCase> TrackSelectionTestCases()
{
    return {
        {"tracks: chosen beats everything", ChosenBeatsEverything},
        {"tracks: file memory beats folder memory", FileMemoryBeatsFolderMemory},
        {"tracks: folder memory follows the language", FolderMemoryFollowsLanguage},
        {"tracks: audio preferences, default, first", AudioPreferencesThenDefaultThenFirst},
        {"tracks: normal beats commentary and forced", NormalTracksBeatSecondaryOnes},
        {"tracks: foreign audio only", ForeignAudioOnly},
        {"tracks: off, always, forced only", OffAlwaysForcedOnly},
        {"tracks: language table", LanguageTable},
        {"tracks: folder memory writes chosen kinds only", FolderMemoryWritesChosenKindsOnly},
        {"tracks: folder memory cap and corrupt file", FolderMemoryCapAndCorruptFile},
        {"tracks: settings lists round trip", SettingsListsRoundTrip},
    };
}
