#pragma once

#include <string>
#include <vector>

#include "../pipeline/stream/profiles/StreamProfile.hpp"

// The video codecs Mango can convert to, per protocol, best first. Adding
// VP8 to DASH or HEVC to HLS later is a change here plus its encoders.
class ConversionCodecs
{
   public:
    [[nodiscard]] static const std::vector<std::string>& ForPlaylist(PlaylistType playlist);
};
