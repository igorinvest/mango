#include "commands/LibraryCommands.hpp"

#include <algorithm>
#include <filesystem>

#include "console/ConsoleText.hpp"

namespace fs = std::filesystem;

namespace mango::cli
{

// Engine::GetLibraryScanStatusJson's shape.
struct ScanStatus
{
    bool scanning = false;
    std::string folderName;
    int filesDone = 0;
    int filesTotal = 0;
    int foldersQueued = 0;
};

namespace
{

std::string ToLower(std::string text)
{
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

fs::path FromUtf8(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

// A path may be typed unquoted with spaces — the words are joined back.
std::string JoinPath(const Args& args)
{
    std::string joined;
    for (const auto& arg : args)
    {
        joined += (joined.empty() ? "" : " ") + arg;
    }
    return ResolveUserPath(joined);
}

std::string WatchedLabel(const mango::LibraryEntry& entry)
{
    if (entry.isFullyWatched)
    {
        return "  watched";
    }
    if (entry.lastPositionSeconds > 0.0)
    {
        return "  at " + FormatTime(entry.lastPositionSeconds);
    }
    return "";
}

void PrintEntryLine(Output& out, std::size_t number, const mango::LibraryEntry& entry)
{
    out.Print("{:>4}. {}  ({}, {}p {}){}{}{}", number, entry.title,
        FormatTime(entry.durationSeconds), entry.videoHeight, entry.videoCodec,
        WatchedLabel(entry), entry.addedThisSession ? "  new" : "",
        entry.remotePeerName.empty() ? "" : "  [on " + entry.remotePeerName + "]");
}

void PrintTracks(Output& out, const char* heading, const std::vector<mango::TrackInfo>& tracks, int defaultId)
{
    out.Print("{}:{}", heading, tracks.empty() ? " none" : "");
    for (const auto& track : tracks)
    {
        out.Print("  {:>4}  {:<5} {}{}{}", track.index, track.language, track.title,
            track.isExternal ? "  [" + track.fileName + "]" : "",
            track.index == defaultId ? "  (default)" : "");
    }
}

void ListMovies(Output& out, const mango::Engine& engine, const Args& args)
{
    const auto filter = args.empty() ? std::string() : ToLower(args.front());
    const auto entries = SortedLibrary(engine);

    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        if (filter.empty() || ToLower(entries[i].title).contains(filter))
        {
            PrintEntryLine(out, i + 1, entries[i]);
        }
    }
    if (entries.empty())
    {
        out.Print("The library is empty. Add videos with add-folder <path> or add-file <path>.");
    }
}

void ShowMovie(Output& out, const mango::Engine& engine, const Args& args)
{
    if (args.empty())
    {
        out.Print("Usage: info <movie>");
        return;
    }
    const auto entry = FindMovie(out, engine, args.front());
    if (!entry)
    {
        return;
    }

    out.Print("{}\n  file:   {}\n  id:     {}\n  video:  {}x{} {}, {}", entry->title,
        entry->filePath, entry->fastHash, entry->videoWidth, entry->videoHeight,
        entry->videoCodec, FormatTime(entry->durationSeconds));
    if (entry->resumeStartSeconds > 0.0)
    {
        out.Print("  resume: {}", FormatTime(entry->resumeStartSeconds));
    }
    PrintTracks(out, "Audio", entry->audioTracks, entry->defaultAudioTrackId);
    PrintTracks(out, "Subtitles (off unless play sub=N)", entry->subtitleTracks, -1);
}

void ListFolders(Output& out, const mango::Engine& engine)
{
    const auto folders =
        ParseJson<std::vector<mango::LibraryFolderEntry>>(engine.GetLibraryFoldersJson());
    if (!folders || folders->empty())
    {
        out.Print("No folders added. Use add-folder <path>.");
        return;
    }
    for (const auto& folder : *folders)
    {
        out.Print("  {}  {}", folder.name, folder.rootPath);
    }
}

void AddFile(Output& out, mango::Engine& engine, const Args& args)
{
    const auto path = JoinPath(args);
    if (args.empty() || !fs::is_regular_file(FromUtf8(path)))
    {
        out.Print("Not a file: {}", path);
        return;
    }

    out.Print("Reading {} ...", path);
    out.Print("{}", engine.AddFile(path) ? "Added." : "Not added: not a playable video (see the log).");
}

void AddFolder(Output& out, mango::Engine& engine, const Args& args)
{
    const auto path = JoinPath(args);
    if (args.empty() || !fs::is_directory(FromUtf8(path)))
    {
        out.Print("Not a folder: {}", path);
        return;
    }

    engine.AddFolder(path);
    out.Print("Scanning {} in the background. `scan` shows progress.", path);
}

void ShowScanStatus(Output& out, const mango::Engine& engine)
{
    const auto status = ParseJson<ScanStatus>(engine.GetLibraryScanStatusJson());
    if (!status || !status->scanning)
    {
        out.Print("Not scanning.");
        return;
    }
    out.Print("Scanning {}: {}/{} files, {} more folder(s) queued", status->folderName,
        status->filesDone, status->filesTotal == 0 ? std::string("?") : std::to_string(status->filesTotal),
        status->foldersQueued);
}

}  // namespace

std::vector<mango::LibraryEntry> SortedLibrary(const mango::Engine& engine)
{
    auto entries = ParseJson<std::vector<mango::LibraryEntry>>(engine.GetLibraryEntriesJson())
                       .value_or(std::vector<mango::LibraryEntry>{});
    auto remote = ParseJson<std::vector<mango::LibraryEntry>>(engine.GetRemote().GetLibraryEntriesJson())
                      .value_or(std::vector<mango::LibraryEntry>{});
    entries.insert(entries.end(), remote.begin(), remote.end());

    // This device's videos first, then each paired device's.
    std::ranges::sort(entries,
        [](const auto& a, const auto& b)
        {
            return std::tie(a.remotePeerName, a.title, a.fastHash) <
                   std::tie(b.remotePeerName, b.title, b.fastHash);
        });
    return entries;
}

std::optional<mango::LibraryEntry> FindMovie(
    Output& out, const mango::Engine& engine, const std::string& reference)
{
    const auto entries = SortedLibrary(engine);

    if (const auto number = ParseInt(reference))
    {
        if (*number >= 1 && static_cast<std::size_t>(*number) <= entries.size())
        {
            return entries[*number - 1];
        }
    }
    const auto it = std::ranges::find_if(
        entries, [&](const auto& entry) { return entry.fastHash.starts_with(reference); });
    if (it != entries.end() && !reference.empty())
    {
        return *it;
    }

    out.Print("No movie {}. `ls` lists them with their numbers.", reference);
    return std::nullopt;
}

void RegisterLibraryCommands(CommandTable& table, CommandContext& context)
{
    table.Add({"ls", "[text]", "List movies, numbered; text filters by title",
        [&context](const Args& args) { ListMovies(context.out(), context.engine, args); }});
    table.Add({"info", "<movie>", "File, resume point, audio and subtitle tracks",
        [&context](const Args& args) { ShowMovie(context.out(), context.engine, args); }});
    table.Add({"folders", "", "List added folders",
        [&context](const Args&) { ListFolders(context.out(), context.engine); }});
    table.Add({"add-file", "<path>", "Add one video",
        [&context](const Args& args) { AddFile(context.out(), context.engine, args); }});
    table.Add({"add-folder", "<path>", "Add a folder and scan it for videos",
        [&context](const Args& args) { AddFolder(context.out(), context.engine, args); }});
    table.Add({"rescan", "", "Re-scan every added folder",
        [&context](const Args&) { context.engine.RescanLibraryFolders(); context.out().Print("Re-scan queued."); }});
    table.Add({"scan", "", "Folder scan progress",
        [&context](const Args&) { ShowScanStatus(context.out(), context.engine); }});
}

}  // namespace mango::cli
