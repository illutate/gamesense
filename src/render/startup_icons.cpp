#include "skeet/render/startup_icons.hpp"
#include "skeet/render/icon_cache.hpp"

namespace skeet::ui {
void initialize_startup_icons(IconCache& cache, StartupIcons& icons) { // 433283A5
    icons.grenade =
        cache.load_svg("materials/panorama/images/icons/equipment/hegrenade.svg", 0, -1);
    icons.inferno = cache.load_svg("materials/panorama/images/icons/equipment/inferno.svg", 0, -1);
    icons.bomb = cache.load_svg("materials/panorama/images/icons/ui/bomb_c4.svg", 0, -1);
}
}
