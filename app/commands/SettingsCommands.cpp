#include "commands/SettingsCommands.hpp"

#include <algorithm>
#include <array>
#include <string_view>

#include "console/ConsoleText.hpp"

namespace mango::cli
{

namespace
{

constexpr std::array<std::string_view, 3> kPresets{"720p", "1080p", "4k"};
constexpr std::array<std::string_view, 3> kModes{"automatic", "forceCopy", "forceDecode"};

// "streaming": the port opens only for a stream; "always": open while the
// engine runs.
std::string_view ListenMode(const mango::Engine& engine)
{
    return engine.GetListenOnlyWhileStreaming() ? "streaming" : "always";
}

mango::SettingsState Settings(const CommandContext& context)
{
    return context.watcher.Read<mango::SettingsState>(mango::SessionPart::Settings);
}

void ShowSettings(const CommandContext& context)
{
    const auto& engine = context.engine;
    context.out().Print("  quality   {}   (720p, 1080p, 4k)", engine.GetEncodingPreset());
    context.out().Print("  mode      {}   (automatic, forceCopy, forceDecode)", engine.GetVideoTranscodeMode());
    context.out().Print("  listen    {}   (streaming: port open only while streaming, always)", ListenMode(engine));
    context.out().Print("  debuglog  {}   (on, off: debug lines in the log, from the next launch)",
        Settings(context).logDebug ? "on" : "off");
}

bool SetQuality(mango::Engine& engine, const std::string& value)
{
    if (!std::ranges::contains(kPresets, value))
    {
        return false;
    }
    engine.SetEncodingPreset(value);
    return true;
}

bool SetMode(mango::Engine& engine, const std::string& value)
{
    if (!std::ranges::contains(kModes, value))
    {
        return false;
    }
    engine.SetVideoTranscodeMode(value);
    return true;
}

bool SetListen(mango::Engine& engine, const std::string& value)
{
    if (value != "streaming" && value != "always")
    {
        return false;
    }
    engine.SetListenOnlyWhileStreaming(value == "streaming");
    return true;
}

bool SetDebugLog(CommandContext& context, const std::string& value)
{
    const auto enabled = ParseBool(value);
    if (!enabled)
    {
        return false;
    }
    context.session().SetLogDebug(*enabled);
    return true;
}

void Set(CommandContext& context, const Args& args)
{
    if (args.size() != 2)
    {
        context.out().Print("Usage: set quality|mode|listen|debuglog <value>  (`settings` shows the values)");
        return;
    }

    auto& engine = context.engine;
    const auto& name = args[0];
    const auto& value = args[1];
    if (name == "debuglog")
    {
        context.out().Print("{}", SetDebugLog(context, value) ? "Saved. Applies at the next launch." : "Unknown value.");
        return;
    }
    if (name == "listen")
    {
        context.out().Print("{}", SetListen(engine, value) ? "Saved. Applies now." : "Unknown value.");
        return;
    }

    const bool applied = name == "quality" ? SetQuality(engine, value)
                         : name == "mode"  ? SetMode(engine, value)
                                           : false;
    context.out().Print("{}", applied ? "Saved. Applies to the next stream." : "Unknown setting or value.");
}

}  // namespace

void RegisterSettingsCommands(CommandTable& table, CommandContext& context)
{
    table.Add({"settings", "", "Show settings",
        [&context](const Args&) { ShowSettings(context); }});
    table.Add({"set", "<name> <value>", "Change a setting",
        [&context](const Args& args) { Set(context, args); }});
}

}  // namespace mango::cli
