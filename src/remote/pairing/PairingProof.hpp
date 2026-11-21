#pragma once

#include <optional>
#include <string>
#include <string_view>

// The pieces of pairing that need no network: the invite secret and link,
// and the proof that the peer the signaling service introduced holds the
// secret (signal/README.md, "Pairing").
namespace mango::remote::PairingProof
{

// What the QR code or link carries: the secret, and A's deviceId so B can
// check the service introduced A and not someone else.
struct InviteLink
{
    std::string secret;
    std::string deviceId;
};

// 128 random bits, base64url.
[[nodiscard]] std::string NewInviteSecret();

// Hex SHA-256 of the secret's UTF-8: all the service learns from A.
[[nodiscard]] std::string InviteHash(std::string_view secret);

// "mango://pair?s=<secret>&d=<deviceId>".
[[nodiscard]] std::string FormatLink(const InviteLink& link);

// Reads a link from FormatLink, its parameters in any order. std::nullopt
// for anything else, or a secret shorter than the service accepts.
[[nodiscard]] std::optional<InviteLink> ParseLink(std::string_view link);

enum class Side
{
    Inviter,
    Joiner,
};

// HMAC-SHA256(secret, "mango-pair-v1|<side>|<inviter key>|<joiner key>"),
// base64url. The sender's side is in it, so a proof can't be sent back to
// the side that made it.
[[nodiscard]] std::string Compute(
    std::string_view secret, Side sender, std::string_view inviterPublicKey, std::string_view joinerPublicKey);

[[nodiscard]] bool Verify(std::string_view proof, std::string_view secret, Side sender,
    std::string_view inviterPublicKey, std::string_view joinerPublicKey);

}  // namespace mango::remote::PairingProof
