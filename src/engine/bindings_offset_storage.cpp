#include "skeet/engine/offset_storage.hpp"
#include "skeet/engine/netvar_initialization.hpp"
#include <cstring>
#include <new>

namespace skeet::engine {
namespace {
struct Recipe {
    std::uint32_t source_pointer, source_key, xor_value;
    std::int32_t displacement;
    std::uint32_t destination_key, destination_pointer;
};
// Exact native4348D65D..4348D9E6 operands, in execution order.
constexpr std::array recipes{
    Recipe{0x43469198, 0x4346a538, 0xd5fd2e2c, 4, 0x4346a6b4, 0x43467004},
    Recipe{0x434670e0, 0x43467d28, 0x4e66bb34, 12, 0x43468ed4, 0x43468d30},
    Recipe{0x434688d4, 0x43469030, 0xf8205eba, 12, 0x43467fac, 0x43468368},
    Recipe{0x4346a53c, 0x43468eb4, 0x13965961, 4, 0x4346920c, 0x4346a588},
    Recipe{0x43467e8c, 0x43467900, 0x32e4daf1, 4, 0x43468524, 0x4346a7d4},
    Recipe{0x4346a808, 0x43468be8, 0x2b554ad0, 20, 0x434674a8, 0x43468e7c},
    Recipe{0x434675cc, 0x43468538, 0xc7e1ecd2, -20, 0x4346a484, 0x43469064},
    Recipe{0x434682c0, 0x434683cc, 0x1952b857, 44, 0x43467b80, 0x43467d78},
    Recipe{0x4346934c, 0x434671f8, 0x334b460c, 1, 0x4346707c, 0x4346757c},
    Recipe{0x434693d8, 0x43468bc4, 0x3d10b94c, 4, 0x4346a5b8, 0x434675c8},
    Recipe{0x43468a8c, 0x43467d88, 0x7fd04456, 4, 0x43467c70, 0x43468fd0},
    Recipe{0x4346934c, 0x434671f8, 0x334b460c, 76, 0x43467b8c, 0x43468bf8},
    Recipe{0x434688a0, 0x43467b78, 0x5461ee58, -8, 0x43467fa8, 0x43467434},
    Recipe{0x434688a0, 0x43467b78, 0x5461ee58, -4, 0x43468c8c, 0x43468ebc},
    Recipe{0x434688a0, 0x43467b78, 0x5461ee58, 28, 0x43467b94, 0x434670f4},
    Recipe{0x43468da4, 0x434684ec, 0xcacaeecc, -20, 0x4346a8b0, 0x43467f4c},
    Recipe{0x4346796c, 0x43469084, 0x572aa59f, -28, 0x43467d24, 0x4346a62c},
    Recipe{0x43469230, 0x434679a8, 0xa1af8ffc, 20, 0x43467500, 0x4346a354},
    Recipe{0x4346a530, 0x43467564, 0x58ae2423, 8, 0x4346a3a8, 0x43468970},
    Recipe{0x4346a6a4, 0x43467120, 0x9d63bb7f, -4, 0x4346a540, 0x43469210}};
std::uint32_t* allocate_words(JobMemory memory) {
    auto* allocation = memory.allocate(memory.process_heap, 8, 0x1020);
    if (!allocation)
        return nullptr;
    const auto raw = reinterpret_cast<std::uintptr_t>(allocation);
    const auto header = (raw + 15) & ~std::uintptr_t(15);
    const auto displacement = static_cast<std::uint32_t>(header - raw);
    constexpr std::uint32_t size = 0x1020;
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &size, 4);
    return reinterpret_cast<std::uint32_t*>(header + 16);
}
void free_words(std::uint32_t* words, JobMemory memory) {
    if (!words)
        return;
    auto* object = reinterpret_cast<std::byte*>(words);
    std::uint32_t displacement;
    std::memcpy(&displacement, object - 8, 4);
    memory.free(memory.process_heap, 0, object - 16 - displacement);
}
void publish(NativeOffsetStorage& storage, std::uint32_t key_id, std::uint32_t pointer_id,
             std::uint32_t offset) {
    auto& key = storage.globals.keys.at(key_id);
    key ^= offset;
    auto& output = storage.globals.pointers[pointer_id];
    publish_native_offset(key, output, storage.cursor);
}
RecvVarProxy* proxy_slot(RecvProp* property) {
    if (!property)
        return reinterpret_cast<RecvVarProxy*>(std::uintptr_t(0x20));
    return &property->proxy;
}
}

void seed_native_offset_words(std::span<std::uint32_t, 0x400> words,
                              std::uint32_t timestamp_low) noexcept {
    for (std::uint32_t index = 1; index != 0x400; ++index) {
        timestamp_low = timestamp_low * 0x343fd + 0x269ec3;
        words[index] = timestamp_low >> 16;
    }
}
void publish_native_offset(std::uint32_t& key, std::uint32_t*& pointer,
                           std::uint32_t*& cursor) noexcept {
    pointer = cursor;
    key ^= *cursor;
    ++cursor;
}

namespace {
template <class InitializeNetvars>
void initialize_offset_storage(NativeOffsetStorage& storage, JobMemory memory, void* base_client,
                               std::uint32_t timestamp_low, InitializeNetvars initialize_netvars) {
    auto* replacement = allocate_words(memory);
    if (replacement != storage.words) {
        free_words(storage.words, memory);
        storage.words = replacement;
    }
    if (!storage.words)
        throw std::bad_alloc();
    storage.words[0] = 0;
    seed_native_offset_words(std::span<std::uint32_t, 0x400>(storage.words, 0x400), timestamp_low);
    storage.globals.pointers[0x43468a90] = storage.words;
    storage.globals.pointers[0x43467a88] = storage.words;
    storage.globals.pointers[0x43467ebc] = storage.words;
    storage.words[0] = 0;
    storage.cursor = storage.words + 1;
    storage.property_proxy_slots[0] =
        proxy_slot(find_receive_property(base_client, 0x5f5bab75, 0x2de6daf5, 0x95daf62c));
    storage.property_proxy_slots[1] =
        proxy_slot(find_receive_property(base_client, 0x5f5bab75, 0x2de6daf5, 0x3006e9eb));
    (void)find_receive_property(base_client, 0x5f5bab75, 0x2de6daf5, 0xa5f4b9b7);
    storage.property_proxy_slots[2] =
        proxy_slot(find_receive_property(base_client, 0x0f0ede49, 0x8c24da3d, 0xff2eac3b));
    storage.property_proxy_slots[3] =
        proxy_slot(find_receive_property(base_client, 0x0f0ede49, 0x8c24da3d, 0xd5c777c9));
    initialize_netvars();
    const auto base =
        *storage.globals.pointers.at(0x4346a5f8) ^ storage.globals.keys.at(0x43467b88) ^ 0x950f7b20;
    constexpr std::array<std::uint32_t, 5> keys{0x43468dd8, 0x43468f2c, 0x4346a394, 0x43468e1c,
                                                0x43467578};
    constexpr std::array<std::uint32_t, 5> pointers{0x43467cd8, 0x43467634, 0x43468ed0, 0x43467ba4,
                                                    0x4346a51c};
    for (std::uint32_t index = 0; index != 5; ++index)
        publish(storage, keys[index], pointers[index], base + 4 * (index + 1));
    publish(storage, 0x43468eb4, 0x4346a53c,
            find_data_map_offset(*storage.maps.c69c5c29, 0x1e4c8218, 0xb1fb5791) - 12);
    publish(storage, 0x43467558, 0x43468d68,
            find_data_map_offset(*storage.maps.a9cd7264, 0xa9cd7264, 0x3c3e400b));
    publish(storage, 0x4346a718, 0x43467430,
            find_data_map_offset(*storage.maps.a9cd7264, 0x43fefb46, 0x8a8558a2));
    publish(storage, 0x43468d2c, 0x4346828c,
            find_data_map_offset(*storage.maps.a9cd7264, 0x43fefb46, 0x99c8229d));
    for (const auto& recipe : recipes) {
        const auto source = *storage.globals.pointers.at(recipe.source_pointer) ^
                            storage.globals.keys.at(recipe.source_key) ^ recipe.xor_value;
        publish(storage, recipe.destination_key, recipe.destination_pointer,
                source + static_cast<std::uint32_t>(recipe.displacement));
    }
}
}
void initialize_native_offset_storage(NativeOffsetStorage& storage, JobMemory memory,
                                      void* base_client, std::byte* client_module,
                                      InitializationMetadata& metadata,
                                      std::uint32_t timestamp_low) {
    initialize_offset_storage(storage, memory, base_client, timestamp_low, [&] {
        initialize_native_netvars(base_client, client_module, metadata, storage.cursor,
                                  storage.maps);
    });
}
void initialize_build_offset_storage(NativeOffsetStorage& storage, JobMemory memory,
                                     void* base_client, std::byte* client_module,
                                     std::uint32_t timestamp_low) {
    struct StaticKey {
        std::uint32_t identity, value;
    };
    // These views share word0. Entity-animation engine binding resolves their
    // collision-height/time keys before any feature consumes the offsets.
    storage.globals.keys.try_emplace(0x43467160u, 0x0D560FACu);
    storage.globals.keys.try_emplace(0x43467038u, 0x432B1F5Bu);
    // Initial static key words from the pinned input build. These29 keys
    // belong to the ordinary post-netvar publications, not to saved offsets.
    constexpr std::array<StaticKey, 29> derived_keys{
        {{0x43468DD8u, 0x01D0BACCu}, {0x43468F2Cu, 0xBB78DC43u}, {0x4346A394u, 0xABD16CE2u},
         {0x43468E1Cu, 0x74019D87u}, {0x43467578u, 0x852D149Au}, {0x43468EB4u, 0x13965961u},
         {0x43467558u, 0x836CB5DBu}, {0x4346A718u, 0xB8969256u}, {0x43468D2Cu, 0x9E2078A1u},
         {0x4346A6B4u, 0x1599541Fu}, {0x43468ED4u, 0x2A671B42u}, {0x43467FACu, 0x3B1F6ECCu},
         {0x4346920Cu, 0x1B04305Au}, {0x43468524u, 0x465B42F8u}, {0x434674A8u, 0x54783141u},
         {0x4346A484u, 0x17978090u}, {0x43467B80u, 0x1CAAF0A8u}, {0x4346707Cu, 0x043FA575u},
         {0x4346A5B8u, 0x3EDECE61u}, {0x43467C70u, 0xF58E6118u}, {0x43467B8Cu, 0xD070684Cu},
         {0x43467FA8u, 0x1BC6D160u}, {0x43468C8Cu, 0x8DE575A5u}, {0x43467B94u, 0x442A38AEu},
         {0x4346A8B0u, 0x3D52A1A7u}, {0x43467D24u, 0x442A38AEu}, {0x43467500u, 0x3353C75Fu},
         {0x4346A3A8u, 0x666EC5F2u}, {0x4346A540u, 0x442A38AEu}}};
    for (const auto& key : derived_keys)
        storage.globals.keys.try_emplace(key.identity, key.value);
    initialize_offset_storage(storage, memory, base_client, timestamp_low, [&] {
        initialize_build_netvars(base_client, client_module, storage);
    });
}
} // namespace skeet::engine
