#pragma once

// File names inside a stream folder that the HTTP routes and the
// hand-written HLS master playlist (see StreamManager) depend on. Part of
// the MediaBackend contract: every backend writes its outputs under these
// names. The DASH manifest and the HLS master playlist themselves are named
// by the StreamProfile (see FileRecord::getStreamEntry).
namespace StreamOutputLayout
{
// The backend-produced HLS video/audio media playlist. Wrapped by the
// master playlist, so never itself the public stream entry point.
inline constexpr const char* kHlsMediaPlaylistName = "stream.m3u8";

// The companion WebVTT playlist written next to kHlsMediaPlaylistName when
// a subtitle track is muxed into an HLS stream.
inline constexpr const char* kHlsSubtitlePlaylistName = "stream_vtt.m3u8";
}  // namespace StreamOutputLayout
