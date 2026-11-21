#include "NetworkUtils.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cstring>

#include "../configuration/Settings.hpp"

namespace {

bool IsPortAvailable(int port) {
#ifdef _WIN32
  WSADATA wsaData;

  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    return false;
  }
#endif

  int sock = socket(AF_INET, SOCK_STREAM, 0);

  if (sock < 0) {
#ifdef _WIN32
    WSACleanup();
#endif
    return false;
  }

  int reuse = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
             reinterpret_cast<const char *>(&reuse), sizeof(reuse));

  sockaddr_in addr{};

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(static_cast<uint16_t>(port));

  const bool available =
      bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0;

#ifdef _WIN32
  closesocket(sock);
  WSACleanup();
#else
  close(sock);
#endif

  return available;
}

bool IsLinkLocal(const std::string &ipv4) { return ipv4.starts_with("169.254."); }

bool QualifiesForLan(const NetworkInterface &networkInterface) {
  return networkInterface.kind != NetworkInterfaceKind::Tunnel &&
         networkInterface.kind != NetworkInterfaceKind::Loopback &&
         !IsLinkLocal(networkInterface.ipv4);
}

// Lower is better: a wired TV connection beats Wi-Fi.
int Rank(NetworkInterfaceKind kind) {
  switch (kind) {
  case NetworkInterfaceKind::Ethernet:
    return 0;
  case NetworkInterfaceKind::WiFi:
    return 1;
  default:
    return 2;
  }
}

}  // namespace

std::string NetworkUtils::GetLanIPAddress() {
  return ChooseLanAddress(ListNetworkInterfaces(), GetDefaultRouteAddress());
}

std::string NetworkUtils::ChooseLanAddress(
    const std::vector<NetworkInterface> &interfaces,
    const std::string &defaultRouteAddress) {
  std::vector<NetworkInterface> candidates;
  std::ranges::copy_if(interfaces, std::back_inserter(candidates),
                       QualifiesForLan);

  if (std::ranges::contains(candidates, defaultRouteAddress,
                            &NetworkInterface::ipv4)) {
    return defaultRouteAddress;
  }

  // Stable, so interfaces of one kind keep the OS's order.
  std::ranges::stable_sort(candidates, {}, [](const NetworkInterface &candidate) {
    return Rank(candidate.kind);
  });
  return candidates.empty() ? defaultRouteAddress : candidates.front().ipv4;
}

bool NetworkUtils::IsThisMachine(const std::string &ipv4) {
  return !ipv4.empty() &&
         std::ranges::contains(ListNetworkInterfaces(), ipv4,
                               &NetworkInterface::ipv4);
}

std::string NetworkUtils::GetDefaultRouteAddress() {
#ifdef _WIN32
  WSADATA wsaData;

  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    return {};
  }
#endif

  int sock = socket(AF_INET, SOCK_DGRAM, 0);

  if (sock < 0) {
#ifdef _WIN32
    WSACleanup();
#endif
    return {};
  }

  sockaddr_in target{};

  target.sin_family = AF_INET;
  target.sin_port = htons(80);

  if (inet_pton(AF_INET, "8.8.8.8", &target.sin_addr) <= 0) {
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
    return {};
  }

  if (connect(sock, reinterpret_cast<sockaddr *>(&target), sizeof(target)) <
      0) {
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
    return {};
  }

  sockaddr_in local{};

#ifdef _WIN32
  int length = sizeof(local);
#else
  socklen_t length = sizeof(local);
#endif

  if (getsockname(sock, reinterpret_cast<sockaddr *>(&local), &length) < 0) {
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
    return {};
  }

  char buffer[INET_ADDRSTRLEN]{};

  if (inet_ntop(AF_INET, &local.sin_addr, buffer, sizeof(buffer)) == nullptr) {
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
    return {};
  }

#ifdef _WIN32
  closesocket(sock);
  WSACleanup();
#else
  close(sock);
#endif

  return buffer;
}

int NetworkUtils::FindAvailablePort() {
  int port = 8080;

  if (const auto setting = Settings::GetStringSetting("serverPort")) {
    try {
      port = std::stoi(*setting);
    } catch (...) {
    }
  }

  while (!IsPortAvailable(port)) {
    ++port;
  }

  return port;
}

std::string NetworkUtils::Get_mime_type(const std::string &filename) {
  if (filename.find(".m3u8") != std::string::npos)
    return "application/vnd.apple.mpegurl";
  if (filename.find(".m4s") != std::string::npos)
    return "video/mp4";
  if (filename.find(".ts") != std::string::npos)
    return "video/MP2T";
  if (filename.find(".mp4") != std::string::npos)
    return "video/mp4";
  if (filename.find(".vtt") != std::string::npos)
    return "text/vtt";
  if (filename.find(".mpd") != std::string::npos)
    return "application/dash+xml";

  return "application/octet-stream";
}
