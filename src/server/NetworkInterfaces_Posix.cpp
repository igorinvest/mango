#include "NetworkInterfaces.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#include <array>
#include <string_view>

namespace
{

bool StartsWithAny(std::string_view name, std::initializer_list<std::string_view> prefixes)
{
    for (const auto prefix : prefixes)
    {
        if (name.starts_with(prefix))
        {
            return true;
        }
    }
    return false;
}

// Only names say what an interface is here. Linux names Ethernet and Wi-Fi
// apart (eth*/en*, wl*); macOS calls both en*.
NetworkInterfaceKind KindFromName(std::string_view name, unsigned int flags)
{
    if ((flags & IFF_LOOPBACK) != 0)
    {
        return NetworkInterfaceKind::Loopback;
    }
    if ((flags & IFF_POINTOPOINT) != 0 ||
        StartsWithAny(name, {"utun", "tun", "tap", "wg", "ppp", "ipsec", "gif", "stf", "zt", "tailscale"}))
    {
        return NetworkInterfaceKind::Tunnel;
    }
#ifdef __linux__
    if (StartsWithAny(name, {"wl"}))
    {
        return NetworkInterfaceKind::WiFi;
    }
    if (StartsWithAny(name, {"eth", "en"}))
    {
        return NetworkInterfaceKind::Ethernet;
    }
#endif
    return NetworkInterfaceKind::Other;
}

bool IsUsable(const ifaddrs& entry)
{
    constexpr unsigned int Up = IFF_UP | IFF_RUNNING;
    return entry.ifa_addr != nullptr && entry.ifa_addr->sa_family == AF_INET && (entry.ifa_flags & Up) == Up;
}

std::string ToString(const sockaddr* address)
{
    std::array<char, INET_ADDRSTRLEN> buffer{};
    const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address);
    return inet_ntop(AF_INET, &ipv4->sin_addr, buffer.data(), buffer.size()) ? buffer.data() : std::string();
}

}  // namespace

std::vector<NetworkInterface> ListNetworkInterfaces()
{
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0)
    {
        return {};
    }

    std::vector<NetworkInterface> interfaces;
    for (const ifaddrs* entry = list; entry != nullptr; entry = entry->ifa_next)
    {
        if (!IsUsable(*entry))
        {
            continue;
        }

        auto ipv4 = ToString(entry->ifa_addr);
        if (!ipv4.empty())
        {
            interfaces.push_back({entry->ifa_name, std::move(ipv4), KindFromName(entry->ifa_name, entry->ifa_flags)});
        }
    }

    freeifaddrs(list);
    return interfaces;
}
