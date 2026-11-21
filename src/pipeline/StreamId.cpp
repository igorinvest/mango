#include "StreamId.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <random>

std::string StreamId::Generate()
{
    // libc++, libstdc++ and MSVC all back std::random_device with the OS
    // CSPRNG (arc4random, getrandom or /dev/urandom, rand_s).
    std::random_device device;

    std::string streamId;
    for (int i = 0; i < 4; ++i)
    {
        streamId += std::format("{:08x}", static_cast<std::uint32_t>(device()));
    }
    return streamId;
}

std::string StreamId::EntryPath(const std::string& streamId, const std::string& fileName)
{
    return streamId + "/" + fileName;
}

std::optional<std::string> StreamId::FromStreamUrl(std::string_view url)
{
    constexpr std::string_view marker = "/stream/";
    const auto start = url.find(marker);
    if (start == std::string_view::npos)
    {
        return std::nullopt;
    }

    const auto idStart = start + marker.size();
    const auto idEnd = url.find('/', idStart);
    if (idEnd == std::string_view::npos || idEnd == idStart)
    {
        return std::nullopt;
    }

    return std::string(url.substr(idStart, idEnd - idStart));
}
