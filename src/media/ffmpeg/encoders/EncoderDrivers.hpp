#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "EncoderDriver.hpp"

// Every encoder driver Mango knows, in priority order: hardware first, then
// the platform's software fallback.
//
//   macOS:   videotoolbox, videotoolbox-sw (h264_videotoolbox -allow_sw 1)
//   Windows: nvenc, qsv, amf, mediafoundation-sw (h264_mf -hw_encoding 0)
//   Linux:   vaapi, nvenc, qsv, x264 (libx264, from a system ffmpeg)
class EncoderDrivers
{
   public:
    // The drivers this platform tries, in order.
    [[nodiscard]] static std::vector<const EncoderDriver*> ForThisPlatform();

    // A driver by Name(), or nullptr.
    [[nodiscard]] static const EncoderDriver* Find(std::string_view name);

   private:
    [[nodiscard]] static std::vector<std::unique_ptr<EncoderDriver>> Create();
    [[nodiscard]] static const std::vector<std::unique_ptr<EncoderDriver>>& All();
};
