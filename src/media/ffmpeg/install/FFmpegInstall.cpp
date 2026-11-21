#include "FFmpegInstall.hpp"

#include <algorithm>

bool FFmpegInstall::HasMuxer(std::string_view muxer) const
{
    return muxers.contains(std::string(muxer));
}

bool FFmpegInstall::ListsEncoder(std::string_view codec, std::string_view encoder) const
{
    const auto it = encodersByCodec.find(std::string(codec));
    return it != encodersByCodec.end() && std::ranges::contains(it->second, encoder);
}

std::string FFmpegInstall::CodecOf(std::string_view encoder) const
{
    for (const auto& [codec, encoders] : encodersByCodec)
    {
        if (std::ranges::contains(encoders, encoder))
        {
            return codec;
        }
    }
    return {};
}

std::string_view FFmpegInstall::OriginName(Origin origin)
{
    switch (origin)
    {
        case Origin::bundled:
            return "bundled";
        case Origin::configured:
            return "configured";
        case Origin::systemPath:
        default:
            return "PATH";
    }
}
