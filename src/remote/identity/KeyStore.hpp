#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "Crypto.hpp"

namespace mango::remote
{

// Where DeviceIdentity keeps its private key.
class KeyStore
{
   public:
    // The key; std::nullopt when there's none yet. An error when the store
    // can't be read right now: the caller must not replace the key then.
    using LoadResult = std::expected<std::optional<Crypto::Bytes>, std::string>;

    virtual ~KeyStore() = default;

    [[nodiscard]] virtual LoadResult Load() = 0;
    virtual bool Save(const Crypto::Bytes& privateKey) = 0;

    // The native store from CallbackRegister, with the file at path as its
    // fallback, when the app registered one; the file alone otherwise.
    [[nodiscard]] static std::unique_ptr<KeyStore> ForFile(const std::filesystem::path& path);
};

// remote/identity.json, readable only by the user.
class FileKeyStore final : public KeyStore
{
   public:
    explicit FileKeyStore(std::filesystem::path path);

    // A missing or unreadable file reads as no key.
    [[nodiscard]] LoadResult Load() override;
    bool Save(const Crypto::Bytes& privateKey) override;
    void Remove();

    [[nodiscard]] const std::filesystem::path& Path() const { return m_path; }

   private:
    std::filesystem::path m_path;
};

// The app's Keychain or Credential Manager, through CallbackRegister's
// secret store handlers. The key is stored as base64url text.
class NativeKeyStore final : public KeyStore
{
   public:
    explicit NativeKeyStore(std::string name);

    [[nodiscard]] LoadResult Load() override;
    bool Save(const Crypto::Bytes& privateKey) override;

   private:
    std::string m_name;
};

// primary first, file when primary fails. A key found only in the file
// moves into primary, and the file goes once primary holds the key.
class FallbackKeyStore final : public KeyStore
{
   public:
    FallbackKeyStore(std::unique_ptr<KeyStore> primary, FileKeyStore fallback);

    [[nodiscard]] LoadResult Load() override;
    bool Save(const Crypto::Bytes& privateKey) override;

   private:
    [[nodiscard]] LoadResult LoadAfterPrimaryFailed(const std::string& error);
    [[nodiscard]] LoadResult MigrateFromFile();

    std::unique_ptr<KeyStore> m_primary;
    FileKeyStore m_fallback;
};

}  // namespace mango::remote
