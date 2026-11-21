#include "LibrarySections.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <ranges>
#include <set>

namespace mango::LibrarySections
{

namespace
{

// One folder or subfolder while sections are being built.
struct FolderNode
{
    std::string sectionId;
    std::string name;
    std::vector<const LibraryEntry*> entries;
    std::set<std::string> entryHashes;
    std::map<std::string, std::unique_ptr<FolderNode>> children;

    // Including every subfolder: for dropping empty trees and the folders'
    // date order.
    int videoCount = 0;
    std::int64_t newestAddedAt = std::numeric_limits<std::int64_t>::min();
};

std::string SectionId(const std::string& folderId, const std::string& folderPath)
{
    return "folder:" + folderId + "/" + folderPath;
}

FolderNode& NodeAt(FolderNode& root, const std::string& folderPath, const std::string& folderId)
{
    FolderNode* node = &root;
    std::string path;

    for (const auto part : folderPath | std::views::split('/'))
    {
        const std::string component(part.begin(), part.end());
        if (component.empty())
        {
            continue;
        }
        path = path.empty() ? component : path + "/" + component;

        auto& child = node->children[component];
        if (!child)
        {
            child = std::make_unique<FolderNode>();
            child->sectionId = SectionId(folderId, path);
            child->name = component;
        }
        node = child.get();
    }
    return *node;
}

// The same file twice in one subfolder (a copy under another name) is still
// one video.
void AddEntry(FolderNode& node, const LibraryEntry& entry)
{
    if (node.entryHashes.insert(entry.fastHash).second)
    {
        node.entries.push_back(&entry);
    }
}

void ComputeTotals(FolderNode& node)
{
    node.videoCount = static_cast<int>(node.entries.size());
    for (const auto* entry : node.entries)
    {
        node.newestAddedAt = std::max(node.newestAddedAt, entry->addedAtUnixSeconds);
    }

    for (auto& [_, child] : node.children)
    {
        ComputeTotals(*child);
        node.videoCount += child->videoCount;
        node.newestAddedAt = std::max(node.newestAddedAt, child->newestAddedAt);
    }
}

// The tree of folder's subfolders holding at least one entry, or nullptr.
std::unique_ptr<FolderNode> BuildTree(const LibraryFolderEntry& folder, const std::vector<LibraryEntry>& entries)
{
    auto root = std::make_unique<FolderNode>();
    root->sectionId = SectionId(folder.folderId, "");
    root->name = folder.name;

    for (const auto& entry : entries)
    {
        for (const auto& location : entry.folderLocations)
        {
            if (location.folderId == folder.folderId)
            {
                AddEntry(NodeAt(*root, location.folderPath, folder.folderId), entry);
            }
        }
    }

    ComputeTotals(*root);
    return root->videoCount > 0 ? std::move(root) : nullptr;
}

// Name A→Z; date added by the newest video anywhere inside, newest first —
// so a folder that just got new videos moves up.
void SortNodes(std::vector<FolderNode*>& nodes, SortOrder order)
{
    std::ranges::stable_sort(nodes,
        [order](const FolderNode* a, const FolderNode* b)
        {
            if (order == SortOrder::DateAdded && a->newestAddedAt != b->newestAddedAt)
            {
                return a->newestAddedAt > b->newestAddedAt;
            }
            return CompareNames(a->name, b->name) < 0;
        });
}

std::vector<std::string> SortedHashes(std::vector<const LibraryEntry*> entries, SortOrder order)
{
    SortEntries(entries, order);
    std::vector<std::string> hashes;
    hashes.reserve(entries.size());
    for (const auto* entry : entries)
    {
        hashes.push_back(entry->fastHash);
    }
    return hashes;
}

void AppendSections(const FolderNode& node, std::vector<LibrarySection>& sections,
    std::vector<std::string> breadcrumb, const std::string& parentSectionId, SortOrder order)
{
    breadcrumb.push_back(node.name);
    sections.push_back(LibrarySection{node.sectionId, "folder", breadcrumb, parentSectionId, SortedHashes(node.entries, order)});

    std::vector<FolderNode*> children;
    for (const auto& [_, child] : node.children)
    {
        children.push_back(child.get());
    }
    SortNodes(children, order);

    for (const auto* child : children)
    {
        AppendSections(*child, sections, breadcrumb, node.sectionId, order);
    }
}

char LowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

// Compares the digit runs starting at a[i] and b[j] by value, and moves i
// and j past them.
int CompareNumbers(std::string_view a, std::size_t& i, std::string_view b, std::size_t& j)
{
    while (i < a.size() && a[i] == '0' && i + 1 < a.size() && IsDigit(a[i + 1]))
    {
        ++i;
    }
    while (j < b.size() && b[j] == '0' && j + 1 < b.size() && IsDigit(b[j + 1]))
    {
        ++j;
    }

    const auto startA = i;
    const auto startB = j;
    while (i < a.size() && IsDigit(a[i]))
    {
        ++i;
    }
    while (j < b.size() && IsDigit(b[j]))
    {
        ++j;
    }

    const auto digitsA = a.substr(startA, i - startA);
    const auto digitsB = b.substr(startB, j - startB);
    if (digitsA.size() != digitsB.size())
    {
        return digitsA.size() < digitsB.size() ? -1 : 1;
    }
    return digitsA.compare(digitsB);
}

}  // namespace

std::optional<SortOrder> SortOrderFromString(std::string_view text)
{
    if (text == "name")
    {
        return SortOrder::Name;
    }
    if (text == "dateAdded")
    {
        return SortOrder::DateAdded;
    }
    return std::nullopt;
}

std::string ToString(SortOrder order) { return order == SortOrder::Name ? "name" : "dateAdded"; }

int CompareNames(std::string_view a, std::string_view b)
{
    std::size_t i = 0;
    std::size_t j = 0;

    while (i < a.size() && j < b.size())
    {
        if (IsDigit(a[i]) && IsDigit(b[j]))
        {
            if (const int numbers = CompareNumbers(a, i, b, j); numbers != 0)
            {
                return numbers;
            }
            continue;
        }

        // Bytes, not code points: UTF-8 keeps the order of code points, and
        // only ASCII letters fold case.
        const auto ca = static_cast<unsigned char>(LowerAscii(a[i]));
        const auto cb = static_cast<unsigned char>(LowerAscii(b[j]));
        if (ca != cb)
        {
            return ca < cb ? -1 : 1;
        }
        ++i;
        ++j;
    }

    if (i < a.size())
    {
        return 1;
    }
    return j < b.size() ? -1 : 0;
}

void SortEntries(std::vector<const LibraryEntry*>& entries, SortOrder order)
{
    std::ranges::sort(entries,
        [order](const LibraryEntry* a, const LibraryEntry* b)
        {
            if (order == SortOrder::DateAdded && a->addedAtUnixSeconds != b->addedAtUnixSeconds)
            {
                return a->addedAtUnixSeconds > b->addedAtUnixSeconds;
            }
            if (const int names = CompareNames(a->title, b->title); names != 0)
            {
                return names < 0;
            }
            return a->fastHash < b->fastHash;
        });
}

namespace
{

// One section per paired device, in the order the devices come, after the
// local ones: "remote:<peerId>", the device's name as its breadcrumb.
void AppendRemoteSections(const std::vector<LibraryEntry>& entries, SortOrder order, std::vector<LibrarySection>& sections)
{
    std::vector<std::string> peers;
    std::map<std::string, std::vector<const LibraryEntry*>> byPeer;
    for (const auto& entry : entries)
    {
        if (entry.remotePeerId.empty())
        {
            continue;
        }
        auto& peerEntries = byPeer[entry.remotePeerId];
        if (peerEntries.empty())
        {
            peers.push_back(entry.remotePeerId);
        }
        peerEntries.push_back(&entry);
    }

    for (const auto& peerId : peers)
    {
        auto& peerEntries = byPeer[peerId];
        const auto name = peerEntries.front()->remotePeerName;
        sections.push_back(
            LibrarySection{std::string(remotePrefix) + peerId, "remote", {name}, "", SortedHashes(peerEntries, order)});
    }
}

}  // namespace

std::vector<LibrarySection> Build(
    const std::vector<LibraryEntry>& entries, const std::vector<LibraryFolderEntry>& folders, SortOrder order)
{
    std::vector<LibrarySection> sections;

    std::vector<const LibraryEntry*> manual;
    for (const auto& entry : entries)
    {
        if (entry.addedManually)
        {
            manual.push_back(&entry);
        }
    }
    if (!manual.empty())
    {
        sections.push_back(LibrarySection{std::string(manualId), "manual", {}, "", SortedHashes(manual, order)});
    }

    std::vector<std::unique_ptr<FolderNode>> roots;
    std::vector<FolderNode*> rootNodes;
    for (const auto& folder : folders)
    {
        if (auto root = BuildTree(folder, entries))
        {
            rootNodes.push_back(root.get());
            roots.push_back(std::move(root));
        }
    }
    SortNodes(rootNodes, order);

    for (const auto* root : rootNodes)
    {
        AppendSections(*root, sections, {}, "", order);
    }
    AppendRemoteSections(entries, order, sections);
    return sections;
}

std::optional<std::string> NextFastHash(
    const std::vector<LibrarySection>& sections, const std::string& sectionId, const std::string& fastHash)
{
    const auto section = std::ranges::find(sections, sectionId, &LibrarySection::sectionId);
    if (section == sections.end())
    {
        return std::nullopt;
    }

    const auto& hashes = section->fastHashes;
    const auto current = std::ranges::find(hashes, fastHash);
    if (current == hashes.end() || std::next(current) == hashes.end())
    {
        return std::nullopt;
    }
    return *std::next(current);
}

}  // namespace mango::LibrarySections
