#include "skeet/rage/spread_miss_log.hpp"
#include "skeet/rage/settings.hpp"
#include "skeet/misc/logs.hpp"

namespace skeet::rage {
void log_spread_miss(const ShotLogSettings& settings, misc::NormalLog& output) {
    if (!settings.spread_misses.value)
        return;
    //4347F7CC..4347F856 recovers these25 bytes for the owner+5C string.
    output.format("%s", "Missed shot due to spread");
}
}
