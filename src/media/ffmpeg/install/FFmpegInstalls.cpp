#include "FFmpegInstalls.hpp"

#include "../FFmpegStartupProbe.hpp"

const std::vector<FFmpegInstall>& FFmpegInstalls::All()
{
    return FFmpegStartupProbe::Get().Installs();
}

const FFmpegInstall* FFmpegInstalls::Primary()
{
    const auto& installs = All();
    return installs.empty() ? nullptr : &installs.front();
}

const FFmpegInstall* FFmpegInstalls::WithMuxer(std::string_view muxer)
{
    for (const auto& install : All())
    {
        if (install.HasMuxer(muxer))
        {
            return &install;
        }
    }
    return nullptr;
}
