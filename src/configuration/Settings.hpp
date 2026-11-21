#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "ChangeListener.hpp"

class Settings {
public:
  Settings(const Settings&) = delete;
  Settings& operator=(const Settings&) = delete;
  Settings(Settings&&) = delete;
  Settings& operator=(Settings&&) = delete;

  static Settings& GetInstance()
  {
    static Settings instance;
    return instance;
  }

  static std::optional<std::string> GetStringSetting(const std::string &key);
  static void SetStringSetting(const std::string &key, const std::optional<std::string> &value);

  // std::nullopt when key is unset, null, or holds a string.
  static std::optional<int> GetIntSetting(const std::string &key);

  // std::nullopt when key is unset, null, or holds a non-bool.
  static std::optional<bool> GetBoolSetting(const std::string &key);
  static void SetBoolSetting(const std::string &key, bool value);

  // Empty when key is unset, null, or holds anything but a list of strings.
  static std::vector<std::string> GetStringListSetting(const std::string &key);
  static void SetStringListSetting(const std::string &key, const std::vector<std::string> &value);

  // Called after every setter, on the setter's thread, without the lock.
  // In-process only: settings.json is read once at start, so a change
  // another process saves there is seen at the next start.
  static void SetChangeListener(ChangeListener::Handler handler);

 private:
  using SettingValue = std::optional<std::variant<bool, int, std::string, std::vector<std::string>>>;

  Settings();

  // Callers hold m_mutex.
  void Load();
  void Save();
  [[nodiscard]] static std::filesystem::path FilePath();

  // Guards m_settings: settings are read from HTTP and stream threads and
  // written from the session thread.
  mutable std::mutex m_mutex;
  ChangeListener m_changes;

  std::unordered_map<std::string, SettingValue> m_settings = {
      // Track preferences (see TrackPreferencesStore).
      {"audioLanguages", std::vector<std::string>{}},
      {"subtitleMode", "foreignAudioOnly"},
      {"subtitleLanguages", std::vector<std::string>{}},
      {"streamsFolder", "streams"},
      {"libraryMetadataFolder", "library-metadata"},
      {"videoTranscodeMode", "automatic"},
      {"encodingPreset", "1080p"},
      {"streamStartDelayMs", 3000},
      {"streamStartReadyPercent", 10},
      {"autoplayNextInFolder", true},
      {"listenOnlyWhileStreaming", true},
      // Remote streaming (see src/remote/RemoteStreaming.hpp). An unset
      // name is the host name. No signaling URL: stays disconnected.
      // remoteStreamingAllowed false refuses paired devices' connections
      // (pairing still works, and turns it back on).
      {"remoteStreamingEnabled", false},
      {"remoteStreamingAllowed", true},
      {"remoteDeviceName", std::nullopt},
      {"remoteSignalingUrl", std::nullopt},
      // The app session's (see src/session/SessionCore.cpp).
      {"librarySortOrder", "dateAdded"},
      {"lastUsedDevice", std::nullopt},
      // Debug-level lines in Release builds (see src/log/Log.cpp).
      {"logDebug", false},
      // The apps' system notifications (see src/session/SessionState.hpp).
      {"showSystemNotifications", true}};
};
