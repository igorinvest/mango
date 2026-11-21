#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The cryptography remote streaming needs (see
// docs/scope/hybrid_streaming_architecture.md), from OpenSSL — the library
// libdatachannel already brings. mbedTLS stays for Cast TLS: its 3.x has no
// Ed25519.
namespace mango::remote::Crypto
{

using Bytes = std::vector<std::uint8_t>;

inline constexpr std::size_t kEd25519KeySize = 32;
inline constexpr std::size_t kEd25519SignatureSize = 64;

[[nodiscard]] Bytes RandomBytes(std::size_t count);

[[nodiscard]] Bytes Sha256(std::span<const std::uint8_t> data);
[[nodiscard]] Bytes HmacSha256(std::span<const std::uint8_t> key, std::span<const std::uint8_t> data);

// Lowercase hex, as the signaling service writes deviceId and inviteHash.
[[nodiscard]] std::string ToHex(std::span<const std::uint8_t> data);

// Base64url without padding, as the signaling service writes keys and
// signatures. Decode accepts padding too; std::nullopt for anything else
// that isn't base64url.
[[nodiscard]] std::string Base64UrlEncode(std::span<const std::uint8_t> data);
[[nodiscard]] std::optional<Bytes> Base64UrlDecode(std::string_view text);

// The private key is the raw 32-byte seed; the public key the raw 32 bytes.
struct Ed25519KeyPair
{
    Bytes privateKey;
    Bytes publicKey;
};

[[nodiscard]] std::optional<Ed25519KeyPair> GenerateEd25519();

// std::nullopt when privateKey isn't a valid 32-byte seed.
[[nodiscard]] std::optional<Bytes> Ed25519PublicKey(std::span<const std::uint8_t> privateKey);

// An empty result means signing failed.
[[nodiscard]] Bytes Ed25519Sign(std::span<const std::uint8_t> privateKey, std::span<const std::uint8_t> message);

[[nodiscard]] bool Ed25519Verify(std::span<const std::uint8_t> publicKey, std::span<const std::uint8_t> message,
    std::span<const std::uint8_t> signature);

// Compares in time that doesn't depend on where the first difference is.
[[nodiscard]] bool ConstantTimeEquals(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b);

[[nodiscard]] inline std::span<const std::uint8_t> AsBytes(std::string_view text)
{
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

}  // namespace mango::remote::Crypto
