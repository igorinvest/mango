#include "Crypto.hpp"

#include <array>
#include <memory>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace mango::remote::Crypto
{

namespace
{

struct PkeyDeleter
{
    void operator()(EVP_PKEY* key) const { EVP_PKEY_free(key); }
};

struct MdContextDeleter
{
    void operator()(EVP_MD_CTX* context) const { EVP_MD_CTX_free(context); }
};

using PkeyPtr = std::unique_ptr<EVP_PKEY, PkeyDeleter>;
using MdContextPtr = std::unique_ptr<EVP_MD_CTX, MdContextDeleter>;

PkeyPtr PrivateKeyFromSeed(std::span<const std::uint8_t> seed)
{
    if (seed.size() != kEd25519KeySize)
    {
        return nullptr;
    }
    return PkeyPtr(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed.data(), seed.size()));
}

PkeyPtr PublicKeyFromRaw(std::span<const std::uint8_t> publicKey)
{
    if (publicKey.size() != kEd25519KeySize)
    {
        return nullptr;
    }
    return PkeyPtr(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, publicKey.data(), publicKey.size()));
}

std::optional<Bytes> RawPublicKey(EVP_PKEY* key)
{
    Bytes raw(kEd25519KeySize);
    std::size_t size = raw.size();
    if (EVP_PKEY_get_raw_public_key(key, raw.data(), &size) != 1 || size != kEd25519KeySize)
    {
        return std::nullopt;
    }
    return raw;
}

int Base64UrlValue(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

std::string_view StripPadding(std::string_view text)
{
    while (!text.empty() && text.back() == '=')
    {
        text.remove_suffix(1);
    }
    return text;
}

}  // namespace

Bytes RandomBytes(std::size_t count)
{
    Bytes bytes(count);
    if (count > 0 && RAND_bytes(bytes.data(), static_cast<int>(count)) != 1)
    {
        return {};
    }
    return bytes;
}

Bytes Sha256(std::span<const std::uint8_t> data)
{
    Bytes digest(32);
    unsigned int size = 0;
    if (EVP_Digest(data.data(), data.size(), digest.data(), &size, EVP_sha256(), nullptr) != 1)
    {
        return {};
    }
    digest.resize(size);
    return digest;
}

Bytes HmacSha256(std::span<const std::uint8_t> key, std::span<const std::uint8_t> data)
{
    Bytes mac(EVP_MAX_MD_SIZE);
    std::size_t size = 0;
    if (!EVP_Q_mac(nullptr, "HMAC", nullptr, "SHA256", nullptr, key.data(), key.size(), data.data(), data.size(),
            mac.data(), mac.size(), &size))
    {
        return {};
    }
    mac.resize(size);
    return mac;
}

std::string ToHex(std::span<const std::uint8_t> data)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(data.size() * 2);
    for (const auto byte : data)
    {
        hex.push_back(kDigits[byte >> 4]);
        hex.push_back(kDigits[byte & 0x0f]);
    }
    return hex;
}

std::string Base64UrlEncode(std::span<const std::uint8_t> data)
{
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string text;
    text.reserve((data.size() * 4 + 2) / 3);

    std::size_t i = 0;
    for (; i + 2 < data.size(); i += 3)
    {
        const std::uint32_t triple = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        text.push_back(kAlphabet[(triple >> 18) & 63]);
        text.push_back(kAlphabet[(triple >> 12) & 63]);
        text.push_back(kAlphabet[(triple >> 6) & 63]);
        text.push_back(kAlphabet[triple & 63]);
    }

    const auto rest = data.size() - i;
    if (rest > 0)
    {
        const std::uint32_t triple = (data[i] << 16) | (rest == 2 ? data[i + 1] << 8 : 0);
        text.push_back(kAlphabet[(triple >> 18) & 63]);
        text.push_back(kAlphabet[(triple >> 12) & 63]);
        if (rest == 2)
        {
            text.push_back(kAlphabet[(triple >> 6) & 63]);
        }
    }
    return text;
}

std::optional<Bytes> Base64UrlDecode(std::string_view text)
{
    text = StripPadding(text);
    if (text.size() % 4 == 1)
    {
        return std::nullopt;
    }

    Bytes bytes;
    bytes.reserve(text.size() * 3 / 4);
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char c : text)
    {
        const int value = Base64UrlValue(c);
        if (value < 0)
        {
            return std::nullopt;
        }
        buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            bytes.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xff));
        }
    }
    return bytes;
}

std::optional<Ed25519KeyPair> GenerateEd25519()
{
    const auto seed = RandomBytes(kEd25519KeySize);
    if (seed.size() != kEd25519KeySize)
    {
        return std::nullopt;
    }

    auto publicKey = Ed25519PublicKey(seed);
    if (!publicKey)
    {
        return std::nullopt;
    }
    return Ed25519KeyPair{seed, std::move(*publicKey)};
}

std::optional<Bytes> Ed25519PublicKey(std::span<const std::uint8_t> privateKey)
{
    const auto key = PrivateKeyFromSeed(privateKey);
    return key ? RawPublicKey(key.get()) : std::nullopt;
}

Bytes Ed25519Sign(std::span<const std::uint8_t> privateKey, std::span<const std::uint8_t> message)
{
    const auto key = PrivateKeyFromSeed(privateKey);
    const MdContextPtr context(EVP_MD_CTX_new());
    if (!key || !context || EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key.get()) != 1)
    {
        return {};
    }

    Bytes signature(kEd25519SignatureSize);
    std::size_t size = signature.size();
    if (EVP_DigestSign(context.get(), signature.data(), &size, message.data(), message.size()) != 1)
    {
        return {};
    }
    signature.resize(size);
    return signature;
}

bool Ed25519Verify(std::span<const std::uint8_t> publicKey, std::span<const std::uint8_t> message,
    std::span<const std::uint8_t> signature)
{
    const auto key = PublicKeyFromRaw(publicKey);
    const MdContextPtr context(EVP_MD_CTX_new());
    if (!key || !context || signature.size() != kEd25519SignatureSize ||
        EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key.get()) != 1)
    {
        return false;
    }
    return EVP_DigestVerify(context.get(), signature.data(), signature.size(), message.data(), message.size()) == 1;
}

bool ConstantTimeEquals(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b)
{
    return a.size() == b.size() && CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

}  // namespace mango::remote::Crypto
