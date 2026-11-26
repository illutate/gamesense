#include "skeet/animation/fix_animation_backup.hpp"
#include "skeet/animation/state_copy.hpp"
#include "skeet/engine/hash_growth.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <new>
#include <xmmintrin.h>

namespace skeet::animation {
namespace {
FixAnimationBackupRuntime* active_backup;
template <class T> T read(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void write(void* object, std::ptrdiff_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
// The immutable native256-word lower-bound table43473248..43473648. This is
// shared native hash policy data, not another animation-backup owner.
constexpr std::array<std::uint32_t, 256> bucket_primes{
    2u,          3u,          5u,          7u,          11u,         13u,         17u,
    19u,         23u,         29u,         31u,         37u,         41u,         43u,
    47u,         53u,         59u,         61u,         67u,         71u,         73u,
    79u,         83u,         89u,         97u,         103u,        109u,        113u,
    127u,        137u,        139u,        149u,        157u,        167u,        179u,
    193u,        199u,        211u,        227u,        241u,        257u,        277u,
    293u,        313u,        337u,        359u,        383u,        409u,        439u,
    467u,        503u,        541u,        577u,        619u,        661u,        709u,
    761u,        823u,        887u,        953u,        1031u,       1109u,       1193u,
    1289u,       1381u,       1493u,       1613u,       1741u,       1879u,       2029u,
    2179u,       2357u,       2549u,       2753u,       2971u,       3209u,       3469u,
    3739u,       4027u,       4349u,       4703u,       5087u,       5503u,       5953u,
    6427u,       6949u,       7517u,       8123u,       8783u,       9497u,       10273u,
    11113u,      12011u,      12983u,      14033u,      15173u,      16411u,      17749u,
    19183u,      20753u,      22447u,      24281u,      26267u,      28411u,      30727u,
    33223u,      35933u,      38873u,      42043u,      45481u,      49201u,      53201u,
    57557u,      62233u,      67307u,      72817u,      78779u,      85229u,      92203u,
    99733u,      107897u,     116731u,     126271u,     136607u,     147793u,     159871u,
    172933u,     187091u,     202409u,     218971u,     236897u,     256279u,     277261u,
    299951u,     324503u,     351061u,     379787u,     410857u,     444487u,     480881u,
    520241u,     562841u,     608903u,     658753u,     712697u,     771049u,     834181u,
    902483u,     976369u,     1056323u,    1142821u,    1236397u,    1337629u,    1447153u,
    1565659u,    1693859u,    1832561u,    1982627u,    2144977u,    2320627u,    2510653u,
    2716249u,    2938679u,    3179303u,    3439651u,    3721303u,    4026031u,    4355707u,
    4712381u,    5098259u,    5515729u,    5967347u,    6456007u,    6984629u,    7556579u,
    8175383u,    8844859u,    9569143u,    10352717u,   11200489u,   12117689u,   13109983u,
    14183539u,   15345007u,   16601593u,   17961079u,   19431899u,   21023161u,   22744717u,
    24607243u,   26622317u,   28802401u,   31160981u,   33712729u,   36473443u,   39460231u,
    42691603u,   46187573u,   49969847u,   54061849u,   58488943u,   63278561u,   68460391u,
    74066549u,   80131819u,   86693767u,   93793069u,   101473717u,  109783337u,  118773397u,
    128499677u,  139022417u,  150406843u,  162723577u,  176048909u,  190465427u,  206062531u,
    222936881u,  241193053u,  260944219u,  282312799u,  305431229u,  330442829u,  357502601u,
    386778277u,  418451333u,  452718089u,  489790921u,  529899637u,  573292817u,  620239453u,
    671030513u,  725980837u,  785430967u,  849749479u,  919334987u,  994618837u,  1076067617u,
    1164186217u, 1259520799u, 1362662261u, 1474249943u, 1594975441u, 1725587117u, 1866894511u,
    2019773507u, 2185171673u, 2364114217u, 2557710269u, 2767159799u, 2993761039u, 3238918481u,
    3504151727u, 3791104843u, 4101556399u, 4294967291u,
};
}

void* FixAnimationBackupRuntime::allocate(std::size_t size) {
    if (!size)
        return nullptr;
    auto* raw = bindings.memory.allocate(bindings.memory.process_heap, 8, size + 0x20);
    if (!raw)
        return nullptr;
    const auto address = reinterpret_cast<std::uintptr_t>(raw);
    const auto header = (address + 15) & ~std::uintptr_t(15);
    const auto displacement = std::uint32_t(header - address);
    const auto allocation_size = std::uint32_t(size + 0x20);
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &allocation_size, 4);
    return reinterpret_cast<void*>(header + 16);
}
void FixAnimationBackupRuntime::free(void* allocation) {
    if (!allocation)
        return;
    auto* bytes = static_cast<std::byte*>(allocation);
    const auto displacement = read<std::uint32_t>(bytes, -8);
    bindings.memory.free(bindings.memory.process_heap, 0, bytes - 16 - displacement);
}

FixAnimationBackupNode* FixAnimationBackupRuntime::create_node(const std::uint32_t* key) {
    auto* node = ::new (allocate(sizeof(FixAnimationBackupNode))) FixAnimationBackupNode;
    // The key is intentionally read after the actual heap call, as43329CDF.
    node->entity_index = *key;
    std::memset(reinterpret_cast<std::byte*>(node) + 4, 0, 0x3C0);
    initialize_animation_state_vector(node->animation.data());
    node->next = nullptr;
    return node;
}

FixAnimationBackupRuntime::GrowthDecision
FixAnimationBackupRuntime::plan_growth(std::uint32_t buckets, std::uint32_t size) {
    const auto& b = bindings;
    const auto growth =
        engine::plan_native_hash_growth(buckets, size, b.growth.maximum_load, b.growth.multiplier,
                                        b.growth.next_resize, bucket_primes, b.ceiling);
    return {growth.rehash, growth.bucket_count};
}

void FixAnimationBackupRuntime::rehash(std::uint32_t count) {
    const auto& b = bindings;
    auto** next =
        static_cast<FixAnimationBackupNode**>(allocate((std::size_t(count) + 1) * sizeof(void*)));
    std::memset(next, 0, std::size_t(count) * sizeof(void*));
    next[count] = reinterpret_cast<FixAnimationBackupNode*>(~std::uintptr_t(0));
    // Allocation can reenter the source owner. Capture the current bucket
    // count/storage only after that genuine boundary, as4332C319 does.
    std::uint32_t old_bucket = 0;
    while (old_bucket < b.bucket_count) {
        auto** previous = b.buckets;
        while (auto* node = previous[old_bucket]) {
            const auto bucket = node->entity_index % count;
            previous[old_bucket] = node->next;
            node->next = next[bucket];
            next[bucket] = node;
            previous = b.buckets;
        }
        ++old_bucket;
    }
    if (b.bucket_count > 1)
        free(b.buckets);
    b.bucket_count = count;
    b.buckets = next;
}

FixAnimationBackupInsert FixAnimationBackupRuntime::find_or_insert(const std::uint32_t* key) {
    const auto& b = bindings;
    const auto identity = *key;
    auto bucket = identity % b.bucket_count;
    auto** slot = b.buckets + bucket;
    for (auto* node = *slot; node; node = node->next)
        if (node->entity_index == identity)
            return {node, slot, false};
    const auto decision = plan_growth(b.bucket_count, b.size);
    auto* node = create_node(key);
    if (decision.rehash) {
        bucket = identity % decision.bucket_count;
        rehash(decision.bucket_count);
    }
    node->next = b.buckets[bucket];
    b.buckets[bucket] = node;
    ++b.size;
    return {node, b.buckets + bucket, true};
}

void FixAnimationBackupRuntime::capture(void* player) {
    const auto& b = bindings;
    if (!b.mode || read<std::uint8_t>(player, b.life_state_9d920e35))
        return;
    const auto index = engine::invoke<std::uint32_t>(static_cast<std::byte*>(player) + 8, 10);
    auto* node = find_or_insert(&index).node;
    node->acknowledged_command = engine::invoke<std::int32_t>(b.entity.engine, 205);
    node->frame = b.prediction_frame;
    const auto* state = read<const void*>(player, b.bones.fields.animation_state_3d52a1a7);
    std::memcpy(node->animation.data(), state, 0x348);
    initialize_animation_state_vector(node->animation.data());
    std::memcpy(node->poses.data(), b.bones.pose_parameters(player).data(), sizeof node->poses);
    const std::array<std::uint32_t, 3> angles{0, read<std::uint32_t>(node->animation.data(), 0x80),
                                              0};
    std::memcpy(node->angles.data(), angles.data(), sizeof angles);
    node->collision_time = read<float>(player, b.entity.offsets.field_0d560fac);
}

void FixAnimationBackupRuntime::detach_entity(void* player) {
    // Complete433CB965. Each getter follows restoration/free of the previous
    // interface, so later genuine engine calls see the original vtables.
    for (const auto slot : {4u, 5u, 8u}) {
        auto* object = engine::invoke<void*>(player, slot);
        engine::uninstall_vtable_hook(detach.hooks, object);
    }
    const auto extra = detach.extra_interface_offset;
    if (extra)
        engine::uninstall_vtable_hook(detach.hooks, static_cast<std::byte*>(player) + extra);
    engine::uninstall_vtable_hook(detach.hooks, player);
    const auto index = engine::invoke<std::uint32_t>(static_cast<std::byte*>(player) + 8, 10);
    capture(player);
    write(detach.player_settings, std::size_t(index) * 0x20 + 4, UINT32_MAX);
    const auto selected =
        detach.target.handle == -1 ? 0x1FFFu : std::uint32_t(detach.target.handle) & 0xFFFFu;
    if (index == selected)
        client::reset_target_state(detach.target);
}

void bind_fix_animation_backup(FixAnimationBackupRuntime& runtime) noexcept {
    active_backup = &runtime;
}
#if defined(_M_IX86) || defined(__i386__)
void __fastcall capture_fix_animation_backup(void* player, void*) {
#else
void capture_fix_animation_backup(void* player, void*) {
#endif
    active_backup->capture(player);
}
#if defined(_M_IX86) || defined(__i386__)
void __fastcall detach_fix_animation_entity(void* player, void*) {
#else
void detach_fix_animation_entity(void* player, void*) {
#endif
    active_backup->detach_entity(player);
}
} // namespace skeet::animation
