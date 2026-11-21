#pragma once

#include <string>
#include <vector>

struct VideoFileType
{
    std::string extension;
    std::string mimeType;
};

class VideoFileTypes
{
public:
    static const std::vector<VideoFileType> &GetSupportedTypes();

    static std::string GetMimeType(const std::string &extension);

    static std::string GetFileDialogFilter();
};
