#include "commands/StreamCommands.hpp"

#include <chrono>

#include "commands/CastCommands.hpp"
#include "commands/LibraryCommands.hpp"
#include "console/ConsoleText.hpp"
#include "session/Notices.hpp"

namespace mango::cli
{

namespace
{

// play/stream options: audio=N sub=N|off from=TIME. Only the tracks
// given are chosen; the engine picks the others.
struct StreamChoice
{
    TrackRequest tracks;
    double startSeconds = 0.0;
};

// A track option's value: nullopt when it isn't given, an error (after
// which the caller prints usage) when it's malformed. "off" only for
// subtitles.
std::optional<std::optional<int>> ParseTrackOption(const Args& args, const std::string& name, bool allowOff)
{
    const auto value = FindOption(args, name);
    if (!value)
    {
        return std::optional<int>();
    }
    if (allowOff && *value == "off")
    {
        return std::optional(-1);
    }
    const auto id = ParseInt(*value);
    return id ? std::optional(id) : std::nullopt;
}

// Defaults: the engine's pick for each kind, and the movie's resume point.
// nullopt (after printing why) for a malformed option.
std::optional<StreamChoice> ParseChoice(const CommandContext& context, const mango::LibraryEntry& entry, const Args& args)
{
    const auto audio = ParseTrackOption(args, "audio", false);
    const auto sub = ParseTrackOption(args, "sub", true);
    const auto from = FindOption(args, "from");
    const auto start = from ? ParseTime(*from) : std::optional(entry.resumeStartSeconds);

    if (!audio || !sub || !start)
    {
        context.out().Print("Bad option. Use audio=N sub=N|off from=1:02:03 (track numbers from `info`).");
        return std::nullopt;
    }
    return StreamChoice{TrackRequest{*audio, *sub}, *start};
}

std::optional<mango::StreamInitiation> Initiate(
    const CommandContext& context, const mango::LibraryEntry& entry, const StreamChoice& choice, PlaylistType playlist)
{
    context.out().Print("Preparing {} ...", entry.title);
    const auto result = ParseJson<mango::StreamInitiation>(
        context.engine.InitiateStreamJson(entry.fastHash, choice.tracks, playlist));
    if (!result || !result->success)
    {
        context.out().Print("Couldn't start the stream: {}", result ? result->error : "no answer");
        return std::nullopt;
    }
    return result;
}

using namespace std::chrono_literals;

mango::NowPlayingState NowPlaying(const CommandContext& context)
{
    return context.watcher.Read<mango::NowPlayingState>(mango::SessionPart::NowPlaying);
}

// The session prepares the stream (a fresh conversion gets its head start)
// and sends it to the TV; this waits for that, or for the failure notice.
// Returns why it failed, "" once it's on the TV.
std::string WaitUntilCast(CommandContext& context, const std::string& fastHash, std::uint64_t noticeBefore)
{
    const auto failed = [&]
    {
        const auto notice = context.watcher.FindNotice(noticeBefore, mango::NoticeCode::StreamStartFailed);
        if (!notice)
        {
            return std::string();
        }
        const auto error = notice->params.find("error");
        return error == notice->params.end() ? std::string("unknown") : error->second;
    };
    const auto busy = [&]
    {
        const auto state = NowPlaying(context).state;
        return state == "starting" || state == "loading";
    };

    // Starting shows up once the session has taken the command.
    context.watcher.WaitUntil([&] { return busy() || !failed().empty(); }, 2s);
    context.watcher.WaitUntil([&] { return !busy() || !failed().empty(); }, 120s);

    if (const auto error = failed(); !error.empty())
    {
        return error;
    }
    return NowPlaying(context).fastHash == fastHash ? "" : "not started (another one was starting)";
}

void Play(CommandContext& context, const Args& args)
{
    if (args.empty())
    {
        context.out().Print("Usage: play <movie> [audio=N] [sub=N|off] [from=TIME]");
        return;
    }
    const auto entry = FindMovie(context.out(), context.engine, args.front());
    const auto choice = entry ? ParseChoice(context, *entry, args) : std::nullopt;
    const auto device = choice ? RequireDevice(context) : std::nullopt;
    if (!device)
    {
        return;
    }

    context.out().Print("Preparing {} for {} from {} ...", entry->title, device->name, FormatTime(choice->startSeconds));
    const auto noticeBefore = context.watcher.LastNoticeId();

    // No section: the console has no playlist, as before.
    context.session().Play(entry->fastHash, "", choice->startSeconds, choice->tracks);

    const auto error = WaitUntilCast(context, entry->fastHash, noticeBefore);
    context.out().Print("{}", error.empty() ? "Casting." : "Couldn't start the stream: " + error);
}

// For VLC, a browser, or anything else that plays a URL.
void PrintStreamUrl(const CommandContext& context, const Args& args)
{
    if (args.empty())
    {
        context.out().Print("Usage: stream <movie> [audio=N] [sub=N|off] [hls]");
        return;
    }
    const auto entry = FindMovie(context.out(), context.engine, args.front());
    const auto choice = entry ? ParseChoice(context, *entry, args) : std::nullopt;
    const auto playlist = std::ranges::contains(args, std::string("hls")) ? PlaylistType::HLS : PlaylistType::DASH;
    const auto stream = choice ? Initiate(context, *entry, *choice, playlist) : std::nullopt;
    if (!stream)
    {
        return;
    }

    const auto& baseUrl = stream->serverUrl;
    context.out().Print("{}/stream/{}", baseUrl, stream->streamAddress);
    if (!stream->subtitleAddress.empty())
    {
        context.out().Print("subtitles: {}/stream/{}", baseUrl, stream->subtitleAddress);
    }
}

void ShowProgress(const CommandContext& context)
{
    const auto status = ParseJson<mango::TranscodeStatus>(context.engine.GetTranscodeStatusJson());
    if (!status || status->fastHash.empty())
    {
        context.out().Print("Nothing is converting.");
        return;
    }
    if (!status->error.empty())
    {
        context.out().Print("{}: conversion failed ({})", status->fastHash, status->error);
        return;
    }
    context.out().Print("{}: {:.0f}% converted", status->fastHash, status->percent);
}

}  // namespace

void RegisterStreamCommands(CommandTable& table, CommandContext& context)
{
    table.Add({"play", "<movie> [audio=N] [sub=N|off] [from=TIME]",
        "Cast a movie to the selected TV (resumes by default)",
        [&context](const Args& args) { Play(context, args); }});
    table.Add({"stream", "<movie> [audio=N] [sub=N|off] [hls]",
        "Start a stream and print its URL, for other players",
        [&context](const Args& args) { PrintStreamUrl(context, args); }});
    table.Add({"progress", "", "How much of the current stream is converted",
        [&context](const Args&) { ShowProgress(context); }});
}

}  // namespace mango::cli
