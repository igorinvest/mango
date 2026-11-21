#pragma once

#include <optional>

// The tracks the user picked for a stream (see docs/scope/
// track-selection-scope.md, section 3). std::nullopt: the user picked
// nothing for that kind, so the engine picks (see TrackSelector).
// subtitleTrackId -1 is "off", which is a pick too.
struct TrackRequest
{
    std::optional<int> audioTrackId;
    std::optional<int> subtitleTrackId;

    bool operator==(const TrackRequest&) const = default;
};
