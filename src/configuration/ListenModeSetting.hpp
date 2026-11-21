#pragma once

// "Listen only while streaming": whether the HTTP port opens only for a
// streaming session (see StreamingSession) or stays open while the engine
// runs. Stored in settings.json as "listenOnlyWhileStreaming" (default
// true). Off is for clients that start on their own, such as a player
// reloading a URL later; it leaves the port reachable on every network the
// machine joins.
class ListenModeSetting
{
   public:
    static constexpr bool defaultValue = true;

    static bool GetListenOnlyWhileStreaming();
    static void SetListenOnlyWhileStreaming(bool enabled);
};
