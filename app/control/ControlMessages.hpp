#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <glaze/glaze.hpp>

#include "console/ConsoleText.hpp"

namespace mango::cli
{

// One line on the control socket (Linux, see ControlServer.hpp), as JSON.
// Only the fields its type uses are set:
//   hello    server → client on connect: version, owner ("service" or
//            "terminal")
//   command  client → server: id, line (what was typed at the prompt)
//   output   server → client: id, text (part of what the command printed)
//   done     server → client: id (the command finished)
//   event    server → every client: text (printed on its own: notices)
struct ControlMessage
{
    std::string type{};
    std::optional<std::uint64_t> id{};
    std::optional<std::string> line{};
    std::optional<std::string> text{};
    std::optional<std::string> version{};
    std::optional<std::string> owner{};
};

// One line of JSON, without the newline. glaze escapes the newlines inside
// text, so a message never spans lines.
[[nodiscard]] inline std::string WriteControlMessage(const ControlMessage& message)
{
    const auto json = glz::write_json(message);
    return json ? *json : std::string();
}

// nullopt for a line that isn't a message.
[[nodiscard]] inline std::optional<ControlMessage> ReadControlMessage(const std::string& line)
{
    return ParseJson<ControlMessage>(line);
}

}  // namespace mango::cli
