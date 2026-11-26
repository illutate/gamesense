#include "skeet/engine/build_runtime_type_bindings.hpp"
#include "skeet/engine/netvars.hpp"
#include <cstring>
#include <sstream>

namespace skeet::engine {
namespace {
constexpr std::array<BuildRuntimeTypeRecipe, 15> recipes{
    {{BuildRuntimeType::trace_filter_simple, 0x0FC0u, 0xF17DA9A2u, 0x434758B4u, 0u,
      ".?AVCTraceFilterSimple@@"},
     {BuildRuntimeType::trace_filter_skip_two_entities, 0x0FCCu, 0x0A3AB8DEu, 0x43475B90u, 0u,
      ".?AVCTraceFilterSkipTwoEntities@@"},
     {BuildRuntimeType::base_player_animation_state, 0x0FD8u, 0x73D50E69u, 0x4346A460u, 0u,
      ".?AVCBasePlayerAnimState@@"},
     {BuildRuntimeType::game_rules_proxy, 0x0FE4u, 0x479D7323u, 0x4346A720u, 0u,
      ".?AVC_CSGameRulesProxy@@"},
     {BuildRuntimeType::cs_ragdoll, 0x0FF0u, 0x17843051u, 0x43467DC0u, 0u, ".?AVC_CSRagdoll@@"},
     {BuildRuntimeType::cs_ragdoll_renderable, 0x0FFCu, 0x17843051u, 0x43467190u, 4u,
      ".?AVC_CSRagdoll@@"},
     {BuildRuntimeType::econ_item_attribute, 0x1008u, 0x7E00C86Du, 0x4346848Cu, 0u,
      ".?AVCEconItemAttribute@@"},
     {BuildRuntimeType::viewmodel_attachment_renderable, 0x1014u, 0x0C3B7DAEu, 0x4346A81Cu, 4u,
      ".?AVC_ViewmodelAttachmentModel@@"},
     {BuildRuntimeType::predicted_viewmodel_renderable, 0x1020u, 0x17CEECD0u, 0x43468D28u, 4u,
      ".?AVC_PredictedViewModel@@"},
     {BuildRuntimeType::weapon_world_model_renderable, 0x102Cu, 0x24C25B1Au, 0x4346A444u, 4u,
      ".?AVC_BaseWeaponWorldModel@@"},
     {BuildRuntimeType::econ_wearable_renderable, 0x1038u, 0x0AE8CF3Cu, 0x43468908u, 4u,
      ".?AVC_EconWearable@@"},
     {BuildRuntimeType::cs_player, 0x1044u, 0x1B1A0D29u, 0x43467D54u, 0u, ".?AVC_CSPlayer@@"},
     {BuildRuntimeType::base_cs_grenade, 0x1050u, 0x581536C7u, 0x4346A43Cu, 0u,
      ".?AVC_BaseCSGrenade@@"},
     {BuildRuntimeType::player_addon_model_renderable, 0x105Cu, 0xFB0368F4u, 0x43469284u, 4u,
      ".?AVC_PlayerAddonModel@@"},
     {BuildRuntimeType::weapon_cs_base, 0x1068u, 0x05F64981u, 0x43467F84u, 0u,
      ".?AVC_WeaponCSBase@@"}}};
template <class T> T read(const std::byte* address) {
    T value;
    std::memcpy(&value, address, sizeof value);
    return value;
}
struct Section {
    std::uint32_t rva, size, flags;
};
constexpr Section text_section{0x1000u, 0xB38409u, 0x60000020u};
constexpr Section rdata_section{0xB3A000u, 0x26629Cu, 0x40000040u};
constexpr Section data_section{0xDA1000u, 0x460C3E0u, 0xC0000040u};
struct ClientImage {
    std::byte* base;
    bool within(const void* pointer, std::size_t size, const Section& section) const noexcept {
        const auto address = reinterpret_cast<std::uintptr_t>(pointer);
        const auto first = reinterpret_cast<std::uintptr_t>(base) + section.rva;
        return address >= first && size <= section.size && address - first <= section.size - size;
    }
};
ClientImage checked_image(std::byte* module, NativeImageEnvironment images) {
    if (!module || !images.image_nt_header)
        throw std::invalid_argument(
            "Runtime RTTI requires the published client module and native PE-header import");
    const auto* nt = images.image_nt_header(module);
    if (!nt || nt != module + 0x150 || read<std::uint16_t>(module) != 0x5A4Du ||
        read<std::uint32_t>(module + 0x3C) != 0x150u || read<std::uint32_t>(nt) != 0x4550u ||
        read<std::uint16_t>(nt + 4) != 0x14Cu || read<std::uint16_t>(nt + 6) != 7u ||
        read<std::uint16_t>(nt + 0x14) != 0xE0u || read<std::uint16_t>(nt + 0x18) != 0x10Bu ||
        read<std::uint32_t>(nt + 8) != 0x65272025u ||
        read<std::uint32_t>(nt + 0x50) != 0x5509000u || read<std::uint32_t>(nt + 0x58) != 0xFA635Fu)
        throw std::runtime_error("Runtime RTTI requires the supported client PE32 build");
    constexpr std::array<const char*, 3> names{".text", ".rdata", ".data"};
    constexpr std::array<Section, 3> expected{text_section, rdata_section, data_section};
    std::array<bool, 3> found{};
    for (std::size_t index = 0; index < 7; ++index) {
        const auto* section = nt + 0xF8 + index * 40;
        const auto rva = read<std::uint32_t>(section + 12), size = read<std::uint32_t>(section + 8);
        if (rva > 0x5509000u || size > 0x5509000u - rva)
            throw std::runtime_error("Runtime RTTI section exceeds its client image");
        for (std::size_t wanted = 0; wanted < names.size(); ++wanted) {
            std::array<char, 8> name{};
            std::memcpy(name.data(), names[wanted], std::strlen(names[wanted]));
            if (std::memcmp(section, name.data(), name.size()))
                continue;
            const auto& profile = expected[wanted];
            if (found[wanted] || rva != profile.rva || size != profile.size ||
                read<std::uint32_t>(section + 36) != profile.flags)
                throw std::runtime_error("Runtime RTTI client section fingerprint differs");
            found[wanted] = true;
        }
    }
    if (!found[0] || !found[1] || !found[2])
        throw std::runtime_error("Runtime RTTI client data sections are unavailable");
    return {module};
}
std::size_t index_of(BuildRuntimeType type) {
    const auto index = static_cast<std::size_t>(type);
    if (index >= recipes.size())
        throw std::out_of_range("Unknown build runtime RTTI identity");
    return index;
}
std::string failure_message(std::size_t index, const BuildRuntimeTypeRecipe& row,
                            BuildRuntimeTypeFailureReason reason) {
    std::ostringstream message;
    message << "Runtime RTTI " << index << " '" << row.decorated_name << "' (hash0x" << std::hex
            << row.type_hash << ", offset0x" << row.object_offset << ") ";
    switch (reason) {
    case BuildRuntimeTypeFailureReason::missing_type:
        message << "has no type descriptor";
        break;
    case BuildRuntimeTypeFailureReason::unexpected_type_name:
        message << "has an unexpected descriptor name";
        break;
    case BuildRuntimeTypeFailureReason::missing_vtable:
        message << "has no matching vtable";
        break;
    case BuildRuntimeTypeFailureReason::invalid_vtable:
        message << "has invalid table provenance";
        break;
    }
    return message.str();
}
}

std::span<const BuildRuntimeTypeRecipe, 15> build_runtime_type_manifest() noexcept {
    return recipes;
}
BuildRuntimeTypeFailure::BuildRuntimeTypeFailure(std::size_t failed_index,
                                                 const BuildRuntimeTypeRecipe& row,
                                                 BuildRuntimeTypeFailureReason classification)
    : std::runtime_error(failure_message(failed_index, row, classification)), index(failed_index),
      identity(row.identity), operand(row.operand), type_hash(row.type_hash),
      destination_cell(row.destination_cell), object_offset(row.object_offset),
      decorated_name(row.decorated_name), reason(classification) {}
BuildRuntimeTypeBindings::BuildRuntimeTypeBindings(NativeDataImage& data,
                                                   const std::array<RttiTypeDescriptor*, 15>& types)
    : data_(data), types_(types) {}
RttiTypeDescriptor* BuildRuntimeTypeBindings::type(BuildRuntimeType identity) const {
    return types_[index_of(identity)];
}
void** BuildRuntimeTypeBindings::vtable(BuildRuntimeType identity) const {
    return data_.native_cell<void**>(recipes[index_of(identity)].destination_cell);
}
BuildRuntimeTypeBindings initialize_build_runtime_type_bindings(NativeDataImage& data,
                                                                NativeImageEnvironment images) {
    std::array<void***, recipes.size()> destinations;
    for (std::size_t index = 0; index < recipes.size(); ++index) {
        const auto& row = recipes[index];
        if (index_of(row.identity) != index || row.operand != 0xFC0u + index * 12u ||
            netvar_name_hash(row.decorated_name) != row.type_hash)
            throw std::logic_error("Runtime RTTI manifest identity or hash differs");
        destinations[index] = &data.native_cell<void**>(row.destination_cell);
    }
    const auto image = checked_image(data.native_cell<std::byte*>(0x4346A55Cu), images);
    std::array<RttiTypeDescriptor*, recipes.size()> types;
    for (std::size_t index = 0; index < recipes.size(); ++index) {
        const auto& row = recipes[index];
        types[index] = find_rtti_type(image.base, row.type_hash, images);
        if (!types[index])
            throw BuildRuntimeTypeFailure(index, row, BuildRuntimeTypeFailureReason::missing_type);
        const auto length = std::strlen(row.decorated_name) + 1u;
        if (!image.within(types[index], offsetof(RttiTypeDescriptor, name) + length,
                          data_section) ||
            std::memcmp(types[index]->name, row.decorated_name, length))
            throw BuildRuntimeTypeFailure(index, row,
                                          BuildRuntimeTypeFailureReason::unexpected_type_name);
        auto** table =
            find_rtti_vtable_at_offset(image.base, types[index], row.object_offset, images);
        if (!table)
            throw BuildRuntimeTypeFailure(index, row,
                                          BuildRuntimeTypeFailureReason::missing_vtable);
        if (!image.within(table, sizeof(void*), rdata_section) ||
            !image.within(*table, 1u, text_section))
            throw BuildRuntimeTypeFailure(index, row,
                                          BuildRuntimeTypeFailureReason::invalid_vtable);
        *destinations[index] = table;
    }
    return {data, types};
}
BuildRuntimeTypeBindings initialize_build_runtime_type_bindings(NativeDataImage& data) {
    return initialize_build_runtime_type_bindings(data, native_image_environment());
}
} //namespace skeet::engine
