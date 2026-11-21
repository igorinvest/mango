#include "DeviceIdentity.hpp"

#include "../../log/Log.hpp"

namespace mango::remote
{

DeviceIdentity::DeviceIdentity(Crypto::Ed25519KeyPair keys)
    : m_keys(std::move(keys)),
      m_deviceId(Crypto::ToHex(Crypto::Sha256(m_keys.publicKey))),
      m_publicKey(Crypto::Base64UrlEncode(m_keys.publicKey))
{
}

std::optional<DeviceIdentity> DeviceIdentity::LoadOrCreate(const std::filesystem::path& path)
{
    return LoadOrCreate(*KeyStore::ForFile(path));
}

std::optional<DeviceIdentity> DeviceIdentity::LoadOrCreate(KeyStore& store)
{
    const auto stored = store.Load();
    if (!stored)
    {
        Log::Error(LogCategory::Remote, "can't read this device's key: {}", stored.error());
        return std::nullopt;
    }
    if (stored->has_value())
    {
        if (auto identity = FromPrivateKey(**stored))
        {
            return identity;
        }
        Log::Error(LogCategory::Remote, "the stored key isn't valid, making a new one");
    }
    return CreateAndSave(store);
}

std::optional<DeviceIdentity> DeviceIdentity::CreateInMemory()
{
    auto keys = Crypto::GenerateEd25519();
    return keys ? std::optional(DeviceIdentity(std::move(*keys))) : std::nullopt;
}

std::string DeviceIdentity::Sign(std::string_view message) const
{
    return Crypto::Base64UrlEncode(Crypto::Ed25519Sign(m_keys.privateKey, Crypto::AsBytes(message)));
}

bool DeviceIdentity::Verify(std::string_view publicKey, std::string_view message, std::string_view signature)
{
    const auto key = Crypto::Base64UrlDecode(publicKey);
    const auto signatureBytes = Crypto::Base64UrlDecode(signature);
    return key && signatureBytes && Crypto::Ed25519Verify(*key, Crypto::AsBytes(message), *signatureBytes);
}

std::string DeviceIdentity::DeviceIdOf(std::string_view publicKey)
{
    const auto key = Crypto::Base64UrlDecode(publicKey);
    if (!key || key->size() != Crypto::kEd25519KeySize)
    {
        return {};
    }
    return Crypto::ToHex(Crypto::Sha256(*key));
}

std::optional<DeviceIdentity> DeviceIdentity::FromPrivateKey(const Crypto::Bytes& privateKey)
{
    auto publicKey = Crypto::Ed25519PublicKey(privateKey);
    return publicKey ? std::optional(DeviceIdentity({privateKey, std::move(*publicKey)})) : std::nullopt;
}

std::optional<DeviceIdentity> DeviceIdentity::CreateAndSave(KeyStore& store)
{
    auto keys = Crypto::GenerateEd25519();
    if (!keys || !store.Save(keys->privateKey))
    {
        Log::Error(LogCategory::Remote, "can't make or save this device's key");
        return std::nullopt;
    }

    DeviceIdentity identity(std::move(*keys));
    Log::Info(LogCategory::Remote, "made this device's key: {}", identity.DeviceId());
    return identity;
}

}  // namespace mango::remote
