#include "CallbackRegister.hpp"

void CallbackRegister::SetOpenFileHandler(OpenFileHandler handler)
{
    m_openFileHandler = std::move(handler);
}

const CallbackRegister::OpenFileHandler& CallbackRegister::GetOpenFileHandler() const
{
    return m_openFileHandler;
}

void CallbackRegister::SetOpenFolderHandler(OpenFolderHandler handler)
{
    m_openFolderHandler = std::move(handler);
}

const CallbackRegister::OpenFolderHandler& CallbackRegister::GetOpenFolderHandler() const
{
    return m_openFolderHandler;
}

void CallbackRegister::SetSecretStoreHandlers(SecretStoreHandlers handlers)
{
    m_secretStoreHandlers = std::move(handlers);
}

const CallbackRegister::SecretStoreHandlers& CallbackRegister::GetSecretStoreHandlers() const
{
    return m_secretStoreHandlers;
}
