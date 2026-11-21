#include "FolderTrackMemory.hpp"

#include <algorithm>
#include <chrono>

#include "../../configuration/Languages.hpp"
#include "../../file/File.hpp"
#include "../../file/Json.hpp"
#include "../../log/Log.hpp"

namespace
{
constexpr auto kFileName = "folder_tracks.json";

std::int64_t NowUnixSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
}  // namespace

FolderTrackMemory& FolderTrackMemory::GetInstance()
{
    static FolderTrackMemory instance(File::getApplicationDataPath() / kFileName);
    return instance;
}

FolderTrackMemory::FolderTrackMemory(std::filesystem::path file) : m_file(std::move(file)) { Load(); }

std::string FolderTrackMemory::FolderKey(const std::filesystem::path& videoFile)
{
    return File::ToUtf8(videoFile.parent_path());
}

std::optional<FolderTrackChoice> FolderTrackMemory::Get(const std::filesystem::path& videoFile) const
{
    std::lock_guard lock(m_mutex);
    const auto found = m_folders.find(FolderKey(videoFile));
    return found != m_folders.end() ? std::optional(found->second) : std::nullopt;
}

void FolderTrackMemory::RememberAudio(const std::filesystem::path& videoFile, const std::string& language)
{
    {
        std::lock_guard lock(m_mutex);
        TouchLocked(FolderKey(videoFile)).audioLanguage = language;
        SaveLocked();
    }
    Log::Info(LogCategory::History, "folder audio {} for {}", language, FolderKey(videoFile));
    Changed();
}

void FolderTrackMemory::RememberSubtitle(
    const std::filesystem::path& videoFile, const std::optional<std::string>& language)
{
    {
        std::lock_guard lock(m_mutex);
        auto& choice = TouchLocked(FolderKey(videoFile));
        choice.subtitleLanguage = language;
        choice.subtitlesOff = !language;
        SaveLocked();
    }
    Log::Info(LogCategory::History, "folder subtitles {} for {}", language.value_or("off"), FolderKey(videoFile));
    Changed();
}

void FolderTrackMemory::RememberChosen(
    const std::filesystem::path& videoFile, const TrackRequest& request, const TrackList& trackList)
{
    if (request.audioTrackId && trackList.audioTrack)
    {
        if (const auto language = Languages::Normalize(trackList.audioTrack->language))
        {
            RememberAudio(videoFile, *language);
        }
    }

    if (request.subtitleTrackId && *request.subtitleTrackId < 0)
    {
        RememberSubtitle(videoFile, std::nullopt);
    }
    else if (request.subtitleTrackId && trackList.subtitleTrack)
    {
        if (const auto language = Languages::Normalize(trackList.subtitleTrack->language))
        {
            RememberSubtitle(videoFile, language);
        }
    }
}

void FolderTrackMemory::Clear()
{
    {
        std::lock_guard lock(m_mutex);
        m_folders.clear();
        SaveLocked();
    }
    Log::Info(LogCategory::History, "folder track choices forgotten");
    Changed();
}

std::size_t FolderTrackMemory::Size() const
{
    std::lock_guard lock(m_mutex);
    return m_folders.size();
}

void FolderTrackMemory::SetChangeListener(ChangeListener::Handler handler) { m_changes.Set(std::move(handler)); }

FolderTrackChoice& FolderTrackMemory::TouchLocked(const std::string& folderKey)
{
    auto& choice = m_folders[folderKey];
    choice.updatedAtUnixSeconds = NowUnixSeconds();
    TrimLocked(folderKey);
    return m_folders[folderKey];
}

void FolderTrackMemory::TrimLocked(const std::string& keep)
{
    while (m_folders.size() > maxFolders)
    {
        auto oldest = m_folders.end();
        for (auto it = m_folders.begin(); it != m_folders.end(); ++it)
        {
            if (it->first != keep &&
                (oldest == m_folders.end() || it->second.updatedAtUnixSeconds < oldest->second.updatedAtUnixSeconds))
            {
                oldest = it;
            }
        }
        m_folders.erase(oldest);
    }
}

void FolderTrackMemory::Load()
{
    if (m_file.empty() || !std::filesystem::exists(m_file))
    {
        return;
    }
    if (auto stored = Json::ReadJsonFromFile<std::unordered_map<std::string, FolderTrackChoice>>(m_file))
    {
        m_folders = std::move(*stored);
        return;
    }
    Log::Error(LogCategory::History, "could not read {}, starting over", File::ToUtf8(m_file));
}

void FolderTrackMemory::SaveLocked() const
{
    if (m_file.empty())
    {
        return;
    }

    std::error_code ignored;
    std::filesystem::create_directories(m_file.parent_path(), ignored);
    if (!Json::SaveJsonToFile(m_file, m_folders))
    {
        Log::Warning(LogCategory::History, "couldn't save {}", File::ToUtf8(m_file));
    }
}

void FolderTrackMemory::Changed() { m_changes.Notify(); }
