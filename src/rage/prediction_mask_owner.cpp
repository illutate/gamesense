#include "skeet/rage/prediction_mask_owner.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/owner_allocation.hpp"
#include <cstring>
#include <new>

namespace skeet::rage {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define SKEET_PREDICTION_MEMBER __thiscall
#define SKEET_PREDICTION_FAST __fastcall
#else
#define SKEET_PREDICTION_MEMBER
#define SKEET_PREDICTION_FAST
#endif
using Entry = void(SKEET_PREDICTION_MEMBER*)(void*);
void SKEET_PREDICTION_MEMBER destroy_entry(void*);
void SKEET_PREDICTION_MEMBER reset_entry(void*);
void SKEET_PREDICTION_MEMBER forward_reset(void*);
void SKEET_PREDICTION_MEMBER unchanged(void*) {}
void SKEET_PREDICTION_FAST update_entry(void*, std::uint8_t);
const std::array<Entry, 9> primary{destroy_entry, unchanged, unchanged,     unchanged, unchanged,
                                   reset_entry,   unchanged, forward_reset, unchanged}; //4347031C
const engine::FrameUpdateEntry secondary[]{update_entry}; //43470174
}

class PredictionMaskOwner {
public:
    const Entry* vtable = nullptr;
    std::uintptr_t preserved_second_word;
    engine::FrameUpdateEndpoint endpoint;
    PredictionMaskState masks;
    PredictionMaskFactoryBindings* bindings;

    explicit PredictionMaskOwner(PredictionMaskFactoryBindings& source) : bindings(&source) {
        source.lifecycle.append(this);
        engine::register_frame_update(source.frames, &endpoint);
        vtable = primary.data();
        endpoint.vtable = secondary;
        // Constructor does not reset the masks;43387448 zeroed the allocation.
    }
};
#if UINTPTR_MAX == UINT32_MAX
static_assert(offsetof(PredictionMaskOwner, endpoint) == 8);
static_assert(offsetof(PredictionMaskOwner, masks) == 0xC);
static_assert(offsetof(PredictionMaskOwner, bindings) == 0x20);
#endif
namespace {
void SKEET_PREDICTION_MEMBER destroy_entry(void* raw) {
    auto* owner = static_cast<PredictionMaskOwner*>(raw);
    auto& bindings = *owner->bindings;
    destroy_prediction_mask_owner(owner, bindings.active, bindings.lifecycle.memory);
}
void SKEET_PREDICTION_MEMBER reset_entry(void* raw) {
    reset_prediction_masks(static_cast<PredictionMaskOwner*>(raw)->masks);
}
void SKEET_PREDICTION_MEMBER forward_reset(void* raw) {
    engine::invoke<void>(raw, 5); //433CF2F5 calls the current slot5.
}
void SKEET_PREDICTION_FAST update_entry(void* raw, std::uint8_t update) {
    auto* owner = reinterpret_cast<PredictionMaskOwner*>(static_cast<std::byte*>(raw) -
                                                         offsetof(PredictionMaskOwner, endpoint));
    owner->bindings->runtime.update(owner->masks, update);
}
void initialize_entry(void* raw) {
    create_prediction_mask_owner(*static_cast<PredictionMaskFactoryBindings*>(raw));
}
#undef SKEET_PREDICTION_MEMBER
#undef SKEET_PREDICTION_FAST
}

PredictionMaskOwner* create_prediction_mask_owner(PredictionMaskFactoryBindings& source) {
    const auto memory = source.lifecycle.memory;
    auto* allocation = engine::allocate_owner_storage(memory, sizeof(PredictionMaskOwner));
    if (!allocation) {
        source.active = nullptr;
        return nullptr;
    }
    auto* owner = ::new (allocation) PredictionMaskOwner(source);
    source.active = owner;
    return owner;
}
void destroy_prediction_mask_owner(PredictionMaskOwner* owner, PredictionMaskOwner*& active,
                                   engine::JobMemory memory) {
    active = nullptr; //433BFE13 clears the singleton before433837D0's null test.
    if (!owner)
        return;
    owner->~PredictionMaskOwner();
    engine::release_owner_storage(memory, owner);
}
PredictionMaskState& prediction_mask_state(PredictionMaskOwner& owner) noexcept {
    return owner.masks;
}
engine::FrameUpdateEndpoint& prediction_mask_endpoint(PredictionMaskOwner& owner) noexcept {
    return owner.endpoint;
}
void bind_prediction_mask_factory(client::NativeStartupSources& sources,
                                  PredictionMaskFactoryBindings& bindings) {
    sources.bind_initializer({0x4347EA07u, &bindings, initialize_entry});
}
}
