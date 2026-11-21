#pragma once

#include <optional>
#include <string>
#include <string_view>

// The random ID a stream is served under: /stream/<id>/<file> and
// /cover/<id>. Unlike the file's fastHash it can't be derived from the file
// or guessed, and it dies with the stream (see StreamManager), so only the
// current stream is reachable, and only to whoever was handed its URL.
class StreamId
{
public:
    // 128 bits from the OS's random source, as 32 lowercase hex digits.
    static std::string Generate();

    // "<id>/<fileName>", relative to the server's /stream/.
    static std::string EntryPath(const std::string& streamId, const std::string& fileName);

    // The <id> of a ".../stream/<id>/<file>" URL, or std::nullopt for
    // anything else.
    static std::optional<std::string> FromStreamUrl(std::string_view url);
};
