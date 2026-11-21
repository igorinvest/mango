#include "MediaBackend.hpp"

#include <mutex>

#include "ffmpeg/FfmpegBackend.hpp"

namespace
{
std::mutex backendMutex;
std::unique_ptr<MediaBackend> backend;
}  // namespace

MediaBackend& MediaBackends::Get()
{
    std::lock_guard lock(backendMutex);
    if (!backend)
    {
        backend = std::make_unique<FfmpegBackend>();
    }
    return *backend;
}

void MediaBackends::SetForTesting(std::unique_ptr<MediaBackend> replacement)
{
    std::lock_guard lock(backendMutex);
    backend = std::move(replacement);
}
