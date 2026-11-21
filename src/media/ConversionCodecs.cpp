#include "ConversionCodecs.hpp"

const std::vector<std::string>& ConversionCodecs::ForPlaylist(PlaylistType playlist)
{
    static const std::vector<std::string> dash = {"h264"};
    static const std::vector<std::string> hls = {"h264"};
    return playlist == PlaylistType::HLS ? hls : dash;
}
