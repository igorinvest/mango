#pragma once

// Cleans up long-lived background work (the active ffmpeg transcode, a
// library folder scan) when
// the process is actually going away — real termination only, distinct from
// Engine::Stop(), which just stops the HTTP listener and must leave a
// running transcode alone. Static-only: there's nothing per-instance here,
// StreamManager is already a singleton.
class ShutdownCoordinator
{
   public:
    ShutdownCoordinator() = delete;

    // Cancels any running library folder scan, then terminates the active
    // ffmpeg conversion (if any), blocking until both are gone, cleans up
    // the conversion's partial stream folder, and writes any playback
    // position not yet on disk (see PlaybackHistory::Flush). Safe to call from any
    // thread, including a signal-delivery queue.
    static void StopActiveStream();

    // Installs a handler for termination requests, which calls
    // StopActiveStream() then exits:
    // - macOS (ShutdownCoordinator_Apple.cpp): SIGTERM, for requests that
    //   never go through AppKit (a raw `kill -TERM`, launchd, logout).
    // - Linux (ShutdownCoordinator_Linux.cpp): SIGTERM (`systemctl stop`) and
    //   SIGINT (Ctrl+C in the console).
    // - Windows: a no-op.
    // Idempotent — safe to call more than once, only installs once.
    // SIGKILL can never be caught (POSIX guarantees this), so a force-quit
    // still leaves ffmpeg running as an orphan; there's no handling for it.
    static void InstallSignalHandler();
};
