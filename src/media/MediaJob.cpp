#include "MediaJob.hpp"

std::string ConversionErrors::ToString(ConversionError error)
{
    switch (error)
    {
        case ConversionError::NoVideoEncoder:
            return "noVideoEncoder";
        case ConversionError::FfmpegMissing:
            return "ffmpegMissing";
        case ConversionError::UnsupportedCodec:
            return "unsupportedCodec";
        case ConversionError::FileUnreadable:
            return "fileUnreadable";
        case ConversionError::OutputFailed:
            return "outputFailed";
        case ConversionError::Cancelled:
            return "cancelled";
        case ConversionError::Unknown:
        default:
            return "unknown";
    }
}
