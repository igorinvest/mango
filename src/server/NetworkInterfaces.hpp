#pragma once

#include <string>
#include <vector>

// What an interface is, as far as picking the address a TV can reach goes.
enum class NetworkInterfaceKind
{
    Ethernet,
    WiFi,
    // VPN, PPP and other point-to-point links: no TV sits behind them.
    Tunnel,
    Loopback,
    // Can't be told apart (e.g. macOS's en0 is Ethernet on one Mac and Wi-Fi
    // on another).
    Other,
};

// One IPv4 address of an interface that is up.
struct NetworkInterface
{
    std::string name;
    std::string ipv4;
    NetworkInterfaceKind kind = NetworkInterfaceKind::Other;
};

// Every IPv4 address of every interface that is up, in the OS's order.
// Implemented per platform (NetworkInterfaces_Posix.cpp /
// NetworkInterfaces_Windows.cpp).
std::vector<NetworkInterface> ListNetworkInterfaces();
