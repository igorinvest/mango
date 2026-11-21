#pragma once

#include <span>
#include <string_view>

namespace mango
{

// How much a notice matters; the apps pick the banner's look and how long
// it stays from it (see docs/scope/user-notifications-scope.md).
enum class NoticeSeverity
{
    Info,
    Warning,
    Error
};

// When the same notice (code, params and file) is shown again.
enum class NoticeRepeat
{
    // Not again within SessionTimings::noticeRepeatWindow (30 s).
    Window,
    // Once per launch.
    OncePerLaunch
};

// One code's catalog entry. A code can only be shown with the severity and
// rules set here, so every app treats it the same.
struct NoticeKind
{
    std::string_view code;
    NoticeSeverity severity = NoticeSeverity::Info;
    NoticeRepeat repeat = NoticeRepeat::Window;
    // May be sent as a system notification while the app's window isn't
    // active.
    bool systemNotification = false;
};

// The codes. Adding one means adding it here, to the catalog in
// Notices.cpp, and a text in each app.
namespace NoticeCode
{
// params: error (the StreamInitiation error), title.
inline constexpr std::string_view StreamStartFailed = "streamStartFailed";
// params: device.
inline constexpr std::string_view DeviceUnreachable = "deviceUnreachable";
// params: title.
inline constexpr std::string_view PlaybackFinished = "playbackFinished";
// params: title, device.
inline constexpr std::string_view PlaybackStopped = "playbackStopped";
// params: path.
inline constexpr std::string_view FileNotAdded = "fileNotAdded";
// params: file, backup (file names in the settings folder).
inline constexpr std::string_view SettingsFileReset = "settingsFileReset";
// No ffmpeg was found at startup (Linux only: macOS and Windows always have
// the bundled one). No params.
inline constexpr std::string_view FfmpegMissing = "ffmpegMissing";

// Remote streaming (see "Notices" in docs/scope/hybrid_streaming_architecture.md).
// peer is the other Mango's name.
// params: peer.
inline constexpr std::string_view Paired = "paired";
// params: reason ("notFound", "expired", "offline", "wrongPeer", "badProof", ...).
inline constexpr std::string_view PairingFailed = "pairingFailed";
// params: peer.
inline constexpr std::string_view PeerConnectionFailed = "peerConnectionFailed";
// params: peer.
inline constexpr std::string_view PeerLinkLost = "peerLinkLost";
// params: peer, items (comma-separated codecs, presets or field names).
inline constexpr std::string_view SettingsNotApplied = "settingsNotApplied";
// params: peer, file.
inline constexpr std::string_view RemoteStreamStarted = "remoteStreamStarted";
// params: peer, file.
inline constexpr std::string_view StreamStoppedByPeer = "streamStoppedByPeer";
}  // namespace NoticeCode

namespace Notices
{

[[nodiscard]] std::span<const NoticeKind> All();

// nullptr for a code that isn't in the catalog.
[[nodiscard]] const NoticeKind* Find(std::string_view code);

// "info", "warning", "error".
[[nodiscard]] std::string_view ToString(NoticeSeverity severity);

}  // namespace Notices

}  // namespace mango
