#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "Crypto.hpp"
#include "KeyStore.hpp"

namespace mango::remote
{

// This Mango install's Ed25519 key pair: made once, then kept. Its public
// key is what the signaling service and paired peers know this device by.
//
// Kept in the app's Keychain or Credential Manager when it registered one
// (CallbackRegister::SecretStoreHandlers), else in remote/identity.json,
// readable only by the user (see KeyStore::ForFile).
class DeviceIdentity
{
   public:
    // LoadOrCreate on KeyStore::ForFile(path).
    [[nodiscard]] static std::optional<DeviceIdentity> LoadOrCreate(const std::filesystem::path& path);

    // Reads the key from store, or makes one and saves it there.
    // std::nullopt when the store can't be read, or no key can be made or
    // saved.
    [[nodiscard]] static std::optional<DeviceIdentity> LoadOrCreate(KeyStore& store);

    // For tests: an identity that's never written anywhere.
    [[nodiscard]] static std::optional<DeviceIdentity> CreateInMemory();

    // Lowercase hex SHA-256 of the raw public key (64 characters).
    [[nodiscard]] const std::string& DeviceId() const { return m_deviceId; }

    // The raw public key, base64url without padding.
    [[nodiscard]] const std::string& PublicKey() const { return m_publicKey; }

    // Base64url signature over message's bytes.
    [[nodiscard]] std::string Sign(std::string_view message) const;

    // Checks a base64url signature against a base64url public key.
    [[nodiscard]] static bool Verify(std::string_view publicKey, std::string_view message, std::string_view signature);

    // The deviceId a base64url public key stands for; empty if it isn't one.
    [[nodiscard]] static std::string DeviceIdOf(std::string_view publicKey);

   private:
    explicit DeviceIdentity(Crypto::Ed25519KeyPair keys);

    [[nodiscard]] static std::optional<DeviceIdentity> FromPrivateKey(const Crypto::Bytes& privateKey);
    [[nodiscard]] static std::optional<DeviceIdentity> CreateAndSave(KeyStore& store);

    Crypto::Ed25519KeyPair m_keys;
    std::string m_deviceId;
    std::string m_publicKey;
};

}  // namespace mango::remote
