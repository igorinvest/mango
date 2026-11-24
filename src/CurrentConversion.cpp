#include "CurrentConversion.hpp"

#include "configuration/StreamStartDelay.hpp"
#include "media/MediaJob.hpp"
#include "pipeline/stream/StreamManager.hpp"

namespace mango
{

CurrentConversion::CurrentConversion(RemoteStatusFn remoteStatus) : m_remoteStatus(std::move(remoteStatus)) {}

void CurrentConversion::StartedLocal()
{
    std::lock_guard lock(m_mutex);
    m_remoteEntry.clear();
}

void CurrentConversion::StartedRemote(const std::string& entryId)
{
    std::lock_guard lock(m_mutex);
    m_remoteEntry = entryId;
}

TranscodeStatus CurrentConversion::Status() const
{
    const auto entryId = RemoteEntry();
    if (entryId.empty())
    {
        return LocalStatus();
    }
    const auto status = m_remoteStatus ? m_remoteStatus(entryId) : std::nullopt;
    return status.value_or(TranscodeStatus{});
}

bool CurrentConversion::IsReady() const
{
    // -1 (nothing yet) is never ready.
    return Status().percent >= StreamStartDelaySetting::ReadyPercent();
}

TranscodeStatus CurrentConversion::LocalStatus()
{
    TranscodeStatus status;
    auto& manager = StreamManager::GetInstance();
    const auto record = manager.GetCurrentFileRecord();
    if (!record)
    {
        return status;
    }

    if (const auto percent = manager.GetTranscodeProgressPercent())
    {
        status.fastHash = record->fastHash;
        status.percent = *percent;
    }
    else if (const auto error = manager.GetCurrentConversionError())
    {
        status.fastHash = record->fastHash;
        status.error = ConversionErrors::ToString(*error);
    }
    return status;
}

std::string CurrentConversion::RemoteEntry() const
{
    std::lock_guard lock(m_mutex);
    return m_remoteEntry;
}

}  // namespace mango
