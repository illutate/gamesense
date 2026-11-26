#include "skeet/engine/game_system.hpp"
#include "game_system_detail.hpp"
#include <new>

namespace skeet::engine {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define SYSTEM_MEMBER __thiscall
#else
#define SYSTEM_MEMBER
#endif
using namespace game_system_detail;
void SYSTEM_MEMBER empty(void*) {} //433CCE8E
void SYSTEM_MEMBER empty1(void*, std::uintptr_t) {} //433C95B7
bool SYSTEM_MEMBER true0(void*) {
    return true;
} //433422C6
void SYSTEM_MEMBER dispatch(void* endpoint, std::uintptr_t first, std::uintptr_t second,
                            std::uintptr_t third, std::uintptr_t fourth) {
    auto& owner = owner_of<GameSystemRegistrationOwner>(
        static_cast<GameSystemRegistrationEndpoint*>(endpoint),
        offsetof(GameSystemRegistrationOwner, endpoint));
    dispatch_registered_client_callbacks(*owner.callbacks, first, second, third, fourth);
}
const GameSystemRegistrationVtable registration_table{empty,    empty,  empty1,
                                                      dispatch, empty1, true0};
void SYSTEM_MEMBER destroy(void* object) {
    auto* owner = static_cast<GameSystemRegistrationOwner*>(object);
    const auto memory = owner->memory;
    owner->~GameSystemRegistrationOwner();
    release(object, memory);
}
void SYSTEM_MEMBER install(void* object) {
    static_cast<GameSystemRegistrationOwner*>(object)->install();
}
void SYSTEM_MEMBER remove(void* object) {
    static_cast<GameSystemRegistrationOwner*>(object)->remove();
}
const std::array<GameSystemEntry<void>, 9> owner_table{destroy, empty, install, empty, remove,
                                                       empty,   empty, empty,   empty};
}
void dispatch_registered_client_callbacks(const OwnerRegistry& callbacks, std::uintptr_t first,
                                          std::uintptr_t second, std::uintptr_t third,
                                          std::uintptr_t fourth) {
    const auto entries = callbacks.entries(); //native captures both begin and end
    for (auto* object : entries) {
        auto* endpoint = static_cast<RegisteredClientEndpoint*>(object);
        endpoint->vtable[0](object, first, second, third, fourth);
    }
}
void GameSystemRegistrationOwner::install() {
    if (!*head_slot)
        return;
    node.endpoint = &endpoint;
    node.next = **head_slot;
    **head_slot = &node;
    registered = 1;
}
void GameSystemRegistrationOwner::remove() {
    if (unload->unloading || !*head_slot || !registered)
        return;
    registered = 0;
    auto** link = *head_slot;
    for (auto* current = *link; current; current = *link) {
        if (current == &node) {
            *link = node.next;
            node.next = nullptr;
            return;
        }
        link = &current->next;
    }
}
GameSystemRegistrationOwner*
create_game_system_registration(OwnerRegistryBinding owners,
                                GameSystemRegistrationNode** const* head_slot,
                                const OwnerRegistry& callbacks, UnloadState& unload) {
    auto* storage = allocate(owners.memory, sizeof(GameSystemRegistrationOwner));
    if (!storage)
        return nullptr;
    auto* owner = ::new (storage) GameSystemRegistrationOwner;
    owners.append(owner);
    owner->node.endpoint = nullptr;
    owner->node.next = nullptr;
    owner->registered = 0;
    owner->empty_name = 0;
    owner->vtable = owner_table.data();
    owner->endpoint.vtable = &registration_table;
    owner->node.name = &owner->empty_name;
    owner->head_slot = head_slot;
    owner->callbacks = &callbacks;
    owner->unload = &unload;
    owner->memory = owners.memory;
    return owner;
}
#undef SYSTEM_MEMBER
} //namespace skeet::engine
