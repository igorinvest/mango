#include "ChromecastDiscovery.hpp"

#include <mdns.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <sys/select.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <mutex>
#include <set>
#include <unordered_map>

#include "../log/Log.hpp"
#include "../server/NetworkUtils.hpp"

namespace mango
{

namespace
{

constexpr const char* kServiceName = "_googlecast._tcp.local";

// mdns.h hands out sockets as `int`, while winsock's FD_SET compares
// against the unsigned SOCKET type.
void AddToFdSet(int sock, fd_set& set)
{
#ifdef _WIN32
    FD_SET(static_cast<SOCKET>(sock), &set);
#else
    FD_SET(sock, &set);
#endif
}

// Accumulates records across every packet received during one discovery
// pass. SRV and TXT records for the same device may arrive in either
// order (or in separate packets), so the TXT "fn" (friendly name) and "id"
// are looked up only once discovery finishes.
struct DiscoveryContext
{
    std::vector<ChromecastDevice> devices;              // name holds the raw instance name until finalized
    std::unordered_map<std::string, std::string> friendlyNames;  // instance name -> TXT "fn" value
    std::unordered_map<std::string, std::string> castIds;        // instance name -> TXT "id" value
};

// DNS names decoded by mdns_string_extract carry the trailing root label
// dot (e.g. "...local."); strip it before matching/using the name.
std::string TrimTrailingDot(const std::string& name)
{
    if (!name.empty() && name.back() == '.')
    {
        return name.substr(0, name.size() - 1);
    }

    return name;
}

// Strips the "._googlecast._tcp.local" suffix off an SRV record's owner
// name. Chromecasts advertise a machine-generated instance name here
// (e.g. "Smart-TV-Pro-<hash>"); the human-readable name, when set, lives
// in the TXT record's "fn" key instead — see DiscoveryContext::friendlyNames.
std::string StripServiceSuffix(const std::string& instanceName)
{
    const std::string suffix = std::string(".") + kServiceName;

    if (instanceName.size() > suffix.size() &&
        instanceName.compare(instanceName.size() - suffix.size(), suffix.size(), suffix) == 0)
    {
        return instanceName.substr(0, instanceName.size() - suffix.size());
    }

    return instanceName;
}

// The SRV record only carries the port; the device's IP is taken from the
// UDP packet's source address rather than a separate A record lookup —
// that's the address the device itself answered from.
std::string SourceAddressToString(const sockaddr* from)
{
    if (from->sa_family != AF_INET)
    {
        return {};
    }

    char buffer[INET_ADDRSTRLEN]{};
    const auto* addr = reinterpret_cast<const sockaddr_in*>(from);

    if (inet_ntop(AF_INET, &addr->sin_addr, buffer, sizeof(buffer)) == nullptr)
    {
        return {};
    }

    return buffer;
}

// Keeps the TXT "fn" and "id" values of instanceName.
void ReadTxtRecord(DiscoveryContext& context, const std::string& instanceName, const void* data, size_t size,
    size_t recordOffset, size_t recordLength)
{
    mdns_record_txt_t txtRecords[16];
    const size_t count = mdns_record_parse_txt(data, size, recordOffset, recordLength, txtRecords, 16);

    for (size_t i = 0; i < count; ++i)
    {
        const std::string key(txtRecords[i].key.str, txtRecords[i].key.length);
        std::string value(txtRecords[i].value.str, txtRecords[i].value.length);

        if (key == "fn")
        {
            context.friendlyNames[instanceName] = std::move(value);
        }
        else if (key == "id")
        {
            context.castIds[instanceName] = std::move(value);
        }
    }
}

int HandleRecord(int /*sock*/, const sockaddr* from, size_t /*addrlen*/, mdns_entry_type_t /*entry*/,
    uint16_t /*queryId*/, uint16_t rtype, uint16_t /*rclass*/, uint32_t /*ttl*/, const void* data, size_t size,
    size_t nameOffset, size_t /*nameLength*/, size_t recordOffset, size_t recordLength, void* userData)
{
    auto* context = static_cast<DiscoveryContext*>(userData);

    char ownerNameBuffer[256];
    size_t ownerOffset = nameOffset;
    const mdns_string_t ownerName =
        mdns_string_extract(data, size, &ownerOffset, ownerNameBuffer, sizeof(ownerNameBuffer));
    const std::string instanceName = TrimTrailingDot(std::string(ownerName.str, ownerName.length));

    if (rtype == MDNS_RECORDTYPE_SRV)
    {
        char targetNameBuffer[256];
        const mdns_record_srv_t srv = mdns_record_parse_srv(
            data, size, recordOffset, recordLength, targetNameBuffer, sizeof(targetNameBuffer));

        ChromecastDevice device;
        device.name = instanceName;
        device.port = srv.port;
        device.ipAddress = SourceAddressToString(from);

        const bool alreadyKnown = std::any_of(context->devices.begin(), context->devices.end(),
            [&device](const ChromecastDevice& existing)
            { return existing.name == device.name && existing.ipAddress == device.ipAddress; });

        if (!alreadyKnown)
        {
            context->devices.push_back(std::move(device));
        }
    }
    else if (rtype == MDNS_RECORDTYPE_TXT)
    {
        ReadTxtRecord(*context, instanceName, data, size, recordOffset, recordLength);
    }

    return 0;
}

// Opens the mDNS socket and sends the discovery query. On success, fills in
// `sock`/`queryId` and returns true; on any failure it logs why, cleans up
// after itself, and returns false so the caller can report a real error
// instead of silently treating it as "no devices found".
bool TryOpenSocketAndSendQuery(std::vector<char>& buffer, int& sock, int& queryId)
{
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        Log::Error(LogCategory::Discovery, "failed to initialize sockets");
        return false;
    }
#endif

    // Binding to a specific interface (rather than leaving it to INADDR_ANY)
    // avoids the OS picking the wrong adapter to join the multicast group
    // on when several are active (VPN, Hyper-V/WSL vEthernet, Bluetooth
    // PAN, ...) — a real-world gap on multi-adapter Windows machines in
    // particular. Falls back to INADDR_ANY if the local IP can't be found.
    sockaddr_in localInterface{};
    localInterface.sin_family = AF_INET;
    localInterface.sin_port = 0;  // ephemeral port — same one-shot-query mode as passing nullptr

    // Read per query, so discovery follows the machine onto a new network.
    const std::string localIp = NetworkUtils::GetLanIPAddress();
    const bool hasLocalInterface =
        !localIp.empty() && inet_pton(AF_INET, localIp.c_str(), &localInterface.sin_addr) == 1;

    sock = mdns_socket_open_ipv4(hasLocalInterface ? &localInterface : nullptr);

    if (sock < 0)
    {
        Log::Error(LogCategory::Discovery, "failed to open the mDNS socket");
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    queryId = mdns_query_send(
        sock, MDNS_RECORDTYPE_PTR, kServiceName, std::strlen(kServiceName), buffer.data(), buffer.size(), 0);

    if (queryId < 0)
    {
        Log::Error(LogCategory::Discovery, "failed to send the mDNS query");
        mdns_socket_close(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    return true;
}

// Turns each device's raw instance name into its friendly name, and fills
// in its id.
void FinalizeDevices(DiscoveryContext& context)
{
    for (auto& device : context.devices)
    {
        if (const auto castId = context.castIds.find(device.name); castId != context.castIds.end())
        {
            device.castId = castId->second;
        }

        const auto friendlyName = context.friendlyNames.find(device.name);
        device.name = friendlyName != context.friendlyNames.end() ? friendlyName->second
                                                                    : StripServiceSuffix(device.name);
    }
}

std::string DeviceKey(const ChromecastDevice& device)
{
    return std::format("{} ({}:{}{}{})", device.name, device.ipAddress, device.port, device.castId.empty() ? "" : ", id ",
        device.castId);
}

// Only what changed since the last discovery is logged at Info: a rescan
// that finds the same TVs adds one Debug line.
void LogDeviceChanges(const std::vector<ChromecastDevice>& devices)
{
    static std::mutex mutex;
    static std::set<std::string> known;

    std::set<std::string> found;
    for (const auto& device : devices)
    {
        found.insert(DeviceKey(device));
    }

    std::lock_guard lock(mutex);
    for (const auto& key : found)
    {
        if (!known.contains(key))
        {
            Log::Info(LogCategory::Discovery, "found {}", key);
        }
    }
    for (const auto& key : known)
    {
        if (!found.contains(key))
        {
            Log::Info(LogCategory::Discovery, "lost {}", key);
        }
    }

    Log::Debug(LogCategory::Discovery, "{} device(s) found", found.size());
    known = std::move(found);
}

}  // namespace

ChromecastDiscoveryResult ChromecastDiscovery::DiscoverAndPrint(int timeoutMs)
{
    DiscoveryContext context;
    std::vector<char> buffer(2048);

    int sock = -1;
    int queryId = -1;

    if (!TryOpenSocketAndSendQuery(buffer, sock, queryId))
    {
        return {context.devices, false};
    }

    Log::Debug(LogCategory::Discovery, "searching for Chromecast devices");

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    while (true)
    {
        const auto remainingMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())
                .count();

        if (remainingMs <= 0)
        {
            break;
        }

        fd_set readSet;
        FD_ZERO(&readSet);
        AddToFdSet(sock, readSet);

        timeval timeout{};
        timeout.tv_sec = static_cast<long>(remainingMs / 1000);
        timeout.tv_usec = static_cast<long>((remainingMs % 1000) * 1000);

        if (select(sock + 1, &readSet, nullptr, nullptr, &timeout) <= 0)
        {
            break;
        }

        mdns_query_recv(sock, buffer.data(), buffer.size(), HandleRecord, &context, queryId);
    }

    mdns_socket_close(sock);
#ifdef _WIN32
    WSACleanup();
#endif

    FinalizeDevices(context);

    LogDeviceChanges(context.devices);

    return {context.devices, true};
}

}  // namespace mango
