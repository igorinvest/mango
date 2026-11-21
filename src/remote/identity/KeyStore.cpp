#include "KeyStore.hpp"

#include <system_error>

#include "../../configuration/CallbackRegister.hpp"
#include "../../file/Json.hpp"
#include "../../log/Log.hpp"

namespace mango::remote
{

// remote/identity.json. Not in an anonymous namespace: glaze needs a type
// with linkage.
struct IdentityFile
{
    std::string privateKey;
};

}  // namespace mango::remote

template <>
struct glz::meta<mango::remote::IdentityFile>
{
    using T = mango::remote::IdentityFile;
    static constexpr auto value = object("privateKey", &T::privateKey);
};

namespace mango::remote
{

namespace
{

// The name the key has in the native store.
constexpr auto kNativeKeyName = "deviceIdentity";

// Only the user may read the key. On Windows the user's profile folder
// already is private.
void RestrictToOwner(const std::filesystem::path& path, std::filesystem::perms perms)
{
    std::error_code ignored;
    std::filesystem::permissions(path, perms, std::filesystem::perm_options::replace, ignored);
}

bool HasKey(const KeyStore::LoadResult& result) { return result && result->has_value(); }

}  // namespace

// ---- KeyStore --------------------------------------------------------------

std::unique_ptr<KeyStore> KeyStore::ForFile(const std::filesystem::path& path)
{
    if (!CallbackRegister::GetInstance().GetSecretStoreHandlers().IsSet())
    {
        return std::make_unique<FileKeyStore>(path);
    }
    return std::make_unique<FallbackKeyStore>(std::make_unique<NativeKeyStore>(kNativeKeyName), FileKeyStore(path));
}

// ---- FileKeyStore ----------------------------------------------------------

FileKeyStore::FileKeyStore(std::filesystem::path path) : m_path(std::move(path)) {}

KeyStore::LoadResult FileKeyStore::Load()
{
    const auto file = Json::ReadJsonFromFile<IdentityFile>(m_path);
    return file ? Crypto::Base64UrlDecode(file->privateKey) : std::nullopt;
}

bool FileKeyStore::Save(const Crypto::Bytes& privateKey)
{
    std::error_code ignored;
    std::filesystem::create_directories(m_path.parent_path(), ignored);
    // The file is written next to m_path before it's renamed over it: the
    // folder keeps others out of that copy too.
    RestrictToOwner(m_path.parent_path(), std::filesystem::perms::owner_all);

    if (!Json::SaveJsonToFile(m_path, IdentityFile{Crypto::Base64UrlEncode(privateKey)}))
    {
        return false;
    }
    RestrictToOwner(m_path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    return true;
}

void FileKeyStore::Remove()
{
    std::error_code ignored;
    std::filesystem::remove(m_path, ignored);
}

// ---- NativeKeyStore --------------------------------------------------------

NativeKeyStore::NativeKeyStore(std::string name) : m_name(std::move(name)) {}

KeyStore::LoadResult NativeKeyStore::Load()
{
    const auto stored = CallbackRegister::GetInstance().GetSecretStoreHandlers().load(m_name);
    if (!stored)
    {
        return std::unexpected(stored.error());
    }
    if (!stored->has_value())
    {
        return std::nullopt;
    }
    // Text that isn't a key reads as no key, as a broken file does.
    return Crypto::Base64UrlDecode(**stored);
}

bool NativeKeyStore::Save(const Crypto::Bytes& privateKey)
{
    return CallbackRegister::GetInstance().GetSecretStoreHandlers().save(m_name, Crypto::Base64UrlEncode(privateKey));
}

// ---- FallbackKeyStore ------------------------------------------------------

FallbackKeyStore::FallbackKeyStore(std::unique_ptr<KeyStore> primary, FileKeyStore fallback)
    : m_primary(std::move(primary)), m_fallback(std::move(fallback))
{
}

KeyStore::LoadResult FallbackKeyStore::Load()
{
    auto result = m_primary->Load();
    if (!result)
    {
        return LoadAfterPrimaryFailed(result.error());
    }
    if (result->has_value())
    {
        // Left behind when an earlier run saved into the file instead.
        m_fallback.Remove();
        return result;
    }
    return MigrateFromFile();
}

bool FallbackKeyStore::Save(const Crypto::Bytes& privateKey)
{
    if (m_primary->Save(privateKey))
    {
        return true;
    }
    Log::Warning(LogCategory::Remote, "can't save this device's key in the system store, using {}", m_fallback.Path().string());
    return m_fallback.Save(privateKey);
}

// A key in the file still works. With none there, the failure stands: a new
// key would replace the one primary can't read right now.
KeyStore::LoadResult FallbackKeyStore::LoadAfterPrimaryFailed(const std::string& error)
{
    auto fromFile = m_fallback.Load();
    if (HasKey(fromFile))
    {
        Log::Warning(LogCategory::Remote, "can't read the system store ({}), using {}", error, m_fallback.Path().string());
        return fromFile;
    }
    return std::unexpected(error);
}

KeyStore::LoadResult FallbackKeyStore::MigrateFromFile()
{
    auto fromFile = m_fallback.Load();
    if (!HasKey(fromFile))
    {
        return std::nullopt;
    }
    if (m_primary->Save(**fromFile))
    {
        m_fallback.Remove();
        Log::Info(LogCategory::Remote, "moved this device's key from {} to the system store", m_fallback.Path().string());
    }
    return fromFile;
}

}  // namespace mango::remote
