#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace mango::remote::RemoteEntryId
{

// A video in a paired peer's library, as B's session and apps know it:
// "remote:<peerId>:<fastHash on the peer>".
inline constexpr std::string_view kPrefix = "remote:";

struct Parts
{
    std::string peerId;
    std::string fastHash;
};

[[nodiscard]] inline std::string Make(const std::string& peerId, const std::string& fastHash)
{
    return std::string(kPrefix) + peerId + ":" + fastHash;
}

[[nodiscard]] inline bool Is(std::string_view entryId) { return entryId.starts_with(kPrefix); }

// std::nullopt for a local fastHash or a malformed id.
[[nodiscard]] inline std::optional<Parts> Parse(std::string_view entryId)
{
    if (!Is(entryId))
    {
        return std::nullopt;
    }
    entryId.remove_prefix(kPrefix.size());
    const auto colon = entryId.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 == entryId.size())
    {
        return std::nullopt;
    }
    return Parts{std::string(entryId.substr(0, colon)), std::string(entryId.substr(colon + 1))};
}

}  // namespace mango::remote::RemoteEntryId
