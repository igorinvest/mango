#include "SleepActivity.hpp"

#include <algorithm>

namespace mango
{

void SleepActivity::Note(Clock::time_point at) { m_lastActivity = std::max(m_lastActivity.value_or(at), at); }

bool SleepActivity::ShouldPrevent(Clock::time_point now) const
{
    return m_lastActivity && now - *m_lastActivity < gracePeriod;
}

}  // namespace mango
