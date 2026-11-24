#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <string>

#include "Engine.hpp"

namespace mango
{

// The conversion of the stream started last, wherever it runs: on this
// computer (StreamManager) or, for a paired peer's video, on that peer,
// which reports its progress over the link (see RemotePlayback::Stream::
// transcode). Answers Engine's status and start-readiness calls. Any
// thread.
class CurrentConversion
{
   public:
    // The latest status a peer reported for entryId ("remote:..."), with
    // fastHash set to entryId; std::nullopt until it reported one.
    using RemoteStatusFn = std::function<std::optional<TranscodeStatus>(const std::string& entryId)>;

    explicit CurrentConversion(RemoteStatusFn remoteStatus);

    // A stream started on this computer: for this device's TV or a peer's.
    void StartedLocal();
    // entryId started on its peer.
    void StartedRemote(const std::string& entryId);

    [[nodiscard]] TranscodeStatus Status() const;

    // Far enough along that the player may have it (see
    // StreamStartDelaySetting::ReadyPercent).
    [[nodiscard]] bool IsReady() const;

    // This computer's conversion, whatever started last.
    [[nodiscard]] static TranscodeStatus LocalStatus();

   private:
    [[nodiscard]] std::string RemoteEntry() const;

    const RemoteStatusFn m_remoteStatus;

    mutable std::mutex m_mutex;
    // Empty: the current conversion is local.
    std::string m_remoteEntry;
};

}  // namespace mango
