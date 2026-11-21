#include "DirectorySnapshot.hpp"

namespace fs = std::filesystem;

void DirectorySnapshot::AddDirectory(const fs::path& directory)
{
    if (!m_directories.try_emplace(directory).second)
    {
        return;
    }

    if (const auto parent = m_directories.find(directory.parent_path());
        parent != m_directories.end() && directory.has_relative_path())
    {
        parent->second.subfolders.push_back(directory);
    }
}

void DirectorySnapshot::AddFile(const fs::path& file)
{
    if (const auto directory = m_directories.find(file.parent_path());
        directory != m_directories.end())
    {
        directory->second.files.push_back(file);
    }
}

bool DirectorySnapshot::Covers(const fs::path& directory) const
{
    return m_directories.contains(directory);
}

const std::vector<fs::path>& DirectorySnapshot::Files(const fs::path& directory) const
{
    return Find(directory).files;
}

const std::vector<fs::path>& DirectorySnapshot::Subfolders(const fs::path& directory) const
{
    return Find(directory).subfolders;
}

const DirectorySnapshot::Directory& DirectorySnapshot::Find(const fs::path& directory) const
{
    static const Directory empty;
    const auto it = m_directories.find(directory);
    return it != m_directories.end() ? it->second : empty;
}
