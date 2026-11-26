#include "skeet/esp/grenade_auxiliary.hpp"
#include "skeet/esp/grenade_warning_owner.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/movement/collision.hpp"
#include "detail.hpp"
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace skeet::esp {
GrenadeAuxiliaryLiveOffsets::GrenadeAuxiliaryLiveOffsets(const engine::NativeOffsetGlobals& g)
    : rejection_bytes{{{g, 0x43468E78, 0x434684E4, 0x9D920E35, 0},
                       {g, 0x4346A5DC, 0x43468430, 0xCDE290D2, 0},
                       {g, 0x43468BF8, 0x43467B8C, 0xD070684C, 0},
                       {g, 0x43467DD0, 0x4346A610, 0xAF649DAC, 0}}},
      team{g, 0x434688B0, 0x4346A518, 0x8B5586F6, 0},
      survival_team{g, 0x434678AC, 0x43467B7C, 0x430C8993, 0},
      origin{g, 0x434670CC, 0x43468BCC, 0xA9EE6062, 0},
      duck_amount{g, 0x434679F8, 0x434675F8, 0x3309C0CF, 0},
      smoke_active{g, 0x4346A530, 0x43467564, 0x58AE2423, 0},
      view_angles{g, 0x43468EA0, 0x4346A74C, 0xE23A9BE1, 0} {}
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + offset, sizeof result);
    return result;
}
float negative(float value) {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000U);
}
Vector3 difference(Vector3 a, Vector3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
float squared_yxz(Vector3 a) {
    return (a.y * a.y + a.x * a.x) + a.z * a.z;
}
Vector3 normalized(Vector3 a) {
    const float length = std::sqrt((a.x * a.x + a.y * a.y) + a.z * a.z);
    const float inverse = 1.0f / (length + std::numeric_limits<float>::epsilon());
    return {a.x * inverse, a.y * inverse, a.z * inverse};
}
GrenadeRay line(Vector3 start, Vector3 end) {
    const auto delta = difference(end, start);
    return {start, delta, {}, squared_yxz(delta) != 0.0f, true};
}
std::uint32_t identity(const void* object) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object));
}
Vector3 forward(Vector3 angles, const RageTrigonometry& trig) {
    constexpr float radians = 0.01745329238474369f;
    const float sy = trig.sine(angles.y * radians);
    const float cy = trig.cosine(angles.y * radians);
    const float sp = trig.sine(angles.x * radians);
    const float cp = trig.cosine(angles.x * radians);
    return {cp * cy, cp * sy, negative(sp)};
}
template <class Context>
void flash_basis(Vector3 delta, const Context& context, Vector3& right, Vector3& up) {
    Vector3 angles{};
    if (delta.y == 0.0f && delta.x == 0.0f)
        angles.x = delta.z <= 0.0f ? 90.0f : 270.0f;
    else {
        constexpr float degrees = 57.2957763671875f;
        angles.y = static_cast<float>(context.arc_tangent(delta.y, delta.x)) * degrees;
        const float horizontal = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        angles.x = static_cast<float>(context.arc_tangent(negative(delta.z), horizontal)) * degrees;
    }
    // Actual43366128 is called with no forward output, followed by right/up.
    // Retain roll-zero sin/cos calls and operation order rather than replacing
    // the pair with a cross product derived from the unrounded input delta.
    constexpr float radians = 0.01745329238474369f;
    const auto& trig = context.trigonometry;
    const float sp = trig.sine(angles.x * radians), cp = trig.cosine(angles.x * radians);
    const float sy = trig.sine(angles.y * radians), cy = trig.cosine(angles.y * radians);
    const float sr = trig.sine(angles.z * radians), cr = trig.cosine(angles.z * radians);
    const float cr_negative = cr * -1.0f;
    const float srsp = sr * sp;
    right = normalized(
        {cr_negative * negative(sy) - srsp * cy, cr_negative * cy - srsp * sy, (sr * -1.0f) * cp});
    const float crsp = cr * sp;
    up = normalized({crsp * cy - negative(sy) * sr, crsp * sy - sr * cy, cr * cp});
}
bool visible(const GrenadeDamageTrace& trace, std::uint32_t target) {
    return trace.fraction == 1.0f || trace.entity == target;
}
std::u16string feet_annotation(std::int32_t distance) {
    const float feet = static_cast<float>(distance) / 12.0f;
    char text[32];
    const auto count = std::snprintf(text, sizeof text, "%.1f ft", static_cast<double>(feet));
    return std::u16string(text, text + count);
}
template <class Context> void* entity_list(const Context& context) {
    return context.targeting.entity_list;
}
void* entity_list(const GrenadeAuxiliaryLiveContext& context) {
    return context.entity_list;
}
template <class Context> void* local_player(const Context& context) {
    return context.targeting.local_player;
}
void* local_player(const GrenadeAuxiliaryLiveContext& context) {
    return context.local_player;
}
template <class Context> const void* game_type(const Context& context) {
    return context.targeting.game_type;
}
const void* game_type(const GrenadeAuxiliaryLiveContext& context) {
    return context.game_type;
}
template <class Context> const void* teammates_are_enemies(const Context& context) {
    return context.targeting.teammates_are_enemies;
}
const void* teammates_are_enemies(const GrenadeAuxiliaryLiveContext& context) {
    return context.teammates_are_enemies;
}
template <class Context> const rage::TargetOffsets& target_offsets(const Context& context) {
    return context.targeting.offsets;
}
const GrenadeAuxiliaryLiveOffsets& target_offsets(const GrenadeAuxiliaryLiveContext& context) {
    return context.offsets;
}
template <class Context> const players::State& player_adjustments(const Context& context) {
    return context.targeting.player_adjustments;
}
const players::State& player_adjustments(const GrenadeAuxiliaryLiveContext& context) {
    return context.player_adjustments;
}
template <class Context> std::ptrdiff_t smoke_offset(const Context& context) {
    return context.smoke_active;
}
std::ptrdiff_t smoke_offset(const GrenadeAuxiliaryLiveContext& context) {
    return context.offsets.smoke_active;
}
template <class Context> std::ptrdiff_t view_angles_offset(const Context& context) {
    return context.view_angles;
}
std::ptrdiff_t view_angles_offset(const GrenadeAuxiliaryLiveContext& context) {
    return context.offsets.view_angles;
}
std::int32_t convar_integer(const void* object) {
    for (;;) {
        const auto* parent = field<const void*>(object, 0x1C);
        if (parent == object)
            break;
        object = parent;
    }
    return std::bit_cast<std::int32_t>(
        field<std::uint32_t>(object, 0x30) ^
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object)));
}
template <class Context> bool enemy(const Context& context, const void* target) {
    // Complete4339EF8C. The current game type selects which live decoder is
    // touched; no eagerly populated TargetOffsets snapshot is constructed.
    const auto* local = local_player(context);
    if (!local || !target || local == target)
        return false;
    const auto& offsets = target_offsets(context);
    if (convar_integer(game_type(context)) == 6) {
        const std::ptrdiff_t offset = offsets.survival_team;
        const auto team = field<std::int32_t>(local, offset);
        return team == -1 || team != field<std::int32_t>(target, offset);
    }
    const std::ptrdiff_t offset = offsets.team;
    const auto team = field<std::int32_t>(local, offset);
    const auto other = field<std::int32_t>(target, offset);
    if (team == 2) {
        if (other == 3)
            return true;
    } else if (team == 3) {
        if (other == 2)
            return true;
    } else
        return false;
    return convar_integer(teammates_are_enemies(context)) != 0;
}
template <class Context> bool eligible(const Context& context, void* target) {
    // Complete43358962. In the live scene, none of the preceding genuine
    // entity/networkable virtuals can freeze the later43475808 local-player
    // read in4339EF8C. The offsets and PLAYERS storage remain canonical.
    if (!target || !engine::invoke<bool>(target, 158))
        return false;
    const auto& adjustments = player_adjustments(context);
    const auto& offsets = target_offsets(context);
    if (field<std::uint8_t>(target, offsets.rejection_bytes[0]))
        return false;
    if (engine::invoke<std::int32_t>(target, 122) <= 0)
        return false;
    auto* networkable = static_cast<std::byte*>(target) + 8;
    if (engine::invoke<bool>(networkable, 9))
        return false;
    if (field<std::uint8_t>(target, offsets.rejection_bytes[1]) ||
        field<std::uint8_t>(target, offsets.rejection_bytes[2]))
        return false;
    const auto index = engine::invoke<std::uint32_t>(networkable, 10);
    if (adjustments.records[index].adjustments.whitelist ||
        field<std::uint8_t>(target, offsets.rejection_bytes[3]))
        return false;
    return enemy(context, target);
}
} // namespace

template <class Context>
static GrenadeFlashResult flash(Vector3 position, std::uint32_t ignored_entity,
                                const Context& context) {
    GrenadeFlashResult result;
    Vector3 nearest_origin{};
    const auto& offsets = target_offsets(context);
    const auto maximum_players = context.maximum_players;
    for (std::int32_t index = 1; index <= maximum_players; ++index) {
        auto* entity = engine::invoke<void*>(entity_list(context), 3, index);
        if (!eligible(context, entity))
            continue;
        auto origin = *engine::invoke<const Vector3*>(entity, 10);
        // 43367C8B returns the live virtual origin with the native duck
        // smoothstep eye height added; its C export omits that SSE tail.
        origin.z = grenade_player_view_height(field<float>(entity, offsets.duck_amount)) + origin.z;
        const float distance = squared_yxz(difference(origin, position));
        if (distance < result.distance_squared) {
            result.target = entity;
            result.distance_squared = distance;
            nearest_origin = origin;
        }
    }
    if (!result.target)
        return result;
    const auto origin = *engine::invoke<const Vector3*>(result.target, 10);
    const auto maximum = *engine::invoke<const Vector3*>(result.target, 77);
    const Vector3 eye{origin.x, origin.y, (maximum.z - 8.0f) + origin.z};
    Vector3 right, up;
    flash_basis(difference(eye, position), context, right, up);
    const auto target = identity(result.target);
    const GrenadeDamageFilter filter{GrenadeDamageFilterKind::ignore_entity, ignored_entity};
    {
        MovementTraceFlagGuard guard(context.trace_flag);
        auto trace = context.engine.trace(line(position, eye), 0x6004001U, filter);
        if (visible(trace, target))
            result.exposure = 1.0f;
        else {
            const Vector3 ends[]{
                {up.x * 50.0f + position.x, up.y * 50.0f + position.y, up.z * 50.0f + position.z},
                {(right.x * 75.0f + position.x) + up.x * 10.0f,
                 (right.y * 75.0f + position.y) + up.y * 10.0f,
                 (right.z * 75.0f + position.z) + up.z * 10.0f},
                {(position.x - right.x * 75.0f) + up.x * 10.0f,
                 (position.y - right.y * 75.0f) + up.y * 10.0f,
                 (position.z - right.z * 75.0f) + up.z * 10.0f}};
            for (const auto end : ends) {
                trace = context.engine.trace(line(position, end), 0x6004001U, filter);
                trace = context.engine.trace(line(trace.end, eye), 0x6004001U, filter);
                if (visible(trace, target))
                    result.exposure += 0.16699999570846558f;
            }
        }
    }
    if (!(result.exposure > 0.0f))
        return result;
    float duration =
        3.549999952316284f - std::sqrt(result.distance_squared) * 0.0011833333410322666f;
    if (!(duration > 0.0f))
        return result;
    const auto view =
        forward(field<Vector3>(result.target, view_angles_offset(context)), context.trigonometry);
    const auto toward_flash = normalized(difference(position, nearest_origin));
    const float facing =
        (toward_flash.y * view.y + toward_flash.x * view.x) + toward_flash.z * view.z;
    // COMISD threshold,facing followed by JBE also takes the first arm for
    // unordered engine angles.
    if (!(static_cast<double>(facing) < 0.6))
        duration *= 2.5f;
    else if (!(static_cast<double>(facing) < 0.3))
        duration *= 1.75f;
    else if (facing < -0.20000000298023224f)
        duration *= 0.5f;
    float fraction = ((duration * result.exposure) * 0.6000000238418579f) / 5.099999904632568f;
    fraction = detail::sse_min(detail::sse_max(fraction, 0.0f), 1.0f);
    result.percent = detail::truncate(fraction * 100.0f);
    return result;
}

template <class Context>
static void fire_distance(const GrenadeState& state, const Context& context, std::int32_t& distance,
                          bool& highlight) {
    float best = 30625.0f;
    float height = std::numeric_limits<float>::max();
    const auto& offsets = target_offsets(context);
    const auto maximum_players = context.maximum_players;
    for (std::int32_t index = 1; index <= maximum_players; ++index) {
        auto* entity = engine::invoke<void*>(entity_list(context), 3, index);
        if (!eligible(context, entity))
            continue;
        const auto delta = difference(state.position, field<Vector3>(entity, offsets.origin));
        const float squared = squared_yxz(delta);
        if (squared < best) {
            best = squared;
            height = delta.z;
        }
    }
    if (best == 30625.0f)
        return;
    if (context.smoke_projectile_class) {
        const auto last = field<std::int32_t>(entity_list(context), 0x24);
        for (std::int32_t index = maximum_players + 1; index <= last; ++index) {
            if (static_cast<std::uint32_t>(index) & 0xffff0000U)
                continue;
            auto* entity = engine::invoke<void*>(entity_list(context), 3, index);
            if (!entity)
                continue;
            auto* networkable = static_cast<std::byte*>(entity) + 8;
            const auto* type = engine::invoke<const void*>(networkable, 2);
            if (type != context.smoke_projectile_class ||
                !field<std::uint8_t>(entity, smoke_offset(context)))
                continue;
            if (grenade_warning_smoke_overlap(state.position,
                                              field<Vector3>(entity, offsets.origin)))
                return;
        }
    }
    distance = detail::truncate(std::sqrt(best));
    if (state.fire_ground && distance < 150 && height >= -20.0f && height < 128.0f)
        highlight = true;
}

void evaluate_grenade_fire_distance(const GrenadeState& state,
                                    const GrenadeAuxiliaryContext& context, std::int32_t& distance,
                                    bool& highlight) {
    fire_distance(state, context, distance, highlight);
}
void evaluate_grenade_fire_distance(const GrenadeState& state,
                                    const GrenadeFireDistanceContext& context,
                                    std::int32_t& distance, bool& highlight) {
    fire_distance(state, context, distance, highlight);
}
void evaluate_grenade_fire_distance(const GrenadeState& state,
                                    const GrenadeAuxiliaryLiveContext& context,
                                    std::int32_t& distance, bool& highlight) {
    fire_distance(state, context, distance, highlight);
}
GrenadeFlashResult evaluate_grenade_flash(Vector3 position, std::uint32_t ignored,
                                          const GrenadeAuxiliaryContext& context) {
    return flash(position, ignored, context);
}
GrenadeFlashResult evaluate_grenade_flash(Vector3 position, std::uint32_t ignored,
                                          const GrenadeAuxiliaryLiveContext& context) {
    return flash(position, ignored, context);
}

template <class Context>
static GrenadeAuxiliaryResult auxiliary(const GrenadeState& state, const Context& context) {
    GrenadeAuxiliaryResult result;
    if (state.kind == GrenadeKind::flash) {
        result.magnitude =
            evaluate_grenade_flash(state.position, state.owner_entity, context).percent;
        if (result.magnitude > 0)
            result.annotation = detail::decimal(result.magnitude) + u"%";
    } else if (state.kind == GrenadeKind::molotov || state.kind == GrenadeKind::incendiary) {
        result.draw_path = state.fire_ground;
        if (!result.draw_path)
            return result;
        evaluate_grenade_fire_distance(state, context, result.magnitude, result.highlight);
        if (result.magnitude > 0)
            result.annotation = feet_annotation(result.magnitude);
    } else if (state.kind == GrenadeKind::smoke && state.extinguished)
        result.magnitude = 100;
    return result;
}
GrenadeAuxiliaryResult evaluate_grenade_auxiliary(const GrenadeState& state,
                                                  const GrenadeAuxiliaryContext& context) {
    return auxiliary(state, context);
}
GrenadeAuxiliaryResult evaluate_grenade_auxiliary(const GrenadeState& state,
                                                  const GrenadeAuxiliaryLiveContext& context) {
    return auxiliary(state, context);
}
} // namespace skeet::esp
