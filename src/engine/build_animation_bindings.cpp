#include "skeet/engine/build_animation_bindings.hpp"
#include "skeet/animation/remote_setup_bones.hpp"
#include <array>
#include <cstring>
#include <initializer_list>
#include <stdexcept>

namespace skeet::engine {
namespace {
template <class T> T read(const std::byte* pointer) {
    T value;
    std::memcpy(&value, pointer, sizeof value);
    return value;
}
void require(bool condition, const char* reason) {
    if (!condition)
        throw std::runtime_error(reason);
}
struct Section {
    char name[8];
    std::uint32_t rva, size, flags;
};
struct Profile {
    std::uint32_t nt, timestamp, size, checksum;
    std::array<Section, 3> sections;
};
// These identities and instruction anchors describe this supported build pair;
// they are source integration evidence, not the original protected resolver.
constexpr Profile client_build{0x150,
                               0x65272025,
                               0x5509000,
                               0xFA635F,
                               {{{".text", 0x1000, 0xB38409, 0x60000020},
                                 {".rdata", 0xB3A000, 0x26629C, 0x40000040},
                                 {".data", 0xDA1000, 0x460C3E0, 0xC0000040}}}};
constexpr Profile tier0_build{0x110,
                              0x63DC10BA,
                              0x307000,
                              0x5E167,
                              {{{".text", 0x1000, 0x3BD56, 0x60000020},
                                {".rdata", 0x3D000, 0x1157E, 0x40000040},
                                {".data", 0x4F000, 0x2ADF90, 0xC0000040}}}};
struct Image {
    std::byte* base;
    const Profile& profile;
    bool within(const void* pointer, std::size_t size, std::size_t section) const noexcept {
        const auto& s = profile.sections[section];
        const auto address = reinterpret_cast<std::uintptr_t>(pointer);
        const auto begin = reinterpret_cast<std::uintptr_t>(base) + s.rva;
        return address >= begin && size <= s.size && address - begin <= s.size - size;
    }
    const std::byte* rdata(std::uint32_t rva, std::size_t size) const {
        require(rva <= profile.size && size <= profile.size - rva,
                "Animation PE table exceeds its image");
        const auto* value = base + rva;
        require(within(value, size, 1), "Animation PE table is outside read-only data");
        return value;
    }
};
Image checked(void* module, const Profile& profile) {
    require(module != nullptr, "Required animation client/tier0 module is not loaded");
    auto* base = static_cast<std::byte*>(module);
    require(read<std::uint16_t>(base) == 0x5A4D && read<std::uint32_t>(base + 0x3C) == profile.nt,
            "Unsupported animation DOS/NT-header layout");
    const auto* nt = base + profile.nt;
    require(read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt + 4) == 0x14C &&
                read<std::uint16_t>(nt + 6) == 7 && read<std::uint16_t>(nt + 0x14) == 0xE0 &&
                read<std::uint16_t>(nt + 0x18) == 0x10B,
            "Unsupported animation PE32 layout");
    require(read<std::uint32_t>(nt + 8) == profile.timestamp &&
                read<std::uint32_t>(nt + 0x50) == profile.size &&
                read<std::uint32_t>(nt + 0x58) == profile.checksum,
            "Unsupported animation client/tier0 build identity");
    std::array<bool, 3> found{};
    for (std::size_t i = 0; i < 7; ++i) {
        const auto* section = nt + 0xF8 + i * 40;
        const auto rva = read<std::uint32_t>(section + 12), size = read<std::uint32_t>(section + 8);
        require(rva <= profile.size && size <= profile.size - rva,
                "Animation section exceeds its image");
        for (std::size_t j = 0; j < found.size(); ++j) {
            const auto& wanted = profile.sections[j];
            if (std::memcmp(section, wanted.name, 8))
                continue;
            require(!found[j] && rva == wanted.rva && size == wanted.size &&
                        read<std::uint32_t>(section + 36) == wanted.flags,
                    "Unsupported animation section fingerprint");
            found[j] = true;
        }
    }
    require(found[0] && found[1] && found[2], "Required animation image section is missing");
    return {base, profile};
}
bool match(const std::byte* code, std::initializer_list<int> pattern) noexcept {
    for (const auto byte : pattern) {
        if (byte >= 0 && static_cast<unsigned char>(*code) != byte)
            return false;
        ++code;
    }
    return true;
}
void expect(const Image& image, const std::byte* code, std::initializer_list<int> pattern) {
    require(image.within(code, pattern.size(), 0) && match(code, pattern),
            "Animation instruction fingerprint differs");
}
const std::byte* unique(const Image& image, std::initializer_list<int> pattern, std::uint32_t rva) {
    const auto& text = image.profile.sections[0];
    const std::byte* found = nullptr;
    for (std::uint32_t i = 0; i <= text.size - pattern.size(); ++i) {
        const auto* at = image.base + text.rva + i;
        if (!match(at, pattern))
            continue;
        require(!found, "Animation instruction anchor is ambiguous");
        found = at;
    }
    require(found == image.base + rva, "Animation instruction anchor is missing or unsupported");
    return found;
}
const std::byte* absolute(const Image& image, const std::byte* operand, std::size_t size,
                          std::size_t section) {
    const auto* value =
        reinterpret_cast<const std::byte*>(std::uintptr_t(read<std::uint32_t>(operand)));
    require(image.within(value, size, section),
            "Decoded animation pointer has wrong section provenance");
    return value;
}
const std::byte* call(const Image& image, const std::byte* code, std::uint32_t rva) {
    expect(image, code, {0xE8, -1, -1, -1, -1});
    const auto offset = std::int64_t(code - image.base) + 5 + read<std::int32_t>(code + 1);
    require(offset >= 0 && offset < image.profile.size, "Animation call leaves its loaded image");
    const auto* value = image.base + static_cast<std::size_t>(offset);
    require(image.within(value, 1, 0) && value == image.base + rva,
            "Animation call target is unsupported");
    return value;
}
bool equal_name(const Image& image, const std::byte* at, const char* name, bool fold = false) {
    for (;; ++at, ++name) {
        require(image.within(at, 1, 1), "Animation PE name leaves read-only data");
        auto actual = static_cast<unsigned char>(*at), wanted = static_cast<unsigned char>(*name);
        if (fold) {
            if (actual >= 'A' && actual <= 'Z')
                actual += 32;
            if (wanted >= 'A' && wanted <= 'Z')
                wanted += 32;
        }
        if (actual != wanted)
            return false;
        if (!actual)
            return true;
    }
}
const std::byte* named_export(const Image& image, const char* name) {
    const auto* nt = image.base + image.profile.nt;
    const auto rva = read<std::uint32_t>(nt + 0x78), size = read<std::uint32_t>(nt + 0x7C);
    const auto* directory = image.rdata(rva, 40);
    const auto count = read<std::uint32_t>(directory + 24),
               functions_count = read<std::uint32_t>(directory + 20);
    require(count <= image.profile.size / 4 && functions_count <= image.profile.size / 4,
            "Animation export count exceeds its image");
    const auto* names = image.rdata(read<std::uint32_t>(directory + 32), count * 4);
    const auto* ordinals = image.rdata(read<std::uint32_t>(directory + 36), count * 2);
    const auto* functions = image.rdata(read<std::uint32_t>(directory + 28), functions_count * 4);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!equal_name(image, image.rdata(read<std::uint32_t>(names + i * 4), 1), name))
            continue;
        const auto ordinal = read<std::uint16_t>(ordinals + i * 2);
        require(ordinal < functions_count, "Animation export ordinal exceeds its function table");
        const auto target = read<std::uint32_t>(functions + ordinal * 4);
        require(!(target >= rva && target - rva < size) && target < image.profile.size,
                "Animation predicate export is forwarded or invalid");
        const auto* result = image.base + target;
        require(image.within(result, 1, 0),
                "Animation predicate export is outside executable text");
        return result;
    }
    throw std::runtime_error("Required tier0 ThreadInMainThread export is missing");
}
const std::byte* import_slot(const Image& image, const char* module, const char* name) {
    const auto* nt = image.base + image.profile.nt;
    const auto size = read<std::uint32_t>(nt + 0x84);
    const auto* directories = image.rdata(read<std::uint32_t>(nt + 0x80), size);
    for (std::uint32_t offset = 0; offset + 20 <= size; offset += 20) {
        const auto* directory = directories + offset;
        const auto original = read<std::uint32_t>(directory),
                   module_name = read<std::uint32_t>(directory + 12),
                   iat = read<std::uint32_t>(directory + 16);
        if (!original && !module_name && !iat)
            break;
        if (!equal_name(image, image.rdata(module_name, 1), module, true))
            continue;
        require(original && iat, "Animation import requires original named thunks");
        for (std::uint32_t index = 0; index < image.profile.size / 4; ++index) {
            const auto value = read<std::uint32_t>(image.rdata(original + index * 4, 4));
            if (!value)
                break;
            if (value & 0x80000000u)
                continue;
            if (equal_name(image, image.rdata(value + 2, 1), name))
                return image.rdata(iat + index * 4, 4);
        }
    }
    throw std::runtime_error("Required animation named import is missing");
}
struct Targets {
    const std::byte *prepare, *attachments, *bone_update, *predicate;
    const std::int32_t* counter;
};
Targets acquire(const Image& client, const Image& tier0) {
    // The native SetupBones path calls prepare and immediately reads the live
    // global counter; a second counter read independently confirms its operand.
    const auto* prepare_site =
        unique(client, {0x8D, 0x4F, 0xFC, 0xE8, -1, -1, -1,   -1,   0xA1, -1,   -1, -1, -1,
                        0x09, 0xB7, 0x9C, 0x26, 0,  0,  0x89, 0x87, 0x8C, 0x26, 0,  0},
               0x1D3490);
    const auto* prepare = call(client, prepare_site + 3, 0x1D2CA0);
    const auto* counter = absolute(client, prepare_site + 9, 4, 2);
    require(counter == client.base + 0x5332374, "Animation bone counter operand is unsupported");
    const auto* counter_site =
        unique(client, {0x8B, 0x87, 0x8C, 0x26, 0,  0,  0xF3, 0x0F, 0x11, 0x44, 0x24, 0x18,
                        0x3B, 0x05, -1,   -1,   -1, -1, 0x0F, 0x84, 0xC0, 0,    0,    0},
               0x1D33B8);
    require(absolute(client, counter_site + 0xE, 4, 2) == counter,
            "Native animation counter operands disagree");
    expect(client, prepare, {0x55, 0x8B, 0xEC, 0x51, 0x80, 0x3D, -1,   -1,   -1, -1,   0,
                             0x56, 0x8B, 0xF1, 0x0F, 0x84, 0xC4, 0,    0,    0,  0x80, 0x3D,
                             -1,   -1,   -1,   -1,   0,    0x0F, 0x85, 0xB7, 0,  0,    0});
    absolute(client, prepare + 6, 1, 2);
    absolute(client, prepare + 0x16, 1, 2);
    expect(client, prepare + 0xD8, {0x5E, 0x8B, 0xE5, 0x5D, 0xC3});
    const auto* attachments_site =
        unique(client, {0xF7, 0x44, 0x24, 0x4C, 0,    0x02, 0,  0,  0x75, 0x13,
                        0xF7, 0x44, 0x24, 0x14, 0,    0x02, 0,  0,  0x74, 0x09,
                        0x56, 0x8D, 0x4F, 0xFC, 0xE8, -1,   -1, -1, -1},
               0x1D3DD7);
    const auto* attachments = call(client, attachments_site + 0x18, 0x1D1BA0);
    expect(client, attachments,
           {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x48, 0x53, 0x8B, 0x5D, 0x08, 0x89, 0x4D,
            0xF4, 0x56, 0x57, 0x85, 0xDB, 0x0F, 0x84, 0xF9, 0x01, 0,    0});
    expect(client, attachments + 0x210, {0x5F, 0x5E, 0x5B, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0});
    const auto* update_site =
        unique(client, {0xF7, 0xC7, 0, 0xFF, 0x0F, 0,    0x74, 0x0E, 0x57, 0xFF, 0xB6,
                        0xA8, 0x26, 0, 0,    0x8B, 0xCE, 0xE8, -1,   -1,   -1,   -1},
               0x3E9748);
    const auto* bone_update = call(client, update_site + 0x11, 0x432E20);
    expect(client, bone_update,
           {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC, 0x70, 0x56, 0x57, 0x8B, 0xF9,
            0x89, 0x7C, 0x24, 0x38, 0x83, 0xBF, 0x50, 0x29, 0,    0,    0,    0x75, 0x16});
    expect(client, bone_update + 0x687, {0x5F, 0x5E, 0x8B, 0xE5, 0x5D, 0xC2, 0x08, 0});
    const auto* predicate = named_export(tier0, "ThreadInMainThread");
    require(predicate == tier0.base + 0x12FE0, "tier0 thread predicate export is unsupported");
    expect(tier0, predicate,
           {0xFF, 0x15, -1, -1, -1, -1, 0x33, 0xC9, 0x3B, 0x05, -1, -1, -1, -1, 0x0F, 0x94, 0xC0,
            0xC3});
    const auto* current_thread_slot = absolute(tier0, predicate + 2, 4, 1);
    require(current_thread_slot == import_slot(tier0, "KERNEL32.dll", "GetCurrentThreadId"),
            "Tier0 predicate does not call its genuine thread-ID import");
    absolute(tier0, predicate + 0xA, 4, 2);
    const auto* client_thread_slot = import_slot(client, "tier0.dll", "ThreadInMainThread");
    require(read<std::uint32_t>(client_thread_slot) == reinterpret_cast<std::uintptr_t>(predicate),
            "Client thread predicate import differs from the actual tier0 export");
    return {prepare, attachments, bone_update, predicate,
            reinterpret_cast<const std::int32_t*>(counter)};
}
const std::byte* acquire_animation_update(const Image& client) {
    const auto* caller =
        unique(client, {0xF3, 0x0F, 0x10, 0x55, 0xFC, 0x51, 0x8B, 0x8E, 0x60, 0x99, 0,
                        0,    0xF3, 0x0F, 0x10, 0x48, 0x04, 0xE8, -1,   -1,   -1,   -1},
               0x3E7AAB);
    const auto* update = call(client, caller + 0x11, 0x43E9E0);
    const auto* second =
        unique(client, {0xF3, 0x0F, 0x10, 0x54, 0x24, 0x04, 0x51, 0x8B, 0x8E, 0x60, 0x99, 0,
                        0,    0xF3, 0x0F, 0x10, 0x48, 0x04, 0xE8, -1,   -1,   -1,   -1},
               0x3DE5A7);
    require(call(client, second + 0x12, 0x43E9E0) == update,
            "Native animation Update callers disagree");
    expect(client, update,
           {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC, 0x18, 0x56, 0x57, 0x8B, 0xF9, 0xF3,
            0x0F, 0x11, 0x54, 0x24, 0x0C, 0xF3, 0x0F, 0x11, 0x4C, 0x24, 0x10, 0x8B, 0x4F, 0x60,
            0x85, 0xC9, 0x0F, 0x84, 0x93, 0x04, 0,    0,    0x8B, 0x01, 0x8B, 0x80, 0x70, 0x02,
            0,    0,    0xFF, 0xD0, 0x84, 0xC0, 0x75, 0x10, 0x8B, 0x47, 0x60, 0x80, 0xB8, 0xE1,
            0x9A, 0,    0,    0,    0x0F, 0x84, 0x75, 0x04, 0,    0});
    expect(client, update + 0x4B7, {0x5F, 0x5E, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0});
    require(client.within(update, 512, 0), "Animation Update startup scan leaves executable text");
    // The next native startup search uses this exact11-byte wildcard pattern.
    // Validate its actual instruction operand, but leave its publication to
    // that recovered search operation rather than inventing a later VM write.
    expect(client, update + 0xE0, {0xC6, 0x05, -1, -1, -1, -1, -1, 0xF3, 0x0F, 0x5F, 0x05});
    const auto* flag = absolute(client, update + 0xE2, 1, 2);
    expect(client, update + 0x4AD, {0xC6, 0x05, -1, -1, -1, -1, 0x01, 0x89, 0x47, 0x70});
    require(absolute(client, update + 0x4AF, 1, 2) == flag,
            "Animation Update flag stores disagree");
    return update;
}
const std::byte* acquire_animation_velocity(const Image& client) {
    const auto* update = acquire_animation_update(client);
    expect(client, update + 0x364, {0x8B, 0xCF, 0xE8, -1, -1, -1, -1});
    const auto* helper = call(client, update + 0x366, 0x443410);
    expect(client, helper,
           {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC, 0x30, 0x56, 0x57, 0x8B, 0x3D,
            -1,   -1,   -1,   -1,   0x8B, 0xF1, 0x8B, 0xCF, 0x89, 0x74, 0x24, 0x14, 0x8B,
            0x07, 0xFF, 0x90, 0x84, 0,    0,    0,    0x8B, 0x0D, -1,   -1,   -1,   -1,
            0xF3, 0x0F, 0x7E, 0x86, 0xC8, 0,    0,    0,    0x8B, 0x86, 0xD0, 0,    0,
            0,    0x89, 0x44, 0x24, 0x28, 0x66, 0x0F, 0xD6, 0x44, 0x24, 0x20, 0x8B, 0x01,
            0x8B, 0x80, 0x74, 0x01, 0,    0,    0xFF, 0xD0, 0x84, 0xC0, 0x75, 0x38, 0x8B,
            0x0D, -1,   -1,   -1,   -1,   0x8B, 0x01, 0x8B, 0x80, 0x48, 0x01, 0,    0,
            0xFF, 0xD0, 0x84, 0xC0, 0x75, 0x24, 0x8B, 0x4E, 0x60, 0x8D, 0x54, 0x24, 0x20,
            0x52, 0x8B, 0x01, 0xFF, 0x90, 0x44, 0x02, 0,    0});
    absolute(client, helper + 0xD, 4, 2);
    const auto* interface_global = absolute(client, helper + 0x23, 4, 2);
    require(absolute(client, helper + 0x4F, 4, 2) == interface_global,
            "Animation vector helper globals disagree");
    const auto* block = helper + 0x71;
    expect(client, block, {0xF3, 0x0F, 0x10, 0x4C, 0x24, 0x28, 0xF3, 0x0F, 0x10, 0x5C,
                           0x24, 0x24, 0xF3, 0x0F, 0x10, 0x64, 0x24, 0x20, 0xEB, 0x38});
    const auto* hash_window = block + 0x14 + read<std::int8_t>(block + 0x13);
    require(hash_window == block + 0x4C,
            "Animation vector branch does not reach the startup hash input");
    // Native4333D6A2 receives exactly these32 bytes through cell+4C. The final
    // three bytes are the COMISS opcode; its relocated absolute operand is
    // outside that hash window. This is code validation, not a supplied hash.
    expect(client, hash_window, {0x0F, 0x28, 0xC4, 0x0F, 0x28, 0xD3, 0xF3, 0x0F, 0x59, 0xC4, 0xF3,
                                 0x0F, 0x59, 0xD3, 0xF3, 0x0F, 0x58, 0xD0, 0x0F, 0x28, 0xC1, 0xF3,
                                 0x0F, 0x59, 0xC1, 0xF3, 0x0F, 0x58, 0xD0, 0x0F, 0x2F, 0x15});
    absolute(client, hash_window + 0x20, 4, 1);
    // The next startup read copies this JBE displacement (8B), not an address.
    expect(client, hash_window + 0x24, {0x0F, 0x86, 0x8B, 0, 0, 0});
    return block;
}
}

const std::byte* initialize_build_animation_update_code(NativeDataImage& data,
                                                        const LoadedModuleEnvironment& modules) {
    require(sizeof(void*) == 4 && data.size() >= NativeDataImage::image_size,
            "Animation Update code binding requires the complete canonical x86 data image");
    require(
        modules.loader && modules.acquire_peb_lock && modules.release_peb_lock,
        "Animation Update code binding requires the actual loaded-module list and lock imports");
    const auto client =
        checked(find_loaded_module(modules, loaded_module_name_hash(u"client.dll", -1), true, true),
                client_build);
    const auto* update = acquire_animation_update(client);
    data.native_cell<const std::byte*>(0x434693F0u) = update;
    return update;
}

const std::byte* initialize_build_animation_velocity_code(NativeDataImage& data,
                                                          const LoadedModuleEnvironment& modules) {
    require(sizeof(void*) == 4 && data.size() >= NativeDataImage::image_size,
            "Animation velocity code binding requires the complete canonical x86 data image");
    require(
        modules.loader && modules.acquire_peb_lock && modules.release_peb_lock,
        "Animation velocity code binding requires the actual loaded-module list and lock imports");
    const auto client =
        checked(find_loaded_module(modules, loaded_module_name_hash(u"client.dll", -1), true, true),
                client_build);
    const auto* block = acquire_animation_velocity(client);
    data.native_cell<const std::byte*>(0x43467F0Cu) = block;
    return block;
}

animation::BoneAttachmentUpdate
initialize_build_animation_bindings(NativeDataImage& data, const LoadedModuleEnvironment& modules) {
    require(sizeof(void*) == 4 && data.size() >= NativeDataImage::image_size,
            "Animation bindings require the complete canonical x86 data image");
    require(modules.loader && modules.acquire_peb_lock && modules.release_peb_lock,
            "Animation bindings require the actual loaded-module list and lock imports");
    const auto client =
        checked(find_loaded_module(modules, loaded_module_name_hash(u"client.dll", -1), true, true),
                client_build);
    const auto tier0 =
        checked(find_loaded_module(modules, loaded_module_name_hash(u"tier0.dll", -1), true, true),
                tier0_build);
    const auto targets = acquire(client, tier0);
    const auto update = reinterpret_cast<animation::BoneAttachmentUpdate>(
        const_cast<std::byte*>(targets.bone_update));
    data.native_cell<animation::BoneEnginePrepare>(0x4346A38Cu) =
        reinterpret_cast<animation::BoneEnginePrepare>(const_cast<std::byte*>(targets.prepare));
    data.native_cell<animation::BoneEngineAttachments>(0x4346A6B0u) =
        reinterpret_cast<animation::BoneEngineAttachments>(
            const_cast<std::byte*>(targets.attachments));
    data.native_cell<const std::int32_t*>(0x43469060u) = targets.counter;
    data.native_cell<animation::BoneEngineThreadPredicate>(0x43476920u) =
        reinterpret_cast<animation::BoneEngineThreadPredicate>(
            const_cast<std::byte*>(targets.predicate));
    data.native_cell<animation::BoneAttachmentUpdate>(0x43468F80u) = update;
    return update;
}
} // namespace skeet::engine
