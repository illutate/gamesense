#include "grenade_live_detail.hpp"
#include "skeet/movement/collision.hpp"
#include <algorithm>

namespace skeet::esp {
using namespace grenade_live_detail;
void GrenadeLiveScene::Storage::assess(GrenadeWarningRecord& record, void* selected,
                                       std::uint32_t& buttons) {
    const auto& o = raw.bindings().offsets;
    if (record.type == 0) {
        const auto remaining = record.end_time - bindings.current_time;
        auto origin = raw.live_origin(selected);
        const auto velocity = field<Vector3>(selected, o.velocity);
        if ((velocity.y * velocity.y + velocity.x * velocity.x) > 400.0f) {
            const auto time = clamp(remaining, 0.2f, 1.6f);
            const Vector3 predicted{time * velocity.x + origin.x, time * velocity.y + origin.y,
                                    time * velocity.z + origin.z};
            if (squared(difference(origin, record.position)) >
                squared(difference(predicted, record.position))) {
                const auto delta = difference(predicted, origin);
                MovementTraceFlagGuard guard(bindings.trace_flag);
                const auto hit =
                    raw.trace({origin, delta, {}, squared(delta) != 0, true}, 1,
                              {GrenadeDamageFilterKind::ignore_entity, identity(selected)});
                if (!hit.start_solid)
                    origin = hit.end;
            }
        }
        auto value = damage(selected, record.position, origin).damage;
        if (value > 0.0f)
            value *= damage_scale(record.handle, record.type);
        if (record.damage > value && remaining > 0.0f && remaining < 0.03125f)
            buttons = (buttons & 0xfffffbffu) | 0x200u;
        record.damage = value;
    } else if (record.type == 1) {
        bool covered = false;
        for (auto* smoke : raw.collect_smokes(record.position, 166.0f, 0x80, false)) {
            if (!field<std::uint8_t>(smoke, o.smoke_active))
                continue;
            if (grenade_warning_smoke_overlap(record.position, raw.live_origin(smoke))) {
                covered = true;
                break;
            }
        }
        record.suppressed = covered;
        const auto origin = raw.live_origin(selected);
        record.damage = squared(difference(record.position, origin)) < 22500.0f ? 100.0f : 0.0f;
    } else if (record.type == 2) {
        if (record.suppressed) {
            record.damage = 0.0f;
            return;
        }
        const auto origin = raw.live_origin(selected);
        auto* inferno = record.handle == 0xffffffffu ? nullptr : raw.by_handle(record.handle);
        record.damage = 0.0f;
        if (!inferno)
            return;
        const auto center = field<Vector3>(inferno, o.origin);
        const auto count = field<std::int32_t>(inferno, o.fire_count);
        auto closest = std::sqrt(squared(difference(origin, center)));
        for (std::int32_t cell = 0; cell < count; ++cell) {
            if (!field<std::uint8_t>(inferno, o.fire_active + cell))
                continue;
            const Vector3 point{float(field<std::int32_t>(inferno, o.fire_x + cell * 4)) + center.x,
                                float(field<std::int32_t>(inferno, o.fire_y + cell * 4)) + center.y,
                                float(field<std::int32_t>(inferno, o.fire_z + cell * 4)) +
                                    center.z};
            const auto next = std::sqrt(squared(difference(point, origin)));
            closest = _mm_cvtss_f32(_mm_min_ss(_mm_set_ss(next), _mm_set_ss(closest)));
        }
        if (closest < 150.0f)
            record.damage = 100.0f;
    }
}
void GrenadeLiveScene::Storage::update(GrenadeWarningOwner& owner, const GrenadeEspConfig& config,
                                       void* selected, std::uintptr_t incoming) {
    const auto& b = bindings;
    const auto& o = raw.bindings().offsets;
    std::erase_if(owner.records, [&](const GrenadeWarningRecord& record) {
        if (record.type < 2) {
            auto* entity = record.handle == 0xffffffffu ? nullptr : raw.by_handle(record.handle);
            if (!entity || field<std::int32_t>(entity, o.explosion_effect_tick) != 0)
                return true;
        }
        const auto expiration =
            truncate((record.end_time + 1.0f) / b.queries.environment().tick_interval + 0.5f);
        return b.tick_count > expiration;
    });
    if (!selected || (!config.proximity_warning.value && !config.molotov_area.value &&
                      !config.smoke_area.value && !b.grenade_entities_enabled))
        return;
    const auto existing = owner.records.size();
    for (std::size_t ordinal = 0; ordinal < existing; ++ordinal) {
        auto& record = owner.records[ordinal];
        auto* entity = record.handle == 0xffffffffu ? nullptr : raw.by_handle(record.handle);
        if (!entity || raw.dormant(entity) || record.type > 1 ||
            field<std::int32_t>(entity, o.explosion_effect_tick) != 0 ||
            (field<std::uint32_t>(entity, o.effects) & 0x20u))
            continue;
        const auto thrower_handle = field<std::uint32_t>(entity, o.thrower_handle);
        if (thrower_handle == 0xffffffffu || !raw.by_handle(thrower_handle))
            continue;
        owner.projectile_points.clear();
        owner.projectile_points.push_back(raw.live_origin(entity));
        const auto velocity = field<Vector3>(entity, o.velocity);
        const auto initial = field<Vector3>(entity, o.initial_velocity);
        const auto current = squared(difference(velocity, initial)) < 1.0f
                                 ? float(b.tick_count) * b.queries.environment().tick_interval
                                 : field<float>(entity, o.simulation_time);
        if (!(squared(velocity) > 900.0f))
            continue;
        GrenadeState prediction{};
        prediction.owner_entity = identity(entity);
        const auto bounce = field<std::uint16_t>(entity, o.projectile_flags);
        const auto group = field<std::int32_t>(entity, o.collision_group);
        simulate_grenade(prediction, record.kind, field<Vector3>(entity, o.origin),
                         field<Vector3>(entity, o.velocity), current, record.creation_time,
                         b.queries.environment(), raw.provider(b.trace_flag), nullptr, nullptr,
                         group, bounce);
        finalize_grenade(prediction, raw.provider(b.trace_flag));
        if (record.kind == GrenadeKind::explosive)
            prediction.position.z += 1.0f;
        if (squared(difference(prediction.position, record.position)) > 4096.0f)
            record.position = prediction.position;
    }
    auto try_add_entity = [&](std::int32_t ent_idx) {
        if (ent_idx <= 64)
            return;
        const auto index_u = static_cast<std::uint32_t>(ent_idx);
        for (const auto& record : owner.records)
            if ((record.handle == 0xffffffffu ? 0x1fffu : record.handle & 0xfffu) == index_u)
                return;
        auto* entity = raw.by_index(ent_idx);
        if (!entity || raw.dormant(entity) || (field<std::uint32_t>(entity, o.effects) & 0x20u))
            return;
        const auto klass = raw.client_class(entity);
        std::uint32_t type = 0;
        GrenadeKind kind = GrenadeKind::explosive;
        float lifetime = 0.0f;
        if (klass == raw.bindings().base_projectile_class) {
            if (field<float>(entity, o.damage) != 350.0f)
                return;
            type = 0;
            kind = GrenadeKind::explosive;
            lifetime = 1.5f;
        } else if (klass == raw.bindings().molotov_projectile_class) {
            type = 1;
            kind = GrenadeKind::molotov;
            lifetime = b.queries.environment().incendiary_duration();
        } else if (klass == raw.bindings().inferno_class) {
            const auto handle = field<std::uint32_t>(entity, o.owner_handle);
            auto* actor = handle == 0xffffffffu ? nullptr : raw.by_handle(handle);
            const bool harmful = admitted(2, actor, selected);
            if (!harmful && !config.molotov_area.value)
                return;
            const auto start = float(field<std::int32_t>(entity, o.inferno_start_tick)) *
                               b.queries.environment().tick_interval;
            const auto reference = *engine::invoke<const std::uint32_t*>(entity, 2);
            const auto origin = field<Vector3>(entity, o.origin);
            owner.records.push_back({reference, origin, 0.0f, 2, GrenadeKind::molotov, !harmful,
                                     start, start + 7.0f});
            return;
        } else {
            return;
        }
        if (field<std::int32_t>(entity, o.explosion_effect_tick) != 0)
            return;
        const auto owner_handle = field<std::uint32_t>(entity, o.thrower_handle);
        if (owner_handle == 0xffffffffu)
            return;
        auto* actor = raw.by_handle(owner_handle);
        if (!admitted(type, actor, selected))
            return;
        const auto initial_z = field<float>(entity, o.initial_velocity + 8);
        const auto current_z = field<float>(entity, o.velocity + 8);
        const auto elapsed =
            grenade_projectile_elapsed(initial_z, current_z, b.queries.environment().gravity(),
                                       b.queries.environment().tick_interval);
        if (elapsed > lifetime + 2.0f)
            return;
        const auto clamped_elapsed =
            std::clamp(elapsed, 0.0f, std::max(0.0f, lifetime - b.queries.environment().tick_interval));
        const auto reference = *engine::invoke<const std::uint32_t*>(entity, 2);
        const auto engine_time = engine::invoke<float>(b.engine_client, 14);
        const auto rounded = static_cast<float>(
            static_cast<long double>(engine_time) / b.engine_clock_interval + 0.5L);
        const auto current = float(truncate(rounded)) * b.queries.environment().tick_interval;
        const auto sim_time = field<float>(entity, o.simulation_time);
        const auto creation = (sim_time > 0.0f ? sim_time : current) - clamped_elapsed;
        if (actor && raw.player(actor))
            (void)raw.dormant(actor);
        GrenadeState prediction{};
        prediction.owner_entity = identity(entity);
        const auto bounce = field<std::uint16_t>(entity, o.projectile_flags);
        const auto group = field<std::int32_t>(entity, o.collision_group);
        simulate_grenade(prediction, kind, field<Vector3>(entity, o.origin),
                         field<Vector3>(entity, o.velocity), current, creation,
                         b.queries.environment(), raw.provider(b.trace_flag), nullptr, nullptr,
                         group, bounce);
        GrenadeWarningRecord record{reference, {},    0.0f,     type,
                                    kind,      false, creation, prediction.time};
        finalize_grenade(prediction, raw.provider(b.trace_flag));
        if (kind == GrenadeKind::explosive)
            prediction.position.z += 1.0f;
        else if (prediction.sky)
            return;
        record.position = prediction.position;
        assess(record, selected, b.command_buttons);
        owner.records.push_back(record);
    };
    if (incoming) {
        auto* packet = reinterpret_cast<const void*>(incoming);
        for (std::int32_t ordinal = 0; ordinal < field<std::int32_t>(packet, 0x4044); ++ordinal) {
            const auto offset = std::ptrdiff_t(0x44) + std::ptrdiff_t(ordinal) * 8;
            if (field<std::uint32_t>(packet, offset + 4) != 0)
                continue;
            const auto index_u = field<std::uint32_t>(packet, offset);
            if (index_u & 0xffff0000u)
                continue;
            try_add_entity(std::bit_cast<std::int32_t>(index_u));
        }
    }
    if (raw.bindings().entity_list) {
        const auto highest =
            std::clamp(engine::invoke<std::int32_t>(raw.bindings().entity_list, 6), 64, 4096);
        for (std::int32_t ent_idx = 65; ent_idx <= highest; ++ent_idx)
            try_add_entity(ent_idx);
    }
    owner.command_buttons = b.command_buttons;
}
} // namespace skeet::esp
