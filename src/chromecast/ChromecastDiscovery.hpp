#pragma once

#include <string>
#include <vector>

namespace mango
{

// A single Chromecast (Google Cast) receiver found via mDNS discovery.
struct ChromecastDevice
{
    std::string name;
    std::string ipAddress;
    int port = 0;

    // The TXT "id": a UUID that survives renames and IP changes. Empty
    // when the TV doesn't send one.
    std::string castId;
};

// Outcome of one DiscoverAndPrint call. `succeeded` is false when the mDNS
// query itself couldn't be sent (socket/network setup failure) - distinct
// from a query that went out fine but simply found no receivers, which is
// `succeeded == true` with an empty `devices`.
struct ChromecastDiscoveryResult
{
    std::vector<ChromecastDevice> devices;
    bool succeeded = true;
};

// Discovers Chromecast receivers on the local network by sending an mDNS
// query for "_googlecast._tcp.local" (RFC 6763 DNS-SD) and printing what
// answers back.
class ChromecastDiscovery
{
   public:
    // Sends the query, listens for `timeoutMs` milliseconds, prints every
    // device found, and returns the list plus whether the query could be
    // sent at all. Blocks the calling thread for the duration of the
    // timeout — callers running this at app startup should do so on a
    // background thread.
    static ChromecastDiscoveryResult DiscoverAndPrint(int timeoutMs = 3000);
};

}  // namespace mango
