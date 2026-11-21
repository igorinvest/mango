#include "commands/RemoteCommands.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

#include "console/ConsoleText.hpp"

namespace mango::cli
{

namespace
{

using namespace std::chrono_literals;

// The service answers within a second; connecting can take a few.
constexpr auto kInviteWait = 20s;
constexpr auto kPingWait = 5s;
constexpr auto kCommandWait = 2s;

std::string Lowercase(std::string text)
{
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string LinkText(const mango::RemotePeerState& peer)
{
    if (peer.link == "peerNeedsUpdate")
    {
        return peer.name + " needs an update";
    }
    if (peer.link == "updateRequired")
    {
        return "update Mango to connect";
    }
    if (peer.link == "connected" && peer.pingMs)
    {
        return std::format("connected, Mango {}, ping {} ms", peer.peerAppVersion, *peer.pingMs);
    }
    if (peer.link == "connected")
    {
        return std::format("connected, Mango {}", peer.peerAppVersion);
    }
    return peer.link;
}

std::string LibraryText(const mango::RemotePeerState& peer)
{
    if (peer.library == "none")
    {
        return {};
    }
    if (peer.library == "ready")
    {
        return std::format("  library: {} videos (`movies` lists them)", peer.libraryCount);
    }
    return "  library: " + peer.library;
}

// Which list a command works on: the host's clients or the client's
// sources.
enum class Side
{
    Clients,
    Sources,
};

const std::vector<mango::RemotePeerState>& PeersOf(const mango::RemoteState& state, Side side)
{
    return side == Side::Clients ? state.clients : state.sources;
}

void PrintPeers(Output& out, const std::string& title, const std::vector<mango::RemotePeerState>& peers, const std::string& empty)
{
    out.Print("  {}", title);
    if (peers.empty())
    {
        out.Print("    {}", empty);
        return;
    }
    for (std::size_t i = 0; i < peers.size(); ++i)
    {
        const auto& peer = peers[i];
        out.Print("    {}. {} ({})  [{}]{}", i + 1, peer.name, peer.deviceId.substr(0, 8), LinkText(peer),
            LibraryText(peer));
    }
}

void PrintPairing(Output& out, const std::string& label, const mango::RemotePairingState& pairing)
{
    if (pairing.stage != "idle")
    {
        out.Print("  {} {}{}", label, pairing.stage, pairing.link.empty() ? "" : "  " + pairing.link);
    }
}

void PrintState(Output& out, const mango::RemoteState& state)
{
    if (!state.available)
    {
        out.Print("Remote streaming is unavailable: this device's key couldn't be made (see the log).");
        return;
    }
    out.Print("  remote    {}   (signaling: {})", state.enabled ? "on" : "off", state.signaling);
    out.Print("  clients   {}", state.streamingAllowed ? "allowed" : "blocked (clients can't connect)");
    out.Print("  name      {}", state.deviceName);
    out.Print("  device    {}", state.deviceId);
    PrintPairing(out, "inviting ", state.hostPairing);
    PrintPairing(out, "joining  ", state.clientPairing);
    PrintPeers(out, "My clients (play this library on their TVs):", state.clients,
        "None. `pair` shows a link to `join` on the other computer.");
    PrintPeers(out, "My sources (their libraries play here):", state.sources,
        "None. `join <link>` with the link `pair` shows there.");
}

// A device in one list by its number in `remote`, its name, or the start
// of its id.
std::optional<mango::RemotePeerState> FindPeer(Output& out, const RemoteWatcher& watcher, Side side, const std::string& text)
{
    const auto peers = PeersOf(watcher.State(), side);
    if (const auto number = ParseInt(text); number && *number >= 1 && *number <= static_cast<int>(peers.size()))
    {
        return peers[static_cast<std::size_t>(*number - 1)];
    }

    const auto wanted = Lowercase(text);
    const auto found = std::ranges::find_if(peers, [&](const mango::RemotePeerState& peer) {
        return Lowercase(peer.name) == wanted || (wanted.size() >= 4 && peer.deviceId.starts_with(wanted));
    });
    if (found == peers.end())
    {
        out.Print("No {} {}. `remote` lists them.", side == Side::Clients ? "client" : "source", text);
        return std::nullopt;
    }
    return *found;
}

std::optional<mango::RemotePeerState> FindPeerById(const RemoteWatcher& watcher, Side side, const std::string& deviceId)
{
    const auto peers = PeersOf(watcher.State(), side);
    const auto found = std::ranges::find(peers, deviceId, &mango::RemotePeerState::deviceId);
    return found != peers.end() ? std::optional(*found) : std::nullopt;
}

std::optional<mango::RemotePeerState> PeerArgument(Output& out, const RemoteWatcher& watcher, Side side, const Args& args,
    const std::string& command)
{
    if (args.size() != 1)
    {
        out.Print("Usage: {} <device>  (a number from `remote`, a name or the start of an id)", command);
        return std::nullopt;
    }
    return FindPeer(out, watcher, side, args[0]);
}

void Remote(Output& out, RemoteWatcher& watcher, const Args& args)
{
    auto& remote = watcher.Remote();
    if (args.empty())
    {
        PrintState(out, watcher.State());
        return;
    }

    if (args[0] == "name" && args.size() >= 2)
    {
        std::string name = args[1];
        for (std::size_t i = 2; i < args.size(); ++i)
        {
            name += " " + args[i];
        }
        remote.SetDeviceName(name);
        out.Print("Paired devices will see this one as \"{}\" from the next pairing.", name);
        return;
    }

    if (args[0] == "allow" && args.size() == 2)
    {
        if (const auto allowed = ParseBool(args[1]))
        {
            remote.SetStreamingAllowed(*allowed);
            out.Print("Clients {} connect to this one.", *allowed ? "can" : "can't");
            return;
        }
    }

    const auto enabled = args.size() == 1 ? ParseBool(args[0]) : std::nullopt;
    if (!enabled)
    {
        out.Print("Usage: remote [on|off | allow on|off | name <name>]");
        return;
    }
    remote.SetEnabled(*enabled);
    out.Print("Remote streaming {}.", *enabled ? "on" : "off");
}

void Pair(Output& out, RemoteWatcher& watcher)
{
    watcher.Remote().OpenInvite();
    out.Print("Opening an invite ...");

    const bool opened = watcher.WaitUntil([&] { return !watcher.State().hostPairing.link.empty(); }, kInviteWait);
    const auto state = watcher.State();
    if (!opened)
    {
        out.Print("No invite: {}.", state.signaling == "ready" ? "the service didn't answer"
                                                                  : "can't reach the signaling service");
        return;
    }
    out.Print("On the other computer, run:\n\n  join {}\n\nThe invite works once, for 10 minutes. That "
                 "computer becomes a client: it plays this library on its TV.",
        state.hostPairing.link);
}

void Join(Output& out, RemoteWatcher& watcher, const Args& args)
{
    if (args.size() != 1 || !watcher.Remote().JoinInvite(args[0]))
    {
        out.Print("Usage: join mango://pair?s=...&d=...  (the link `pair` shows on the other computer)");
        return;
    }
    out.Print("Joining ...");
}

void Ping(Output& out, RemoteWatcher& watcher, Side side, const mango::RemotePeerState& peer)
{
    if (peer.link != "connected")
    {
        out.Print("{} isn't connected ({}).", peer.name, LinkText(peer));
        return;
    }

    const auto before = peer.pingMs;
    if (side == Side::Clients)
    {
        watcher.Remote().PingClient(peer.deviceId);
    }
    else
    {
        watcher.Remote().PingSource(peer.deviceId);
    }
    const auto current = [&] {
        const auto found = FindPeerById(watcher, side, peer.deviceId);
        return found ? found->pingMs : std::nullopt;
    };

    // The same round trip twice would look unchanged: good enough here.
    if (watcher.WaitUntil([&] { return current() && current() != before; }, kPingWait) || current())
    {
        out.Print("{}: {} ms", peer.name, *current());
        return;
    }
    out.Print("{} didn't answer.", peer.name);
}

void Remove(Output& out, RemoteWatcher& watcher, Side side, const Args& args)
{
    const auto command = side == Side::Clients ? "remove-client" : "remove-source";
    const auto peer = PeerArgument(out, watcher, side, args, command);
    if (!peer)
    {
        return;
    }
    if (side == Side::Clients)
    {
        watcher.Remote().RemoveClient(peer->deviceId);
    }
    else
    {
        watcher.Remote().RemoveSource(peer->deviceId);
    }
    watcher.WaitUntil([&] { return !FindPeerById(watcher, side, peer->deviceId); }, kCommandWait);
    out.Print("Removed {} from your {}.", peer->name, side == Side::Clients ? "clients" : "sources");
}

void RegisterSourceCommands(CommandTable& table, CommandContext& context)
{
    auto& watcher = context.remote;
    table.Add({"connect", "<source>", "Connect to a source now",
        [&context, &watcher](const Args& args) {
            if (const auto peer = PeerArgument(context.out(), watcher, Side::Sources, args, "connect"))
            {
                watcher.Remote().ConnectSource(peer->deviceId);
                context.out().Print("Connecting to {} ...", peer->name);
            }
        }});
    table.Add({"disconnect", "<source>", "Close the link to a source",
        [&context, &watcher](const Args& args) {
            if (const auto peer = PeerArgument(context.out(), watcher, Side::Sources, args, "disconnect"))
            {
                watcher.Remote().DisconnectSource(peer->deviceId);
            }
        }});
    table.Add({"refresh-remote", "<source>", "Load a source's library again",
        [&context, &watcher](const Args& args) {
            if (const auto peer = PeerArgument(context.out(), watcher, Side::Sources, args, "refresh-remote"))
            {
                watcher.Remote().RefreshLibrary(peer->deviceId);
                context.out().Print("Loading the library of {} ...", peer->name);
            }
        }});
}

void RegisterPeerCommands(CommandTable& table, CommandContext& context, Side side)
{
    auto& watcher = context.remote;
    const bool clients = side == Side::Clients;
    table.Add({clients ? "remove-client" : "remove-source", clients ? "<client>" : "<source>",
        clients ? "Stop a client from playing this library" : "Forget a source and its library",
        [&context, &watcher, side](const Args& args) { Remove(context.out(), watcher, side, args); }});
    table.Add({clients ? "ping-client" : "ping-source", clients ? "<client>" : "<source>",
        clients ? "Round trip over the link to a client" : "Round trip over the link to a source",
        [&context, &watcher, side](const Args& args) {
            const auto command = side == Side::Clients ? "ping-client" : "ping-source";
            if (const auto peer = PeerArgument(context.out(), watcher, side, args, command))
            {
                Ping(context.out(), watcher, side, *peer);
            }
        }});
}

}  // namespace

void RegisterRemoteCommands(CommandTable& table, CommandContext& context)
{
    auto& watcher = context.remote;

    table.Add({"remote", "[on|off | allow on|off | name <name>]",
        "Remote streaming: status, on/off, client connections, this device's name",
        [&context, &watcher](const Args& args) { Remote(context.out(), watcher, args); }});
    table.Add({"pair", "", "Let another Mango play this library: shows a link to `join` there",
        [&context, &watcher](const Args&) { Pair(context.out(), watcher); }});
    table.Add({"join", "<link>", "Play the library of the Mango that showed this link",
        [&context, &watcher](const Args& args) { Join(context.out(), watcher, args); }});
    RegisterPeerCommands(table, context, Side::Clients);
    RegisterPeerCommands(table, context, Side::Sources);
    RegisterSourceCommands(table, context);
}

}  // namespace mango::cli
