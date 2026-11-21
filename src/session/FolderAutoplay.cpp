#include "FolderAutoplay.hpp"

#include <format>

#include "LibrarySections.hpp"
#include "log/Log.hpp"

namespace mango
{

void FolderAutoplay::Request(const std::string& fastHash, const std::string& sectionId, bool isAutoplay)
{
    CancelCountdown();
    m_sectionId = sectionId == LibrarySections::manualId ? "" : sectionId;
    m_currentFastHash = fastHash;
    m_autoStartedFastHash = isAutoplay ? fastHash : "";
    ++m_token;
    ResetEnd();

    if (!isAutoplay)
    {
        m_unreadableInARow = 0;
    }

    Log::Info(LogCategory::Playlist, "{} requested {} (section {})", isAutoplay ? "autoplay" : "Play", fastHash,
        m_sectionId.empty() ? "none" : m_sectionId);
}

void FolderAutoplay::StreamStarted(const std::string& fastHash)
{
    ++m_token;
    if (fastHash != m_currentFastHash)
    {
        return;
    }
    m_unreadableInARow = 0;
    ResetEnd();
}

void FolderAutoplay::NotePlaying(const std::string& fastHash, int token)
{
    if (fastHash == m_currentFastHash && token == m_token)
    {
        m_hasSeenPlaying = true;
    }
}

bool FolderAutoplay::AwaitsEnd(const std::string& fastHash, int token) const
{
    return fastHash == m_currentFastHash && token == m_token && m_hasSeenPlaying && !m_endHandled &&
           !m_countdown;
}

void FolderAutoplay::MarkEndHandled() { m_endHandled = true; }

std::string FolderAutoplay::DescribeEndState(const std::string& fastHash, int token) const
{
    return std::format("current={} (is {}: {}), token={}/{}, seenPlaying={}, endHandled={}, countdown={}",
        m_currentFastHash.empty() ? "none" : m_currentFastHash, fastHash, fastHash == m_currentFastHash, token,
        m_token, m_hasSeenPlaying, m_endHandled, m_countdown.has_value());
}

bool FolderAutoplay::IsAutoStarted(const std::string& fastHash) const
{
    return !fastHash.empty() && m_autoStartedFastHash == fastHash;
}

bool FolderAutoplay::NoteUnreadable()
{
    ++m_unreadableInARow;
    const bool keepGoing = m_unreadableInARow < maxUnreadableInARow;
    Log::Info(LogCategory::Playlist, "unreadable file {} in a row: {}", m_unreadableInARow,
        keepGoing ? "skipping to the next" : "stopping the chain");
    return keepGoing;
}

void FolderAutoplay::StartCountdown(AutoplayCountdown countdown)
{
    CancelCountdown();
    Log::Info(LogCategory::Playlist, "countdown → {} ({}, section {})", countdown.nextTitle,
        countdown.nextFastHash, m_sectionId.empty() ? "none" : m_sectionId);
    m_countdown = std::move(countdown);
}

std::optional<AutoplayCountdown> FolderAutoplay::CancelCountdown()
{
    auto cancelled = std::move(m_countdown);
    m_countdown.reset();
    return cancelled;
}

void FolderAutoplay::ResetEnd()
{
    m_hasSeenPlaying = false;
    m_endHandled = false;
}

}  // namespace mango
