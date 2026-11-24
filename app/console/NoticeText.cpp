#include "console/NoticeText.hpp"

#include <format>
#include <string_view>

#include "session/Notices.hpp"

namespace mango::cli
{

namespace
{

std::string Param(const mango::SessionNotice& notice, const std::string& name, std::string_view fallback)
{
    const auto it = notice.params.find(name);
    return it == notice.params.end() || it->second.empty() ? std::string(fallback) : it->second;
}

std::string GeneralText(const mango::SessionNotice& notice)
{
    if (notice.severity == "error")
    {
        return "Something went wrong.";
    }
    return notice.severity == "warning" ? "Something didn't work fully." : "Done.";
}

// StreamInitiation::error (see ConversionErrors::ToString), as a reason.
std::string StreamStartReason(const std::string& error)
{
    if (error == "noVideoEncoder")
    {
        return "no video encoder on this computer can convert it for this TV "
               "(files in a format the TV supports still play)";
    }
    if (error == "ffmpegMissing")
    {
        return "ffmpeg wasn't found";
    }
    return error;
}

std::string StopText(const mango::SessionNotice& notice)
{
    const auto device = Param(notice, "device", "");
    if (device.empty())
    {
        return std::format("{} stopped.", Param(notice, "title", "Playback"));
    }
    return std::format("Playback stopped on {}.", device);
}

std::string PairingFailureText(const std::string& reason)
{
    if (reason == "notFound" || reason == "expired")
    {
        return "the invite expired or was already used. Make a new one with `pair`.";
    }
    if (reason == "offline")
    {
        return "the other Mango isn't connected. Keep it open and try again.";
    }
    if (reason == "wrongPeer" || reason == "badProof")
    {
        return "the other device couldn't be confirmed. Make a new invite.";
    }
    return reason + ".";
}

// The texts from "Notices" in docs/scope/hybrid_streaming_architecture.md;
// empty for a code that isn't remote.
std::string RemoteText(const mango::SessionNotice& notice)
{
    namespace Code = mango::NoticeCode;
    const auto& code = notice.code;
    const auto peer = Param(notice, "peer", "the other Mango");
    if (code == Code::Paired)
    {
        return std::format("Paired with {}.", peer);
    }
    if (code == Code::PairingFailed)
    {
        return "Pairing didn't finish: " + PairingFailureText(Param(notice, "reason", "unknown"));
    }
    if (code == Code::PeerConnectionFailed)
    {
        return std::format("Can't reach {} directly. Its network may block it.", peer);
    }
    if (code == Code::SettingsNotApplied)
    {
        return std::format("{} runs an older Mango. Not applied: {}.", peer, Param(notice, "items", "some settings"));
    }
    if (code == Code::RemoteStreamStarted)
    {
        return std::format("{} is playing {}.", peer, Param(notice, "file", "a video"));
    }
    if (code == Code::StreamStoppedByPeer)
    {
        return std::format("{} stopped because {} started a stream.", Param(notice, "file", "The video"), peer);
    }
    return {};
}

std::string Text(const mango::SessionNotice& notice)
{
    if (auto remote = RemoteText(notice); !remote.empty())
    {
        return remote;
    }
    namespace Code = mango::NoticeCode;
    const auto& code = notice.code;
    if (code == Code::StreamStartFailed)
    {
        return std::format(
            "Couldn't start {}: {}.", Param(notice, "title", "the video"),
            StreamStartReason(Param(notice, "error", "unknown")));
    }
    if (code == Code::DeviceUnreachable)
    {
        return std::format("{} isn't answering.", Param(notice, "device", "The TV"));
    }
    if (code == Code::PlaybackFinished)
    {
        return std::format("{} finished.", Param(notice, "title", "The video"));
    }
    if (code == Code::PlaybackStopped)
    {
        return StopText(notice);
    }
    if (code == Code::FileNotAdded)
    {
        return std::format("Couldn't add {} to the library.", Param(notice, "path", "the file"));
    }
    if (code == Code::FfmpegMissing)
    {
        return "ffmpeg wasn't found. Install it (for example `sudo apt install ffmpeg`) "
               "or set ffmpegFolder in settings.json, then restart Mango.";
    }
    if (code == Code::SettingsFileReset)
    {
        const auto file = Param(notice, "file", "A settings file");
        return std::format("{} couldn't be read and was reset. Your version is saved as {}.", file,
            Param(notice, "backup", file + ".invalid"));
    }
    return GeneralText(notice);
}

}  // namespace

std::string NoticeLine(const mango::SessionNotice& notice)
{
    return std::format("[{}] {}", notice.severity, Text(notice));
}

std::string NoticeText(const mango::SessionNotice& notice) { return Text(notice); }

}  // namespace mango::cli
