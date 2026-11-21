#include "VideoFileTypes.hpp"

const std::vector<VideoFileType> &VideoFileTypes::GetSupportedTypes()
{
    static const std::vector<VideoFileType> types =
        {
            {".mkv", "video/x-matroska"},
            {".mp4", "video/mp4"},
            {".mov", "video/quicktime"},
            {".avi", "video/x-msvideo"},
            {".webm", "video/webm"},
            {".m2ts", "video/mp2t"},
            {".ts", "video/mp2t"}};

    return types;
}

std::string VideoFileTypes::GetMimeType(const std::string &extension)
{
    for (const auto &type : GetSupportedTypes())
    {
        if (type.extension == extension)
            return type.mimeType;
    }

    return "application/octet-stream";
}

std::string VideoFileTypes::GetFileDialogFilter()
{
    std::string filter;

    for (const auto &type : GetSupportedTypes())
    {
        if (!filter.empty())
            filter += ' ';

        filter += '*';
        filter += type.extension;
    }

    return filter;
}
