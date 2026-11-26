#include "skeet/engine/build_module_bindings.hpp"
#include "skeet/engine/source_initialization_imports.hpp"
#include <cstring>
#include <stdexcept>
#include <string>

namespace skeet::engine {
namespace {
constexpr std::array<BuildModuleRecipe, 17> recipes{{
    {0x434670ACu, 0xFAE7A604u, u"matchmaking.dll"},
    {0x434670E4u, 0x70A76D19u, u"server.dll"},
    {0x4346762Cu, 0xD45F4768u, u"panoramauiclient.dll"},
    {0x43467AFCu, 0x424EC73Eu, u"vguimatsurface.dll"},
    {0x43467B74u, 0xDF6E939Bu, u"filesystem_stdio.dll"},
    {0x43467DACu, 0xDB89A589u, u"inputsystem.dll"},
    {0x4346889Cu, 0xAD47D7B1u, u"vgui2.dll"},
    {0x43468A7Cu, 0x0BCD5926u, u"vstdlib.dll"},
    {0x43468B74u, 0x9BB09935u, u"vphysics.dll"},
    {0x43468D20u, 0xAA8F5C60u, u"engine.dll"},
    {0x43468D40u, 0x2D366476u, u"soundsystem.dll"},
    {0x43468D98u, 0x9BD3DF1Eu, u"datacache.dll"},
    {0x43469234u, 0xA7DF62D9u, u"panorama.dll"},
    {0x4346A55Cu, 0xFE901E85u, u"client.dll"},
    {0x4346A600u, 0x1533A6AAu, u"materialsystem.dll"},
    {0x4346A604u, 0xBBF5D94Cu, u"shaderapidx9.dll"},
    {0x4346A650u, 0x76C44BACu, u"studiorender.dll"},
}};
constexpr BuildModuleRecipe overlay_recipe{0x43468D08u, 0x8B08B50Du, u"gameoverlayrenderer.dll"};

void validate_recipe(NativeDataImage& data, const BuildModuleRecipe& recipe) {
    (void)data.native_cell<void*>(recipe.destination_cell);
    if (loaded_module_name_hash(recipe.module_name.data(),
                                static_cast<std::int32_t>(recipe.module_name.size())) !=
        recipe.salted_name_hash)
        throw std::logic_error("Build module name differs from its loader hash");
}
[[noreturn]] void missing_module(const BuildModuleRecipe& recipe) {
    throw std::runtime_error("Required build module is not loaded: " +
                             std::string(recipe.module_name.begin(), recipe.module_name.end()));
}
[[noreturn]] void invalid_module(const BuildModuleRecipe& recipe) {
    throw std::runtime_error("Loaded build module has an invalid basename or PE32 image: " +
                             std::string(recipe.module_name.begin(), recipe.module_name.end()));
}
struct PebLock {
    const LoadedModuleEnvironment& environment;
    explicit PebLock(const LoadedModuleEnvironment& source) : environment(source) {
        environment.acquire_peb_lock();
    }
    ~PebLock() {
        environment.release_peb_lock();
    }
};
template <class T> T read(const std::byte* address) {
    T value;
    std::memcpy(&value, address, sizeof value);
    return value;
}
bool exact_name(const LoaderUnicodeName& actual, std::u16string_view expected) noexcept {
    if (!actual.buffer || (actual.length & 1u) || actual.length / 2u != expected.size())
        return false;
    for (std::size_t index = 0; index < expected.size(); ++index) {
        auto value = actual.buffer[index];
        if (value >= u'A' && value <= u'Z')
            value += u'a' - u'A';
        if (value != expected[index])
            return false;
    }
    return true;
}
bool pe32_image(const LoaderModule& module) {
    if (module.image_size < 0x40)
        return false;
    const auto* const base = static_cast<const std::byte*>(module.base);
    if (read<std::uint16_t>(base) != 0x5A4Du)
        return false;
    const auto offset = read<std::uint32_t>(base + 0x3C);
    if (offset < 0x40 || offset > module.image_size - 0x1Au)
        return false;
    const auto* const nt = base + offset;
    const auto optional_size = read<std::uint16_t>(nt + 0x14);
    if (optional_size < 2 || optional_size > module.image_size - offset - 0x18u)
        return false;
    return read<std::uint32_t>(nt) == 0x00004550u && read<std::uint16_t>(nt + 4) == 0x014Cu &&
           read<std::uint16_t>(nt + 0x18) == 0x010Bu;
}
void* resolve_module(const LoadedModuleEnvironment& environment, const BuildModuleRecipe& recipe) {
    const PebLock locked(environment);
    // Reuse433F29A5's hash resolver under this same existing PEB lock. The
    //additional checks accept only the named x86 image, not another hash match.
    auto* const base = find_loaded_module(environment, recipe.salted_name_hash, true, false);
    if (!base)
        return nullptr;
    auto* const sentinel = &environment.loader->modules;
    for (auto* link = sentinel->next; link != sentinel; link = link->next) {
        const auto& module = *reinterpret_cast<const LoaderModule*>(link);
        if (module.base != base)
            continue;
        if (!exact_name(module.base_name, recipe.module_name) || !pe32_image(module))
            invalid_module(recipe);
        return base;
    }
    invalid_module(recipe);
}
}

std::span<const BuildModuleRecipe, 17> build_module_manifest() noexcept {
    return recipes;
}
const BuildModuleRecipe& build_overlay_module_recipe() noexcept {
    return overlay_recipe;
}
void* BuildModuleBases::at(std::u16string_view name) const {
    for (std::size_t index = 0; index < recipes.size(); ++index)
        if (recipes[index].module_name == name)
            return modules[index];
    if (overlay_recipe.module_name == name)
        return overlay;
    throw std::out_of_range("Name is not a build module binding");
}
BuildModuleBases publish_build_module_bases(NativeDataImage& data,
                                            const LoadedModuleEnvironment& environment) {
    if (!environment.loader || !environment.acquire_peb_lock || !environment.release_peb_lock)
        throw std::invalid_argument(
            "Build module publication requires the real loaded-module list and locks");
    for (const auto& recipe : recipes)
        validate_recipe(data, recipe);
    validate_recipe(data, overlay_recipe);
    BuildModuleBases result;
    for (std::size_t index = 0; index < recipes.size(); ++index) {
        result.modules[index] = resolve_module(environment, recipes[index]);
        if (!result.modules[index])
            missing_module(recipes[index]);
    }
    result.overlay = resolve_module(environment, overlay_recipe);
    for (std::size_t index = 0; index < recipes.size(); ++index)
        data.native_cell<void*>(recipes[index].destination_cell) = result.modules[index];
    data.native_cell<void*>(overlay_recipe.destination_cell) = result.overlay;
    return result;
}
BuildModuleBases publish_build_module_bases(NativeDataImage& data,
                                            const SourceInitializationImports& imports) {
    return publish_build_module_bases(data, imports.modules);
}
BuildModuleBases publish_build_module_bases(NativeDataImage& data) {
    return publish_build_module_bases(data, native_loaded_module_environment());
}
} //namespace skeet::engine
