#include "skeet/rage/group_storage.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/client/shutdown.hpp"
#include <stdexcept>
#include <utility>

namespace skeet::rage {
namespace {
struct BlobInitializer {
    std::uint32_t callback, finalizer;
};
constexpr std::array<BlobInitializer, 12> blobs{{{0x43492965, 0x4349298E},
                                                 {0x434929A8, 0x434929D1},
                                                 {0x434929EB, 0x43492A14},
                                                 {0x43492A2E, 0x43492A57},
                                                 {0x43492A71, 0x43492A9A},
                                                 {0x43492AB4, 0x43492ADD},
                                                 {0x43492AF7, 0x43492B20},
                                                 {0x43492B3A, 0x43492B63},
                                                 {0x43492B7D, 0x43492BA6},
                                                 {0x43492BC0, 0x43492BE9},
                                                 {0x43492C03, 0x43492C2C},
                                                 {0x43492C46, 0x43492C6F}}};
constexpr std::array<std::uint32_t, 12> enabled{0x43492C89, 0x43492CAB, 0x43492CCD, 0x43492CEF,
                                                0x43492D11, 0x43492D33, 0x43492D55, 0x43492D77,
                                                0x43492D99, 0x43492DBB, 0x43492DDD, 0x43492DFF};
GroupInitializerBindings* active_groups = nullptr;

template <std::size_t Index> void finalize_blob() {
    active_groups->groups.finalize_blob(Index);
}
template <std::size_t... Index> constexpr auto finalizer_entries(std::index_sequence<Index...>) {
    return std::array<void (*)(), sizeof...(Index)>{finalize_blob<Index>...};
}
constexpr auto finalizers = finalizer_entries(std::make_index_sequence<12>{});
template <std::size_t Index> void initialize_blob(void* raw) {
    auto& source = *static_cast<GroupInitializerBindings*>(raw);
    source.groups.initialize_blob(Index, source.configuration.descriptors);
    (void)source.callbacks.append(source.source_finalizer(blobs[Index].finalizer));
}
template <std::size_t Index> void initialize_enabled(void* raw) {
    auto& source = *static_cast<GroupInitializerBindings*>(raw);
    source.groups.initialize_enabled(Index, source.configuration.descriptors);
}
template <std::size_t... Index>
void bind(client::NativeStartupSources& sources, GroupInitializerBindings& context,
          std::index_sequence<Index...>) {
    (sources.bind_initializer({blobs[Index].callback, &context, initialize_blob<Index>}), ...);
    (sources.bind_initializer({enabled[Index], &context, initialize_enabled<Index>}), ...);
}
}
void (*GroupInitializerBindings::source_finalizer(std::uint32_t address))() {
    for (std::size_t index = 0; index < blobs.size(); ++index) {
        if (blobs[index].finalizer != address)
            continue;
        if (active_groups && active_groups != this)
            throw std::logic_error("RAGE group exit callbacks require one stable source binding");
        active_groups = this;
        return finalizers[index];
    }
    throw std::out_of_range("Finalizer is not a RAGE weapon-group blob callback");
}
void bind_native_group_initializers(client::NativeStartupSources& sources,
                                    GroupInitializerBindings& context) {
    if (&context.groups.image() != &sources.data())
        throw std::invalid_argument(
            "RAGE groups require the startup scheduler's canonical data owner");
    if (active_groups && active_groups != &context)
        throw std::logic_error("RAGE group exit callbacks require one stable source binding");
    active_groups = &context;
    bind(sources, context, std::make_index_sequence<12>{});
}
}
