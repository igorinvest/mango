#pragma once

#include <string>

struct VideoTargetSpec
{
    bool copy{false};
    // The encoder ("h264_videotoolbox"), or "copy".
    std::string codec;
    std::string preset;
    std::string bitrate;
    std::string pix_fmt;
    // Box the converted output is scaled down to fit, long side to long side
    // (see FFmpegArgumentFormatter::BuildScaleFilter). Both 0: no scaling.
    int maxWidth{0};
    int maxHeight{0};
    // The source is interlaced: the converted output is deinterlaced first
    // (see FFmpegArgumentFormatter::BuildVideoFilter).
    bool deinterlace{false};
    // The backend's driver for codec ("videotoolbox-sw", see
    // VideoEncoderOption). Empty: codec run with standard arguments.
    std::string driver{};
};

struct AudioTargetSpec
{
    bool copy{false};
    std::string codec;
    std::string bitrate;
};

struct SubtitleTargetSpec
{
    std::string codec;
    std::string bitrate;
};

struct StreamTargetSpecs
{
    VideoTargetSpec video;
    AudioTargetSpec audio;
    SubtitleTargetSpec subtitle;
};
