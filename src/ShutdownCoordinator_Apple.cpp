#include <dispatch/dispatch.h>

#include <csignal>
#include <cstdlib>
#include <mutex>

#include "ShutdownCoordinator.hpp"

// dispatch_source's DISPATCH_SOURCE_TYPE_SIGNAL delivers the signal to a
// normal GCD queue instead of a signal-handler context, so the handler below
// is free to do the non-signal-safe work (mutexes, filesystem, threads) that
// StopActiveStream() needs. The default disposition must be explicitly
// ignored first — otherwise the process still terminates immediately via the
// default action before the dispatch source ever fires.
void ShutdownCoordinator::InstallSignalHandler()
{
    static std::once_flag installed;

    std::call_once(installed, []
    {
        signal(SIGTERM, SIG_IGN);

        static dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL,
            SIGTERM, 0, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0));

        // dispatch_source_set_event_handler takes an Objective-C block, not
        // a C++ lambda — there's no implicit conversion between the two,
        // even in a .cpp file (Apple Clang still recognizes ^{} block syntax
        // outside Objective-C).
        dispatch_source_set_event_handler(source, ^{
            ShutdownCoordinator::StopActiveStream();
            std::exit(0);
        });

        dispatch_resume(source);
    });
}
