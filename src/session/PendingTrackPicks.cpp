#include "PendingTrackPicks.hpp"

namespace mango
{

void PendingTrackPicks::Pick(const std::string& fastHash, TrackKind kind, int trackId)
{
    auto& picks = m_picks[fastHash];
    (kind == TrackKind::audio ? picks.audioTrackId : picks.subtitleTrackId) = trackId;
}

std::optional<TrackRequest> PendingTrackPicks::Find(const std::string& fastHash) const
{
    const auto found = m_picks.find(fastHash);
    return found != m_picks.end() ? std::optional(found->second) : std::nullopt;
}

void PendingTrackPicks::Clear(const std::string& fastHash) { m_picks.erase(fastHash); }

}  // namespace mango
