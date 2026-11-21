#include "PairingProof.hpp"

#include <format>

#include "../identity/Crypto.hpp"

namespace mango::remote::PairingProof
{

namespace
{

constexpr std::string_view kLinkPrefix = "mango://pair?";

// The service's minimum.
constexpr std::size_t kMinSecretLength = 16;

// value of name in query ("a=1&b=2"); std::nullopt when it's missing.
std::optional<std::string_view> QueryValue(std::string_view query, std::string_view name)
{
    while (!query.empty())
    {
        const auto end = query.find('&');
        const auto pair = query.substr(0, end);
        const auto equals = pair.find('=');
        if (equals != std::string_view::npos && pair.substr(0, equals) == name)
        {
            return pair.substr(equals + 1);
        }
        query = end == std::string_view::npos ? std::string_view{} : query.substr(end + 1);
    }
    return std::nullopt;
}

bool IsDeviceId(std::string_view text)
{
    return text.size() == 64 && text.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

std::string_view SideName(Side side) { return side == Side::Inviter ? "inviter" : "joiner"; }

}  // namespace

std::string NewInviteSecret() { return Crypto::Base64UrlEncode(Crypto::RandomBytes(16)); }

std::string InviteHash(std::string_view secret) { return Crypto::ToHex(Crypto::Sha256(Crypto::AsBytes(secret))); }

std::string FormatLink(const InviteLink& link)
{
    return std::format("{}s={}&d={}", kLinkPrefix, link.secret, link.deviceId);
}

std::optional<InviteLink> ParseLink(std::string_view link)
{
    if (!link.starts_with(kLinkPrefix))
    {
        return std::nullopt;
    }

    const auto query = link.substr(kLinkPrefix.size());
    const auto secret = QueryValue(query, "s");
    const auto deviceId = QueryValue(query, "d");
    if (!secret || !deviceId || secret->size() < kMinSecretLength || !IsDeviceId(*deviceId) ||
        !Crypto::Base64UrlDecode(*secret))
    {
        return std::nullopt;
    }
    return InviteLink{std::string(*secret), std::string(*deviceId)};
}

std::string Compute(
    std::string_view secret, Side sender, std::string_view inviterPublicKey, std::string_view joinerPublicKey)
{
    const auto message = std::format("mango-pair-v1|{}|{}|{}", SideName(sender), inviterPublicKey, joinerPublicKey);
    return Crypto::Base64UrlEncode(Crypto::HmacSha256(Crypto::AsBytes(secret), Crypto::AsBytes(message)));
}

bool Verify(std::string_view proof, std::string_view secret, Side sender, std::string_view inviterPublicKey,
    std::string_view joinerPublicKey)
{
    const auto expected = Compute(secret, sender, inviterPublicKey, joinerPublicKey);
    return Crypto::ConstantTimeEquals(Crypto::AsBytes(proof), Crypto::AsBytes(expected));
}

}  // namespace mango::remote::PairingProof
