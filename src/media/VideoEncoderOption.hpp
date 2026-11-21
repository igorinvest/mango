#pragma once

#include <string>

// One way the backend can convert video for a protocol: what it makes and
// with what. GetSupportedFormatsJson lists them as "conversion".
struct VideoEncoderOption
{
    std::string codec;    // the output codec: "h264"
    std::string driver;   // the backend's name for how it runs encoder: "videotoolbox-sw"
    std::string encoder;  // "h264_videotoolbox"
    bool software{false};
    // Runs in a tool the user installed rather than the bundled one.
    bool external{false};
};
