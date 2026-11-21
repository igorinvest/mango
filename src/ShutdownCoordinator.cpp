#include "ShutdownCoordinator.hpp"

#include "log/Log.hpp"
#include "pipeline/library/Library.hpp"
#include "pipeline/playback/PlaybackHistory.hpp"
#include "pipeline/stream/StreamManager.hpp"

void ShutdownCoordinator::StopActiveStream()
{
    // Written before StopCurrentStream deletes the folder - and the log
    // with it. Debug builds still show it on stderr.
    Log::Info(LogCategory::Engine, "quitting");
    Log::Flush();

    Library::GetInstance().StopScanning();
    StreamManager::GetInstance().StopCurrentStream();
    PlaybackHistory::GetInstance().Flush();
}
