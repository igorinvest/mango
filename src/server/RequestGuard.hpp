#pragma once

#include <string_view>

// Pure checks the HTTP server applies to incoming requests before trusting
// them. See docs/scope/web-server-security-scope.md for the threats each one
// answers.
class RequestGuard
{
public:
    // A single path segment that is safe to join onto a folder: non-empty,
    // only [A-Za-z0-9._-], and not "." or "..". Rejects every separator
    // ('/', '\\') and drive prefix (':'), whatever the request percent-encoded.
    static bool IsSafePathSegment(std::string_view segment);

    // A Host header naming this machine the way a LAN client would: an IP
    // literal, "localhost" or a "*.local" mDNS name, with an optional port.
    // A DNS-rebinding page always arrives with its own public domain name.
    // An empty Host (HTTP/1.0, non-browser clients) is allowed.
    static bool IsAllowedHost(std::string_view host);

private:
    static std::string_view StripPort(std::string_view host);
    static bool IsIpv4Literal(std::string_view host);
    static bool IsBracketedIpv6Literal(std::string_view host);
    static bool EqualsIgnoreCase(std::string_view a, std::string_view b);
    static bool EndsWithIgnoreCase(std::string_view text, std::string_view suffix);
};
