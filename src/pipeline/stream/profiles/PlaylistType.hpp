#pragma once

// The kind of stream a player takes: DASH for Chromecast, HLS for
// AirPlay/AVPlayer. Part of the Engine API, so a new protocol (DLNA) is a
// new value here rather than another flag.
enum class PlaylistType
{
    DASH,
    HLS
};
