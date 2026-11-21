#include "NetworkInterfaces.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <array>
#include <string_view>
#include <vector>

#include "../file/File.hpp"

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace
{

// Wintun (WireGuard), TAP-Windows (OpenVPN) and most VPN clients' adapters
// report themselves as Ethernet; only the description gives them away.
bool LooksLikeVpn(std::wstring_view description)
{
    for (const auto marker : {L"VPN", L"TAP-", L"Wintun", L"WireGuard", L"Tailscale", L"ZeroTier"})
    {
        if (description.find(marker) != std::wstring_view::npos)
        {
            return true;
        }
    }
    return false;
}

NetworkInterfaceKind KindOf(const IP_ADAPTER_ADDRESSES& adapter)
{
    switch (adapter.IfType)
    {
        case IF_TYPE_SOFTWARE_LOOPBACK: return NetworkInterfaceKind::Loopback;
        case IF_TYPE_TUNNEL:
        case IF_TYPE_PPP:
        case IF_TYPE_PROP_VIRTUAL: return NetworkInterfaceKind::Tunnel;
        case IF_TYPE_IEEE80211: return NetworkInterfaceKind::WiFi;
        case IF_TYPE_ETHERNET_CSMACD:
            return LooksLikeVpn(adapter.Description) ? NetworkInterfaceKind::Tunnel : NetworkInterfaceKind::Ethernet;
        default: return NetworkInterfaceKind::Other;
    }
}

std::string ToString(const SOCKET_ADDRESS& address)
{
    std::array<char, INET_ADDRSTRLEN> buffer{};
    const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address.lpSockaddr);
    return inet_ntop(AF_INET, &ipv4->sin_addr, buffer.data(), buffer.size()) ? buffer.data() : std::string();
}

// GetAdaptersAddresses' list, grown until it fits.
std::vector<unsigned char> ReadAdapters()
{
    constexpr ULONG Flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buffer;

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        buffer.resize(size);
        const auto result = GetAdaptersAddresses(
            AF_INET, Flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
        if (result == NO_ERROR)
        {
            return buffer;
        }
        if (result != ERROR_BUFFER_OVERFLOW)
        {
            break;
        }
    }
    return {};
}

}  // namespace

std::vector<NetworkInterface> ListNetworkInterfaces()
{
    auto buffer = ReadAdapters();
    if (buffer.empty())
    {
        return {};
    }

    std::vector<NetworkInterface> interfaces;
    for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); adapter != nullptr;
         adapter = adapter->Next)
    {
        if (adapter->OperStatus != IfOperStatusUp)
        {
            continue;
        }

        const auto name = File::ToUtf8(std::filesystem::path(adapter->FriendlyName));
        for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next)
        {
            auto ipv4 = ToString(unicast->Address);
            if (!ipv4.empty())
            {
                interfaces.push_back({name, std::move(ipv4), KindOf(*adapter)});
            }
        }
    }
    return interfaces;
}
