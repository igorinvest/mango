#include "console/Output.hpp"

#include <iostream>

namespace mango::cli
{

void StdoutOutput::Write(std::string_view text)
{
    std::lock_guard lock(m_mutex);
    std::cout << text << std::flush;
}

}  // namespace mango::cli
