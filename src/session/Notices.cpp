#include "Notices.hpp"

#include <algorithm>
#include <array>

namespace mango::Notices
{

namespace
{

constexpr std::array kCatalog{
    NoticeKind{NoticeCode::StreamStartFailed, NoticeSeverity::Error},
    NoticeKind{NoticeCode::DeviceUnreachable, NoticeSeverity::Error},
    NoticeKind{NoticeCode::PlaybackFinished, NoticeSeverity::Info, NoticeRepeat::Window, true},
    NoticeKind{NoticeCode::PlaybackStopped, NoticeSeverity::Info},
    NoticeKind{NoticeCode::FileNotAdded, NoticeSeverity::Error},
    NoticeKind{NoticeCode::SettingsFileReset, NoticeSeverity::Warning, NoticeRepeat::OncePerLaunch},
    NoticeKind{NoticeCode::FfmpegMissing, NoticeSeverity::Error, NoticeRepeat::OncePerLaunch},
    NoticeKind{NoticeCode::Paired, NoticeSeverity::Info},
    NoticeKind{NoticeCode::PairingFailed, NoticeSeverity::Error},
    NoticeKind{NoticeCode::PeerConnectionFailed, NoticeSeverity::Error},
    NoticeKind{NoticeCode::PeerLinkLost, NoticeSeverity::Error, NoticeRepeat::Window, true},
    NoticeKind{NoticeCode::SettingsNotApplied, NoticeSeverity::Warning},
    NoticeKind{NoticeCode::RemoteStreamStarted, NoticeSeverity::Info, NoticeRepeat::Window, true},
    NoticeKind{NoticeCode::StreamStoppedByPeer, NoticeSeverity::Warning, NoticeRepeat::Window, true},
};

}  // namespace

std::span<const NoticeKind> All() { return kCatalog; }

const NoticeKind* Find(std::string_view code)
{
    const auto it = std::ranges::find(kCatalog, code, &NoticeKind::code);
    return it == kCatalog.end() ? nullptr : &*it;
}

std::string_view ToString(NoticeSeverity severity)
{
    switch (severity)
    {
        case NoticeSeverity::Info:
            return "info";
        case NoticeSeverity::Warning:
            return "warning";
        case NoticeSeverity::Error:
            return "error";
    }
    return "info";
}

}  // namespace mango::Notices
