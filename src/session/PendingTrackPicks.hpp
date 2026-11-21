#pragma once

#include <map>
#include <optional>
#include <string>

#include "Engine.hpp"

namespace mango
{

// Which kind of track a pick is for.
enum class TrackKind
{
    audio,
    subtitle
};

// Tracks picked on a file's card that haven't been played yet, per
// fastHash: Play sends them as chosen (see TrackRequest). Removed when that
// file's stream starts — from then on the engine's playback history has
// them. In memory only. Session thread only.
class PendingTrackPicks
{
   public:
    // trackId -1 for subtitles: off.
    void Pick(const std::string& fastHash, TrackKind kind, int trackId);

    // The picks for fastHash; a kind not picked stays std::nullopt.
    [[nodiscard]] std::optional<TrackRequest> Find(const std::string& fastHash) const;

    void Clear(const std::string& fastHash);

    [[nodiscard]] const std::map<std::string, TrackRequest>& All() const { return m_picks; }

   private:
    std::map<std::string, TrackRequest> m_picks;
};

}  // namespace mango
