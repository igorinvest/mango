#pragma once

#include "CurrentConversion.hpp"
#include "remote/streaming/RemoteHost.hpp"

class StreamingSession;

namespace mango
{

// Remote streaming's way into this engine on A, the library side: the
// library without local paths, stream starts with the peer's target, and
// the watch history of the peer's playback.
class EngineRemoteHost final : public remote::RemoteHost
{
   public:
    EngineRemoteHost(const Engine& engine, StreamingSession& session, CurrentConversion& conversion);

    std::vector<LibraryEntry> SharedLibrary() override;
    std::optional<std::filesystem::path> CoverFile(const std::string& fastHash) override;
    remote::HostedStream StartStream(
        const std::string& fastHash, const TrackRequest& tracks, const StreamTarget& target) override;
    std::optional<std::string> ServerUrl() override;
    std::string CurrentStreamFastHash() override;
    TranscodeStatus CurrentTranscode() override;
    std::string Title(const std::string& fastHash) override;
    void PlaybackStarted(const std::string& fastHash, double startSeconds) override;
    void ReportPlaybackProgress(
        const std::string& fastHash, double positionSeconds, int audioTrackId, int subtitleTrackId) override;
    void PlaybackEnded(const std::string& fastHash) override;

   private:
    const Engine& m_engine;
    StreamingSession& m_session;
    CurrentConversion& m_conversion;
};

}  // namespace mango
