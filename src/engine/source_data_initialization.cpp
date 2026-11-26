#include "skeet/engine/source_data_initialization.hpp"
#include "skeet/misc/settings_runtime.hpp"
#include "skeet/engine/startup_container_cleanup.hpp"
#include "skeet/engine/first_startup_services.hpp"
#include "skeet/menu/settings_menu_storage.hpp"
#include "skeet/misc/config.hpp"
#include "skeet/client/target_mask_owner.hpp"
#include "skeet/animation/fix_animation_backup.hpp"
#include <array>
#include <bit>
#include <limits>

namespace skeet::engine {
namespace {
template <class Descriptor>
void initialize_static_descriptor(Descriptor& target, const Descriptor& source) {
    target.next = source.next;
    target.previous = source.previous;
    target.hash = source.hash;
    target.serialized_size = source.serialized_size;
    target.type = source.type;
    target.reserved = source.reserved;
    target.value = source.value;
}
}
void initialize_source_configuration_data(NativeDataImage& data) {
#if UINTPTR_MAX == UINT32_MAX
    static_assert(sizeof(misc::AntiUntrustedConfig) == 20);
    static_assert(sizeof(misc::HideFromObsConfig) == 20);
    static_assert(sizeof(misc::CustomEventsConfig) == 20);
    static_assert(sizeof(misc::FreeLookConfig) == 40);
    static_assert(sizeof(client::TargetMaskSettings) == 20);
#endif
    // Each class is a single existing descriptor at the proved native base.
    // Its declaration supplies static hash/type/size/value, not a CRT default
    // or an owner registration. The source-only creation path starts zeroed.
    // Assign fields individually: BooleanConfig's three trailing padding
    // bytes are not initialized by its C++ declaration and must stay zero.
    initialize_static_descriptor(data.native_cell<misc::AntiUntrustedConfig>(0x434693DCu).enabled,
                                 misc::AntiUntrustedConfig{}.enabled);
    initialize_static_descriptor(data.native_cell<misc::HideFromObsConfig>(0x43468E48u).enabled,
                                 misc::HideFromObsConfig{}.enabled);
    initialize_static_descriptor(data.native_cell<misc::CustomEventsConfig>(0x434691A8u).enabled,
                                 misc::CustomEventsConfig{}.enabled);
    initialize_static_descriptor(data.native_cell<misc::FreeLookConfig>(0x43468264u).key,
                                 misc::FreeLookConfig{}.key);
    initialize_static_descriptor(data.native_cell<client::TargetMaskSettings>(0x43467974u).enabled,
                                 client::TargetMaskSettings{}.enabled);
}
void initialize_source_native_data(NativeDataImage& data) {
    // The three SVG texture slots start absent before433283A5 creates them.
    data.native_cell<std::int32_t>(0x43468E3Cu) = -1;
    data.native_cell<std::int32_t>(0x4346842Cu) = -1;
    data.native_cell<std::int32_t>(0x43467DA8u) = -1;
    // Immutable native weapon-classification bits shared by skin/RAGE readers.
    data.native_cell<std::array<std::uint32_t, 3>>(0x43474580u) = {0xFF8B6F9Eu, 0xB00001DFu, 1};
    ui::initialize_source_settings_menu_data(data);
    misc::initialize_source_settings_runtime_data(data);
    initialize_source_configuration_data(data);
    // The two Lua menu flags are enabled in the pinned static image. Source
    // menu bindings borrow these cells before any script owner is created.
    data.native_cell<std::uint8_t>(0x434675D4u) = 1;
    data.native_cell<std::uint8_t>(0x4346D544u) = 1;
    // The independent Lua availability gate starts enabled in static data.
    // Its only native runtime writes are the two shutdown paths clearing it;
    // leaving source BSS zero would suppress every initial Lua-state request.
    data.native_cell<bool>(0x434688BDu) = true;
    // Skins schedules HUD invalidation from menu callbacks and consumes it
    // in command phase4. Both initial values are independent static data.
    data.native_cell<std::uint8_t>(0x4346838Bu) = 1;
    data.native_cell<float>(0x4346A8A0u) = std::numeric_limits<float>::max();
    // These are initial scalar data, not defaults supplied by a feature
    // constructor. Their source consumers distinguish absent handles and
    // unseen command numbers from zero before their first dynamic capture.
    for (const auto cell : {0x43467114u, 0x43468488u, 0x4346D4B4u, 0x4346D4B8u, 0x4346D4BCu,
                            0x4346D648u, 0x4346D650u, 0x4346D674u, 0x4346DB1Cu})
        data.native_cell<std::int32_t>(cell) = -1;
    // The independent camera/ESP FOVs, aim-step interval and uncaptured duck
    // sentinel retain their exact native static bits until their real writers.
    data.native_cell<float>(0x43468E80u) = 90.0f;
    data.native_cell<float>(0x4346D548u) = 90.0f;
    data.native_cell<float>(0x43469254u) = 0.015625f;
    data.native_cell<float>(0x4346D654u) = std::numeric_limits<float>::max();
    data.native_cell<std::uint8_t>(0x4346A65Cu) = 1;
    data.native_cell<std::array<std::uint8_t, 3>>(0x434745E0u) = {2, 0, 1};
    //43369FAA/CA pass this immutable float directly to the native trigonometry
    // boundary. Preserve its recorded bits rather than infer a different angle.
    data.native_cell<float>(0x43472534u) = std::bit_cast<float>(0x7C8EFA34u);
    //43416B4E only compares the caller word with this pinned literal. Neither
    // the ordinary payload nor loader extra() writes it, and the working
    // dump retains it verbatim. It is never called/dereferenced. Preserve the
    // comparison bits; an inferred rebase would invent a different predicate.
    data.native_cell<std::uintptr_t>(0x43467DE8u) = 0x283A0BF0u;
    // Original static hashes consumed, in order, by the devirtualized first
    // startup callback. They become native service numbers only on its
    // matching loader-base branch.
    for (const auto& service : first_startup_service_slots())
        data.native_cell<std::uint32_t>(service.address) = service.initial_hash;

    // Static4346D678 starts at1; the first433EA7BC token read builds the
    // delimiter bitmap and clears it. The source parser shares these cells.
    data.native_cell<std::uint8_t>(0x4346D678u) = 1;
    data.native_cell<std::array<std::uint32_t, 8>>(0x4347C620u).fill(0);

    // All three native one-bucket headers initially use the same null bucket and
    // -1 terminator at4346935C. Store their actual source addresses rather than the
    // original absolute pointer value contained in the captured data image.
    auto& empty_buckets = data.native_cell<std::array<void*, 2>>(0x4346935Cu);
    empty_buckets = {nullptr, reinterpret_cast<void*>(~std::uintptr_t{})};
    data.native_cell<StartupTextBuckets>(0x4346D700u) = {
        0, reinterpret_cast<StartupTextBucketNode**>(empty_buckets.data()), 1, 0};
    data.native_cell<StartupPointerBuckets>(0x4346DA40u) = {
        0, reinterpret_cast<StartupPointerBucketNode**>(empty_buckets.data()), 1, 0};
    data.native_cell<animation::FixAnimationBackupNode**>(0x4346D6E4u) =
        reinterpret_cast<animation::FixAnimationBackupNode**>(empty_buckets.data());
    data.native_cell<std::uint32_t>(0x4346D6E8u) = 1;
    data.native_cell<std::uint32_t>(0x4346D6ECu) = 0;
    data.native_cell<animation::FixAnimationBackupGrowth>(0x4346D6F0u) = {1.0f, 2.0f, 0};
}
} //namespace skeet::engine
