#include "skeet/esp/entity_models.hpp"
#include "skeet/esp/frame.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/events/game_events.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>

#pragma STDC FENV_ACCESS ON

namespace skeet::esp {
namespace {
// Original256-word rehash table43473248..43473648.
constexpr std::array<std::uint32_t, 256> primes{
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
void* model_table(void* const& tables) { //433AE8B7, wanted9FE6B136
    const auto count = engine::invoke<std::int32_t>(tables, 5);
    for (std::int32_t index = 0; index < count; ++index) {
        auto* table = engine::invoke<void*>(tables, 4, index);
        if (!table)
            continue;
        const auto* name = engine::invoke<const char*>(table, 1);
        if (name && events::event_hash(name) == 0x9FE6B136u)
            return table;
    }
    return nullptr;
}
}
DangerZoneModels::DangerZoneModels(engine::JobMemory memory) : memory_(memory) {
    static_assert(sizeof(void*) != 4 || sizeof(Node) == 12);
    if (!memory_.allocate || !memory_.free || !memory_.process_heap)
        throw std::invalid_argument("Entity model map requires native process heap exports");
}
DangerZoneModels::~DangerZoneModels() {
    release_storage();
}
void DangerZoneModels::release_storage() {
    clear();
    if (bucket_count_ > 1)
        free(buckets_);
    // The native owner is dead after this point. Keep the borrowed semantic
    // wrapper empty so its later C++ lifetime end does not free dead storage.
    initial_ = nullptr;
    buckets_ = &initial_;
    bucket_count_ = 1;
    next_resize_ = 0;
}
void* DangerZoneModels::allocate(std::size_t size) { //43387448
    const auto allocation_size = size + 0x20;
    auto* raw = memory_.allocate(memory_.process_heap, 8, allocation_size);
    if (!raw)
        return nullptr;
    const auto base = reinterpret_cast<std::uintptr_t>(raw);
    const auto header = (base + 15) & ~std::uintptr_t(15);
    const auto displacement = std::uint32_t(header - base);
    const auto stored_size = std::uint32_t(allocation_size);
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &stored_size, 4);
    return reinterpret_cast<void*>(header + 16);
}
void DangerZoneModels::free(void* object) { //433837D0
    if (!object)
        return;
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, 4);
    memory_.free(memory_.process_heap, 0, bytes - 16 - displacement);
}
void DangerZoneModels::clear() {
    const auto count = bucket_count_;
    auto** buckets = buckets_;
    for (std::uint32_t index = 0; index < count; ++index) {
        auto* node = buckets[index];
        while (node) {
            auto* next = node->next;
            free(node);
            node = next;
        }
        buckets[index] = nullptr;
    }
    size_ = 0;
}
void DangerZoneModels::rehash(std::uint32_t count) {
    auto** next = static_cast<Node**>(allocate((std::size_t(count) + 1) * sizeof(Node*)));
    std::memset(next, 0, std::size_t(count) * sizeof(Node*));
    next[count] = reinterpret_cast<Node*>(~std::uintptr_t{});
    std::uint32_t old_bucket = 0;
    for (; old_bucket < bucket_count_; ++old_bucket) {
        while (auto* node = buckets_[old_bucket]) {
            const auto bucket = std::uint32_t(node->entry.model_index) % count;
            buckets_[old_bucket] = node->next;
            node->next = next[bucket];
            next[bucket] = node;
        }
    }
    if (bucket_count_ > 1)
        free(buckets_);
    bucket_count_ = count;
    buckets_ = next;
}
bool DangerZoneModels::insert(std::uint16_t model, DangerZoneItem& item) {
    auto* node = ::new (allocate(sizeof(Node))) Node{{model, &item}, nullptr};
    auto bucket = std::uint32_t(model) % bucket_count_;
    for (auto* previous = buckets_[bucket]; previous; previous = previous->next) {
        if (previous->entry.model_index == model) {
            free(node);
            return false;
        }
    }
    const auto wanted = size_ + 1;
    if (wanted > next_resize_) {
        const auto previous = bucket_count_ == 1 ? 0u : bucket_count_;
        if (wanted > previous) {
            // Constructor policy is exactly load1/growth2. Model keys are
            // uint16, so these integer products are exact native float values.
            const auto minimum = std::max(previous * 2, wanted);
            const auto count = *std::lower_bound(primes.begin(), primes.end(), minimum);
            next_resize_ = count;
            bucket = std::uint32_t(model) % count;
            rehash(count);
        } else
            next_resize_ = previous;
    }
    node->next = buckets_[bucket];
    buckets_[bucket] = node;
    ++size_;
    return true;
}
DangerZoneItem* DangerZoneModels::find(std::uint16_t model) const noexcept {
    for (auto* node = buckets_[std::uint32_t(model) % bucket_count_]; node; node = node->next)
        if (node->entry.model_index == model)
            return node->entry.item;
    return nullptr;
}
DangerZoneModels::Iterator DangerZoneModels::begin() const noexcept {
    std::uint32_t bucket = 0;
    while (bucket < bucket_count_ && !buckets_[bucket])
        ++bucket;
    return {this, bucket, bucket == bucket_count_ ? nullptr : buckets_[bucket]};
}
DangerZoneModels::Iterator& DangerZoneModels::Iterator::operator++() noexcept {
    node_ = node_->next;
    if (!node_) {
        do {
            ++bucket_;
        } while (bucket_ < map_->bucket_count_ && !map_->buckets_[bucket_]);
        if (bucket_ < map_->bucket_count_)
            node_ = map_->buckets_[bucket_];
    }
    return *this;
}
EntityModelState::EntityModelState(engine::JobMemory memory) : models(memory) {
    const auto templates = danger_zone_item_templates();
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        descriptors[index].label = templates[index].label;
        descriptors[index].distance = 0.0f; //constructor+descriptor1C
    }
}
void EntityModelState::bind_config(const WorldEspConfig& config) noexcept {
    const auto templates = danger_zone_item_templates();
    for (std::size_t index = 0; index < descriptors.size(); ++index)
        descriptors[index].setting = &(config.*templates[index].setting);
}
void reset_danger_zone_models(EntityModelState& state) noexcept {
    for (const auto& entry : state.models)
        entry.item->distance = std::numeric_limits<float>::max();
}
void collect_danger_zone_model(EntityModelState& state, const WorldEntity& entity,
                               Vector3 local) noexcept {
    if (entity.dormant || entity.model_index < 0)
        return;
    auto* item = state.models.find(std::uint16_t(entity.model_index));
    if (!item || !item->setting->enabled.value)
        return;
    const auto x = local.x - entity.origin.x, y = local.y - entity.origin.y,
               z = local.z - entity.origin.z;
    const auto distance = std::sqrt((y * y + x * x) + z * z);
    if (distance >= item->distance)
        return;
    item->distance = distance;
    item->closest_origin = entity.origin;
}
void draw_danger_zone_models(ui::RenderQueue& queue, const Projection& projection,
                             std::int32_t mode, bool enabled, const EntityModelState& state) {
    if (mode != 6 || !enabled)
        return;
    for (const auto& entry : state.models)
        draw_danger_zone_items(queue, projection, 6, true, {entry.item, 1});
}
void reset_esp_frame_owner(EspFrameOwner& owner, effects::GlowRuntime& glow) {
    owner.frame.timer = 0;
    owner.state_6b4 = 0;
    owner.sounds.ring_model = -1;
    glow.bomb_handle = -1;
    owner.tablet.frame = -1;
    owner.bomb_peak_damage = 500.0f;
    owner.frame.transient = 0;
    glow.weapons_active = false;
    owner.pickups.count = 0;
    owner.entity_models.models.clear();
    owner.notice_entity = -1;
}
void initialize_esp_entity_models(EspFrameOwner& owner, effects::GlowRuntime& glow,
                                  const WorldEspConfig& config, void* const& tables) {
    reset_esp_frame_owner(owner, glow);
    owner.entity_models.bind_config(config);
    auto* models = model_table(tables);
    if (!models)
        return;
    const auto count = engine::invoke<std::int32_t>(models, 3);
    const auto templates = danger_zone_item_templates();
    for (std::int32_t index = 0; index < count; ++index) {
        const auto* name = engine::invoke<const char*>(models, 9, index);
        if (!name)
            continue;
        const auto length = std::char_traits<char>::length(name);
        if (length <= 38) {
            if (length == 28 && events::event_hash(name) == 0x58A15B00u)
                owner.defuse_kit_model = index;
            continue;
        }
        if (name[7] != 'p' || name[12] != '_' || name[16] != 'v' || name[21] != '/')
            continue;
        const auto hash = events::event_hash(name);
        for (std::size_t item = 0; item < templates.size(); ++item) {
            if (hash != templates[item].model_hash)
                continue;
            owner.entity_models.models.insert(std::uint16_t(index),
                                              owner.entity_models.descriptors[item]);
            break;
        }
    }
    owner.sounds.initialize_model_table(models);
}
} // namespace skeet::esp
