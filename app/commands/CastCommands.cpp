#include "commands/CastCommands.hpp"

#include <algorithm>

#include "console/ConsoleText.hpp"

namespace mango::cli
{

namespace
{

using namespace std::chrono_literals;

// Discovery takes ~3 s; a cast command up to its 3 s timeout.
constexpr auto kDiscoveryWait = 10s;
constexpr auto kCommandWait = 10s;

mango::DevicesState Devices(const CommandContext& context)
{
    return context.watcher.Read<mango::DevicesState>(mango::SessionPart::Devices);
}

mango::NowPlayingState NowPlaying(const CommandContext& context)
{
    return context.watcher.Read<mango::NowPlayingState>(mango::SessionPart::NowPlaying);
}

std::vector<mango::SessionDevice> Chromecasts(const mango::DevicesState& state)
{
    std::vector<mango::SessionDevice> chromecasts;
    std::ranges::copy_if(state.devices, std::back_inserter(chromecasts),
        [](const auto& device) { return device.kind == "chromecast"; });
    return chromecasts;
}

void Discover(CommandContext& context)
{
    context.out().Print("Looking for Chromecasts ...");
    context.session().RefreshDevices();

    // Discovery is under way once the session says so, done once it no
    // longer does.
    context.watcher.WaitUntil([&] { return Devices(context).discovering; }, 1s);
    context.watcher.WaitUntil([&] { return !Devices(context).discovering; }, kDiscoveryWait);
    if (Devices(context).discoveryFailed)
    {
        context.out().Print("Discovery failed: no usable network interface.");
    }
}

void PrintDevices(const CommandContext& context)
{
    const auto state = Devices(context);
    const auto chromecasts = Chromecasts(state);
    if (chromecasts.empty())
    {
        context.out().Print("No Chromecasts found.");
    }
    for (std::size_t i = 0; i < chromecasts.size(); ++i)
    {
        const auto& device = chromecasts[i];
        context.out().Print("{:>4}. {}  {}{}", i + 1, device.name, device.receiverId,
            device.receiverId == state.selectedReceiverId ? "  (selected)" : "");
    }
}

void ListDevices(CommandContext& context)
{
    Discover(context);
    PrintDevices(context);
}

// By `devices` number, or by part of its name.
std::optional<mango::SessionDevice> FindDevice(const CommandContext& context, const std::string& reference)
{
    const auto chromecasts = Chromecasts(Devices(context));
    if (const auto number = ParseInt(reference);
        number && *number >= 1 && static_cast<std::size_t>(*number) <= chromecasts.size())
    {
        return chromecasts[*number - 1];
    }
    const auto it = std::ranges::find_if(chromecasts, [&](const auto& device) { return device.name.contains(reference); });
    return it == chromecasts.end() ? std::nullopt : std::optional(*it);
}

void UseDevice(CommandContext& context, const Args& args)
{
    if (args.empty())
    {
        context.out().Print("Usage: use <tv>  (number or part of the name from `devices`)");
        return;
    }
    if (Chromecasts(Devices(context)).empty())
    {
        Discover(context);
    }
    const auto device = FindDevice(context, args.front());
    if (!device)
    {
        context.out().Print("No TV {}. `devices` lists them.", args.front());
        return;
    }
    context.session().SelectDevice(device->receiverId);
    context.watcher.WaitUntil([&] { return Devices(context).selectedReceiverId == device->receiverId; }, 1s);
    context.out().Print("Using {}.", device->name);
}

void ShowTvStatus(CommandContext& context)
{
    const auto device = RequireDevice(context);
    if (!device)
    {
        return;
    }

    // The first status after selecting a TV takes a moment.
    context.watcher.WaitUntil([&] { return !NowPlaying(context).tvState.empty(); }, 4s);
    const auto status = NowPlaying(context);
    if (status.tvState.empty())
    {
        context.out().Print("{}: no answer.", device->name);
        return;
    }
    context.out().Print("{}: {}{}", device->name, status.tvState, status.tvMediaTitle.empty() ? "" : "  " + status.tvMediaTitle);
    if (status.durationSeconds > 0.0)
    {
        context.out().Print("  {} / {}", FormatTime(status.positionSeconds), FormatTime(status.durationSeconds));
    }
}

// Waits for the command the session is sending to finish, then shows the
// TV's state.
void WaitForCommand(CommandContext& context)
{
    context.watcher.WaitUntil([&] { return NowPlaying(context).commandInFlight; }, 1s);
    context.watcher.WaitUntil([&] { return !NowPlaying(context).commandInFlight; }, kCommandWait);
    context.out().Print("{}", NowPlaying(context).tvState);
}

// Pause and resume are one toggle in the session; each only sends it from
// the matching state.
void Toggle(CommandContext& context, const std::string& fromState)
{
    if (!RequireDevice(context))
    {
        return;
    }
    const auto state = NowPlaying(context).tvState;
    if (state != fromState)
    {
        context.out().Print("The TV is {}.", state.empty() ? "not answering yet" : state);
        return;
    }
    context.session().PlayPause();
    WaitForCommand(context);
}

void Seek(CommandContext& context, const Args& args)
{
    const auto position = args.empty() ? std::nullopt : ParseTime(args.front());
    if (!position)
    {
        context.out().Print("Usage: seek <time>  (1:02:03, 45:00 or seconds)");
        return;
    }
    if (RequireDevice(context))
    {
        context.session().Seek(*position);
        WaitForCommand(context);
    }
}

void StopPlayback(CommandContext& context)
{
    if (RequireDevice(context))
    {
        context.session().Stop();
        WaitForCommand(context);
    }
}

// In place when the TV can; otherwise the session reloads the movie where
// the TV is.
void SwitchSubtitles(CommandContext& context, const Args& args)
{
    const auto playing = NowPlaying(context);
    const auto track = args.empty() ? std::nullopt : args.front() == "off" ? std::optional(-1) : ParseInt(args.front());
    if (playing.fastHash.empty() || !track)
    {
        context.out().Print("{}", playing.fastHash.empty() ? "Nothing cast from here is playing."
                                                    : "Usage: sub <track|off>  (numbers from `info`)");
        return;
    }

    context.session().SelectSubtitleTrack(*track);
    const bool switched =
        context.watcher.WaitUntil([&] { return NowPlaying(context).subtitleTrackId == *track; }, 30s);
    context.out().Print("{}", switched ? "Switched." : "Not switched (see the log).");
}

}  // namespace

std::optional<mango::SessionDevice> RequireDevice(CommandContext& context)
{
    if (Devices(context).selectedReceiverId.empty() && !Devices(context).discovering)
    {
        Discover(context);
    }
    context.watcher.WaitUntil([&] { return !Devices(context).discovering; }, kDiscoveryWait);

    const auto state = Devices(context);
    const auto chromecasts = Chromecasts(state);
    const auto selected = std::ranges::find(chromecasts, state.selectedReceiverId, &mango::SessionDevice::receiverId);
    if (selected == chromecasts.end())
    {
        context.out().Print("{}", chromecasts.empty() ? "No Chromecasts found." : "No TV selected. Pick one with use <tv>.");
        return std::nullopt;
    }
    return *selected;
}

void RegisterCastCommands(CommandTable& table, CommandContext& context)
{
    table.Add({"devices", "", "Find Chromecasts on the network", [&context](const Args&) { ListDevices(context); }});
    table.Add({"use", "<tv>", "Select the TV to cast to", [&context](const Args& args) { UseDevice(context, args); }});
    table.Add({"tv", "", "What the selected TV is playing", [&context](const Args&) { ShowTvStatus(context); }});
    table.Add({"pause", "", "Pause the TV", [&context](const Args&) { Toggle(context, "Playing"); }});
    table.Add({"resume", "", "Resume the TV", [&context](const Args&) { Toggle(context, "Paused"); }});
    table.Add({"seek", "<time>", "Jump to a position", [&context](const Args& args) { Seek(context, args); }});
    table.Add({"sub", "<track|off>", "Switch subtitles while playing",
        [&context](const Args& args) { SwitchSubtitles(context, args); }});
    table.Add({"stop", "", "Stop playback on the TV", [&context](const Args&) { StopPlayback(context); }});
}

}  // namespace mango::cli
