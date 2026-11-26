#include "skeet/effects/glow.hpp"
#include "skeet/effects/glow_engine.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cstring>

#if defined(_M_IX86) && !defined(__i386__)
#error The captured glow registration ABI requires the x86 Clang/GNU inline-assembly adapter.
#endif

namespace skeet::effects {
namespace {
template <class T> T field(const void* object, std::uint32_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void store(void* object, std::uint32_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
std::array<float, 4> color(std::uint32_t packed, float scale = 0.003921568859368563f) {
    std::array<float, 4> result;
    for (unsigned i = 0; i != 4; ++i)
        result[i] = float(std::uint8_t(packed >> (i * 8))) * scale;
    return result;
}
std::int32_t register_glow(GlowEngine& engine, void* entity, const float* rgb, float alpha,
                           bool occluded) {
#if defined(__i386__)
    // The captured client entry consumes alpha in XMM3 and three zero floats
    // in XMM0..2. Its remaining arguments use the Microsoft fastcall ABI.
    struct Call {
        void* function;
        GlowRegistry* manager;
        void* entity;
        const float* color;
        std::uint32_t occluded, unoccluded;
        std::int32_t split;
        float alpha;
    };
    const Call call{
        engine.registration_function, engine.registry, entity, rgb, occluded, 0, -1, alpha};
    std::int32_t result;
    asm volatile("pushl %%esi\n\t"
                 "movl %[packet], %%esi\n\t"
                 "movl 4(%%esi), %%ecx\n\t"
                 "xorl %%edx, %%edx\n\t"
                 "xorps %%xmm0, %%xmm0\n\t"
                 "xorps %%xmm1, %%xmm1\n\t"
                 "xorps %%xmm2, %%xmm2\n\t"
                 "movss 28(%%esi), %%xmm3\n\t"
                 "pushl 24(%%esi)\n\t"
                 "pushl 20(%%esi)\n\t"
                 "pushl 16(%%esi)\n\t"
                 "pushl 12(%%esi)\n\t"
                 "pushl 8(%%esi)\n\t"
                 "call *0(%%esi)\n\t"
                 "popl %%esi"
                 : "=a"(result)
                 : [packet] "r"(&call)
                 : "ecx", "edx", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
                   "memory", "cc");
    return result;
#else
    return reinterpret_cast<GlowEngine::HostRegistration>(engine.registration_function)(
        engine.registry, entity, rgb, alpha, occluded, false, -1);
#endif
}
}

void update_glow(GlowEngine& engine, void* entity, std::uint32_t index,
                 const std::array<float, 4>& rgba) {
    auto* manager = engine.registry;
    if (!manager || !engine.registration_function || index >= std::uint32_t(manager->count))
        return;
    auto* object = manager->objects + index;
    if (!object || (object->rgba == rgba && object->render_occluded != 0) ||
        object->entity != entity || object->next_free != -2)
        return;
    const auto saved = manager->first_free;
    manager->first_free = std::int32_t(index);
    register_glow(engine, entity, rgba.data(), rgba[3], true);
    manager->first_free = saved;
}
void clear_glow(GlowEngine& engine, void* entity, std::uint32_t index) {
    auto* manager = engine.registry;
    if (!manager || !engine.registration_function || index >= std::uint32_t(manager->count))
        return;
    auto* object = manager->objects + index;
    if (!object || (object->rgba[3] == 0.0f && object->render_occluded == 0) ||
        object->entity != entity || object->next_free != -2)
        return;
    const auto saved = manager->first_free;
    manager->first_free = std::int32_t(index);
    const std::array<float, 3> zero{};
    register_glow(engine, entity, zero.data(), 0, false);
    manager->first_free = saved;
}
void GlowRuntime::clear_bomb() {
    if (bomb_handle == -1)
        return;
    void* bomb = entities.by_handle(bomb_handle);
    if (bomb) {
        const auto index = field<std::int32_t>(bomb, entities.bomb_glow_offset());
        if (index > 0)
            clear_glow(engine, bomb, std::uint32_t(index));
    }
    bomb_handle = -1;
}
void GlowRuntime::update() {
    using engine::invoke;
    void* local_player;
    if (players.glow.value && !entities.suspended() && entities.activation() &&
        (local_player = entities.local_player()) && engine.registry) {
        const auto rgba = color(players.glow_color.value);
        const auto limit = entities.max_clients();
        for (std::int32_t index = 1; index <= limit; ++index) {
            void* entity = entities.by_index(index);
            if (!entity || entities.life_state(entity) || invoke<std::int32_t>(entity, 122) <= 0 ||
                invoke<bool>(static_cast<std::byte*>(entity) + 8, 9) ||
                entities.suppressed(entity) || entities.eligibility_state(entity))
                continue;
            if (!players.teammates.value && !entities.enemy(local_player, entity))
                continue;
            if (entities.disable_visuals(std::uint32_t(index)))
                continue;
            update_glow(engine, entity, entities.player_glow_index(entity), rgba);
        }
    }
    bool enable_weapons = true;
    if (!(world.dropped_weapons.value & 4) || entities.suspended() || !entities.activation()) {
        if (!weapons_active)
            goto bomb;
        enable_weapons = false;
    }
    weapons_active = enable_weapons;
    if (engine.registry) {
        const auto rgba = color(world.dropped_weapon_color.value);
        const auto limit = entities.entity_limit();
        for (std::int32_t index = entities.max_clients() + 1; index < limit; ++index) {
            if (std::uint32_t(index) & 0xFFFF0000u)
                continue;
            void* entity = entities.by_index(index);
            if (!entity || !invoke<bool>(entity, 166))
                continue;
            const auto glow = entities.weapon_glow_index(entity);
            if (enable_weapons)
                update_glow(engine, entity, glow, rgba);
            else
                clear_glow(engine, entity, glow);
        }
    }
bomb:
    auto* planted_bombs = entities.planted_bombs();
    if (!planted_bombs)
        return;
    if (world.bomb.enabled.value && !entities.suspended() && entities.activation() &&
        field<std::int32_t>(planted_bombs, 0xC)) {
        auto* entries = field<void**>(planted_bombs, 0);
        void* entity = entries[0];
        if (entity && entities.bomb_active(entity)) {
            const auto handle = invoke<const std::int32_t*>(entity, 2);
            if (*handle != bomb_handle)
                clear_bomb();
            auto rgba = color(world.bomb.color.value, 0.003921568393707275f);
            rgba[3] = 1.0f;
            const auto glow_offset = entities.bomb_glow_offset();
            const auto index = field<std::int32_t>(entity, glow_offset);
            if (index == -1) {
                const auto assigned = engine.registration_function
                                          ? register_glow(engine, entity, rgba.data(), 1, true)
                                          : -1;
                store(entity, glow_offset, assigned);
            } else
                update_glow(engine, entity, std::uint32_t(index), rgba);
            bomb_handle = *handle;
            return;
        }
    }
    clear_bomb();
}

} // namespace skeet::effects
