#pragma once

#include <string>
#include <vector>

#include "NetworkInterfaces.hpp"

class NetworkUtils
{
public:
    // The address a TV on the same network can reach this machine at — the
    // one stream URLs carry. Skips loopback, link-local and VPN/PPP tunnels;
    // prefers the interface carrying the default route, otherwise Ethernet
    // over Wi-Fi. Falls back to the default route's address when nothing
    // else qualifies. Read fresh on every call: it changes with the network.
    static std::string GetLanIPAddress();

    // GetLanIPAddress's choice among interfaces, given the default route's
    // address (empty if unknown).
    static std::string ChooseLanAddress(
        const std::vector<NetworkInterface>& interfaces, const std::string& defaultRouteAddress);

    // Whether ipv4 is one of this machine's own addresses, on any interface.
    static bool IsThisMachine(const std::string& ipv4);

    // The local address the OS routes toward the internet from — with a
    // full-tunnel VPN, the tunnel's.
    static std::string GetDefaultRouteAddress();
    static std::string Get_mime_type(const std::string &filename);

    // Reads the starting port from Settings (defaulting to 8080) and returns
    // the first port from there upward that isn't already bound.
    static int FindAvailablePort();
};
