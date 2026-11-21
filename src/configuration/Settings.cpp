#include "Settings.hpp"
#include "../file/File.hpp"
#include "../file/Json.hpp"

Settings::Settings()
{
    std::lock_guard lock(m_mutex);
    Load();
}

std::filesystem::path Settings::FilePath() {
  return File::getApplicationDataPath() / "settings.json";
}

void Settings::SetChangeListener(ChangeListener::Handler handler) {
  GetInstance().m_changes.Set(std::move(handler));
}

std::optional<std::string> Settings::GetStringSetting(const std::string &key) {
  auto &instance = GetInstance();
  std::lock_guard lock(instance.m_mutex);
  auto &settings = instance.m_settings;

  auto it = settings.find(key);
  if (it == settings.end() || !it->second.has_value()) {
    return std::nullopt;
  }

  if (const auto *value = std::get_if<std::string>(&*it->second)) {
    return *value;
  }

  return std::nullopt;
}

std::optional<int> Settings::GetIntSetting(const std::string &key) {
  auto &instance = GetInstance();
  std::lock_guard lock(instance.m_mutex);
  auto &settings = instance.m_settings;

  auto it = settings.find(key);
  if (it == settings.end() || !it->second.has_value()) {
    return std::nullopt;
  }

  if (const auto *value = std::get_if<int>(&*it->second)) {
    return *value;
  }

  return std::nullopt;
}

std::optional<bool> Settings::GetBoolSetting(const std::string &key) {
  auto &instance = GetInstance();
  std::lock_guard lock(instance.m_mutex);
  auto &settings = instance.m_settings;

  auto it = settings.find(key);
  if (it == settings.end() || !it->second.has_value()) {
    return std::nullopt;
  }

  if (const auto *value = std::get_if<bool>(&*it->second)) {
    return *value;
  }

  return std::nullopt;
}

void Settings::SetBoolSetting(const std::string &key, bool value) {
  auto &instance = GetInstance();
  {
    std::lock_guard lock(instance.m_mutex);
    instance.m_settings[key] = value;
    instance.Save();
  }
  instance.m_changes.Notify();
}

std::vector<std::string> Settings::GetStringListSetting(const std::string &key) {
  auto &instance = GetInstance();
  std::lock_guard lock(instance.m_mutex);
  auto &settings = instance.m_settings;

  auto it = settings.find(key);
  if (it == settings.end() || !it->second.has_value()) {
    return {};
  }

  if (const auto *value = std::get_if<std::vector<std::string>>(&*it->second)) {
    return *value;
  }

  return {};
}

void Settings::SetStringListSetting(const std::string &key,
                                    const std::vector<std::string> &value) {
  auto &instance = GetInstance();
  {
    std::lock_guard lock(instance.m_mutex);
    instance.m_settings[key] = value;
    instance.Save();
  }
  instance.m_changes.Notify();
}

void Settings::SetStringSetting(const std::string &key,
                                const std::optional<std::string> &value) {
  auto &instance = GetInstance();
  {
    std::lock_guard lock(instance.m_mutex);

    if (value) {
      instance.m_settings[key] = *value;
    } else {
      instance.m_settings[key] = std::nullopt;
    }

    instance.Save();
  }
  instance.m_changes.Notify();
}

void Settings::Load() {
  const auto path = FilePath();

  auto settings =
      Json::ReadJsonFromFile<std::unordered_map<std::string, SettingValue>>(
          path);

  if (!settings) {
    return;
  }

  // Keys added since settings.json was written keep their defaults, and are
  // written back so they show up in the file for editing.
  bool addedDefaults = false;
  for (const auto &[key, value] : m_settings) {
    addedDefaults = settings->try_emplace(key, value).second || addedDefaults;
  }

  m_settings = std::move(*settings);

  if (addedDefaults) {
    Save();
  }
}

void Settings::Save() {
  const auto applicationFolder = File::getApplicationDataPath();

  std::filesystem::create_directories(applicationFolder);

  Json::SaveJsonToFile(applicationFolder / "settings.json", m_settings);
}
