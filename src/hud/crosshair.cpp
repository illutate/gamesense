#include "skeet/hud/crosshair.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstring>

namespace skeet::hud {
namespace {
template <class T> T read(const void* object, std::uint32_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void write(void* object, std::uint32_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
std::int32_t variable_integer(const void* object) {
    for (;;) {
        const auto* parent = read<const void*>(object, 0x1C);
        if (parent == object)
            break;
        object = parent;
    }
    return std::bit_cast<std::int32_t>(read<std::uint32_t>(object, 0x30) ^
                                       std::uint32_t(reinterpret_cast<std::uintptr_t>(object)));
}
std::int32_t sub(std::int32_t first, std::uint32_t second) {
    return std::bit_cast<std::int32_t>(std::uint32_t(first) - second);
}
std::uint32_t return_hash(const std::byte* address) {
    auto hash = 0x811C9DC5u;
    if (address)
        for (unsigned i = 0; i != 6; ++i)
            hash = (hash ^ std::to_integer<std::uint8_t>(address[i])) * 0x01000193u;
    return hash ^ 0x92CA5DF1u;
}
struct BoundCrosshairAccess {
    const CrosshairBindings& bindings;
    const Point& dimensions;
    std::uint32_t life() const {
        return bindings.offsets.life_state;
    }
    std::uint32_t scoped() const {
        return bindings.offsets.scoped;
    }
    std::uint32_t weapon() const {
        return bindings.offsets.active_weapon;
    }
    std::uint32_t flag() const {
        return bindings.offsets.weapon_draw_flag;
    }
    void* entities() const {
        return bindings.entity_list;
    }
    const void* style() const {
        return bindings.style_variable;
    }
    const void* mode() const {
        return bindings.weapon_mode_variable;
    }
    Point screen(const ui::RenderQueue&) const {
        return dimensions;
    }
};
struct NativeCrosshairAccess {
    const NativeCrosshairBindings& bindings;
    std::uint32_t offset(std::uint32_t pointer, std::uint32_t key, std::uint32_t mask) const {
        const auto& globals = bindings.offsets.globals;
        return *globals.pointers.at(pointer) ^ globals.keys.at(key) ^ mask;
    }
    std::uint32_t life() const {
        return offset(0x43468e78, 0x434684e4, 0x9d920e35);
    }
    std::uint32_t scoped() const {
        return offset(0x43468da4, 0x434684ec, 0xcacaeecc);
    }
    std::uint32_t weapon() const {
        return offset(0x43467a74, 0x434678c0, 0xcd812654);
    }
    std::uint32_t flag() const {
        return offset(0x43469230, 0x434679a8, 0xa1af8ffc);
    }
    void* entities() const {
        return bindings.entity_list;
    }
    const void* style() const {
        return bindings.style_variable;
    }
    const void* mode() const {
        return bindings.weapon_mode_variable;
    }
    Point screen(const ui::RenderQueue& queue) const {
        return queue.screen();
    }
};
template <class Access, class Suspended>
void draw_crosshair_body(ui::RenderQueue& queue, const esp::ScreenOverlayConfig& config,
                         const Access& access, void* local, const Suspended& suspended) {
    if (!config.crosshair.value || !local)
        return;
    if (read<std::uint8_t>(local, access.life()) || read<std::uint8_t>(local, access.scoped()))
        return;
    if (variable_integer(access.style()) < 2)
        return;
    const auto handle = read<std::int32_t>(local, access.weapon());
    if (handle == -1)
        return;
    auto* weapon = engine::invoke<void*>(access.entities(), 4, handle);
    if (!weapon)
        return;
    auto* data = engine::invoke<void*>(weapon, 461);
    if (!data)
        return;
    const auto type = read<std::int32_t>(data, 0xC8);
    if (type == 0) {
        const auto* mode = access.mode();
        if (mode && variable_integer(mode) != 0)
            return;
    } else if (type != 5)
        return;
    if (suspended) {
        const auto screen_size = access.screen(queue);
        const auto x = screen_size.x / 2;
        const auto y = screen_size.y / 2;
        queue.rectangle({sub(x, 2), sub(y, 2), 5, 5, 0xF0000000u});
        queue.rectangle({sub(x, 1), sub(y, 1), 3, 3, 0xFF00EEC4u});
    } else {
        const auto flag_offset = access.flag();
        const auto flag = read<std::uint8_t>(weapon, flag_offset);
        write(data, 0xC8, std::int32_t(3));
        write(weapon, access.flag(), std::uint8_t(0));
        engine::invoke<void>(weapon, 404);
        write(data, 0xC8, type);
        write(weapon, flag_offset, flag);
    }
}
} //namespace

void draw_crosshair(ui::RenderQueue& queue, const esp::ScreenOverlayConfig& config,
                    const CrosshairBindings& bindings, void* local, bool suspended,
                    Point screen_size) {
    draw_crosshair_body(queue, config, BoundCrosshairAccess{bindings, screen_size}, local,
                        suspended);
}
void draw_live_crosshair(ui::RenderQueue& queue, const esp::ScreenOverlayConfig& config,
                         const NativeCrosshairBindings& bindings, void* local,
                         const std::uint8_t& suspended) {
    draw_crosshair_body(queue, config, NativeCrosshairAccess{bindings}, local, suspended);
}
void prepare_crosshair_view(const esp::ScreenOverlayConfig& config,
                            const CrosshairBindings& bindings, void* local, void* view_setup,
                            bool suspended, const std::byte* return_address,
                            void** original_vtable) {
    prepare_crosshair_view(config, CrosshairViewBindings{bindings.entity_list, bindings.offsets},
                           local, view_setup, suspended, return_address, original_vtable);
}
void prepare_crosshair_view(const esp::ScreenOverlayConfig& config,
                            const CrosshairViewBindings& bindings, void* local, void* view_setup,
                            bool suspended, const std::byte* return_address,
                            void** original_vtable) {
    if (config.crosshair.value && !suspended && view_setup &&
        return_hash(return_address) == 0xA3C52284u) {
        const auto& offsets = bindings.offsets;
        if (!read<std::uint8_t>(local, offsets.life_state) &&
            !read<std::uint8_t>(local, offsets.scoped)) {
            const auto handle = read<std::int32_t>(local, offsets.active_weapon);
            if (handle != -1) {
                auto* weapon = engine::invoke<void*>(bindings.entity_list, 4, handle);
                if (weapon) {
                    auto* data = engine::invoke<void*>(weapon, 461);
                    if (data) {
                        const auto type = read<std::int32_t>(data, 0xC8);
                        if (type == 0 || type == 5)
                            write(view_setup, 0xE0, std::uint16_t(0x0101));
                    }
                }
            }
        }
    }
    reinterpret_cast<engine::MemberFunction<void>>(original_vtable[294])(local);
}
} // namespace skeet::hud
