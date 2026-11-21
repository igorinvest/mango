#include "RequestGuard.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>

namespace
{
bool IsSafePathChar(unsigned char c)
{
    return std::isalnum(c) || c == '.' || c == '_' || c == '-';
}

bool IsDecimalOctet(std::string_view part)
{
    if (part.empty() || part.size() > 3 ||
        !std::ranges::all_of(part, [](unsigned char c) { return std::isdigit(c); }))
    {
        return false;
    }

    int value = 0;
    for (const char c : part)
    {
        value = value * 10 + (c - '0');
    }
    return value <= 255;
}

char ToLower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
}  // namespace

bool RequestGuard::IsSafePathSegment(std::string_view segment)
{
    if (segment.empty() || segment == "." || segment == "..")
    {
        return false;
    }

    return std::ranges::all_of(segment, [](unsigned char c) { return IsSafePathChar(c); });
}

bool RequestGuard::IsAllowedHost(std::string_view host)
{
    if (host.empty())
    {
        return true;
    }

    const auto name = StripPort(host);
    return IsIpv4Literal(name) || IsBracketedIpv6Literal(name) ||
           EqualsIgnoreCase(name, "localhost") ||
           (name.size() > 6 && EndsWithIgnoreCase(name, ".local"));
}

// "host:port" -> "host"; "[v6]:port" -> "[v6]". A bare IPv6 address without
// brackets isn't a valid Host header, so a colon always starts the port.
std::string_view RequestGuard::StripPort(std::string_view host)
{
    if (host.starts_with('['))
    {
        const auto close = host.find(']');
        return close == std::string_view::npos ? host : host.substr(0, close + 1);
    }

    return host.substr(0, host.find(':'));
}

bool RequestGuard::IsIpv4Literal(std::string_view host)
{
    int parts = 0;
    for (const auto part : host | std::views::split('.'))
    {
        if (!IsDecimalOctet(std::string_view(part.begin(), part.end())))
        {
            return false;
        }
        ++parts;
    }
    return parts == 4;
}

bool RequestGuard::IsBracketedIpv6Literal(std::string_view host)
{
    if (host.size() < 4 || !host.starts_with('[') || !host.ends_with(']'))
    {
        return false;
    }

    const auto address = host.substr(1, host.size() - 2);
    return address.contains(':') && std::ranges::all_of(address,
                                        [](unsigned char c)
                                        { return std::isxdigit(c) || c == ':' || c == '.'; });
}

bool RequestGuard::EqualsIgnoreCase(std::string_view a, std::string_view b)
{
    return std::ranges::equal(a, b, [](char x, char y) { return ToLower(x) == ToLower(y); });
}

bool RequestGuard::EndsWithIgnoreCase(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() &&
           EqualsIgnoreCase(text.substr(text.size() - suffix.size()), suffix);
}
