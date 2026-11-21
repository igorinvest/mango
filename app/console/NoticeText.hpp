#pragma once

#include <string>

#include "session/SessionState.hpp"

namespace mango::cli
{

// One line for a notice: "[error] Couldn't start Movie.mkv: no video
// encoder." English only; an unknown code gets a general text by severity.
[[nodiscard]] std::string NoticeLine(const mango::SessionNotice& notice);

}  // namespace mango::cli
