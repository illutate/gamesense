#include "skeet/animation/layer_receive.hpp"
#include "skeet/animation/fix_animations.hpp"
#include "skeet/engine/interface_call.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <new>

namespace skeet::animation {
namespace {
template <class T> T read(const void* object, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void write(void* object, std::size_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
struct Field {
    std::uint32_t original;
    std::size_t cache;
    bool capture_early;
};
constexpr std::array<Field, 7> fields{{{0x43476AB4u, 0x480, true},
                                       {0x43476A84u, 0x470, true},
                                       {0x43476974u, 0x46C, false},
                                       {0x43476B04u, 0x474, false},
                                       {0x43476A24u, 0x47C, false},
                                       {0x43476984u, 0x478, false},
                                       {0x434769A4u, 0x468, false}}};
}
std::uint32_t LayerReceiveRuntime::offset(std::uint32_t pointer, std::uint32_t key,
                                          std::uint32_t salt) const {
    return *offsets_.globals.pointers.at(pointer) ^ offsets_.globals.keys.at(key) ^ salt;
}
void LayerReceiveRuntime::receive(ReceivedLayerField field, const engine::RecvProxyData* packet,
                                  void* layer, void* output) {
    if (field == ReceivedLayerField::sequence) {
        animations_.receive_layer(packet, layer, output);
        return;
    }
    const auto& description = fields.at(static_cast<unsigned>(field));
    auto* player = read<void*>(layer, 0x30);
    if (player && engine::invoke<bool>(player, 158)) {
        const auto begin =
            read<std::uintptr_t>(player, offset(0x4346A660u, 0x434679E0u, 0xB538BBA5u));
        const auto difference =
            std::uint32_t(reinterpret_cast<std::uintptr_t>(layer)) - std::uint32_t(begin);
        const auto layer_index = std::bit_cast<std::int32_t>(difference) / 0x38;
        auto value = description.capture_early ? read<std::uint32_t>(packet, 8) : 0u;
        const auto id = engine::invoke<std::uint32_t>(static_cast<std::byte*>(player) + 8, 10);
        auto* owner = animations_.bindings.players.get(id);
        if (!owner)
            throw std::bad_alloc();
        if (!description.capture_early)
            value = read<std::uint32_t>(packet, 8);
        write(&owner->native, description.cache + layer_index * 0x1C, value);
        const auto& globals = animations_.bindings.globals;
        if (globals.local_player == player &&
            (globals.command_mode || globals.alternate_command_mode) &&
            !read<std::uint8_t>(globals.local_player,
                                offset(0x43468E78u, 0x434684E4u, 0x9D920E35u)))
            return;
        if (field == ReceivedLayerField::weight && layer_index == 6 &&
            read<std::uint8_t>(player, offset(0x43468DBCu, 0x434689C8u, 0x8481EA04u)) !=
                read<std::uint8_t>(&owner->native, 0x17C))
            return;
    }
    data_.native_cell<engine::RecvVarProxy>(description.original)(packet, layer, output);
}
} //namespace skeet::animation
