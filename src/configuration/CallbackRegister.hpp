#pragma once

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// Registry for native-host callbacks: how a native app shows its own file
// and folder pickers (NSOpenPanel, the WinUI picker), and where it keeps
// secrets (Keychain, Credential Manager). The engine has no dialog of its
// own; with no picker registered, a picker request is a logged no-op (see
// File::OpenFileAsync). With no secret store, secrets stay in files only the
// user can read (see remote::KeyStore). Defines its own handler types
// instead of reusing the classes that call them (File, ...), so those
// classes can depend on this header without a circular include.
class CallbackRegister
{
   public:
    // Must not block the calling thread — callers may invoke this from an
    // HTTP worker thread and return immediately, expecting `callback` to
    // fire later. NSOpenPanel.begin(completionHandler:) and similar native
    // async pickers satisfy this naturally; runModal()-style blocking APIs
    // do not and should be dispatched to their own thread by the handler.
    //
    // callback's second argument is a base64-encoded macOS security-scoped
    // bookmark for the selected file (empty if not applicable), persisted so
    // read access can be re-activated on future app launches.
    using OpenFileHandler = std::function<void(
        const std::string& title,
        const std::vector<std::string>& filters,
        std::function<void(const std::string& filePath, const std::string& securityBookmark)>
            callback)>;

    // Same contract as OpenFileHandler, for picking a folder (see
    // Library::SelectFolder). securityBookmark is for the folder itself and
    // covers everything inside it.
    using OpenFolderHandler = std::function<void(const std::string& title,
        std::function<void(const std::string& folderPath, const std::string& securityBookmark)>
            callback)>;

    // The value stored under name; std::nullopt when there's none. An error
    // (its text is logged) when the store can't be read right now, e.g. a
    // locked Keychain: the engine then doesn't replace the secret. Called
    // synchronously, on any engine thread.
    using LoadSecretHandler =
        std::function<std::expected<std::optional<std::string>, std::string>(const std::string& name)>;

    // Stores value under name, replacing what was there. false on failure.
    using SaveSecretHandler = std::function<bool(const std::string& name, const std::string& value)>;

    // Both handlers, or neither.
    struct SecretStoreHandlers
    {
        LoadSecretHandler load;
        SaveSecretHandler save;

        [[nodiscard]] bool IsSet() const { return load && save; }
    };

    CallbackRegister(const CallbackRegister&) = delete;
    CallbackRegister& operator=(const CallbackRegister&) = delete;
    CallbackRegister(CallbackRegister&&) = delete;
    CallbackRegister& operator=(CallbackRegister&&) = delete;

    static CallbackRegister& GetInstance()
    {
        static CallbackRegister instance;
        return instance;
    }

    void SetOpenFileHandler(OpenFileHandler handler);
    const OpenFileHandler& GetOpenFileHandler() const;

    void SetOpenFolderHandler(OpenFolderHandler handler);
    const OpenFolderHandler& GetOpenFolderHandler() const;

    // Register before Engine::Start: the device key is read then.
    void SetSecretStoreHandlers(SecretStoreHandlers handlers);
    const SecretStoreHandlers& GetSecretStoreHandlers() const;

   private:
    CallbackRegister() = default;

    OpenFileHandler m_openFileHandler;
    OpenFolderHandler m_openFolderHandler;
    SecretStoreHandlers m_secretStoreHandlers;
};
