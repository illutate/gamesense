#include "skeet/client/exit_registration.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/client/shutdown.hpp"
#include "skeet/client/static_cleanup.hpp"
#include <array>
#include <cstdint>

namespace skeet::client {
namespace {
struct ExitRegistration {
    std::uint32_t initializer, finalizer;
    void (*invoke)(void*);
};
template <std::uint32_t Finalizer> void append_exit_callback(void* owner) {
    auto& context = *static_cast<ExitRegistrationContext*>(owner);
    // source_callback selects an actual recovered C++ finalizer. The native
    // identifier is not converted into a host function pointer.
    (void)context.callbacks.append(context.cleanup.source_callback(Finalizer));
}

void initialize_spread_and_append(void* owner) { //4340B25A
    auto& context = *static_cast<ExitRegistrationContext*>(owner);
    context.cleanup.initialize_spread_storage(); //complete 43402E73
    append_exit_callback<0x4337D954u>(owner);
}
}

void bind_exit_initializers(NativeStartupSources& sources, ExitRegistrationContext& context) {
    // Ordinary wrappers load ECX with the finalizer, then tail-jump into the
    // existing 433837F5 append primitive. 4340B25A first initializes the two
    // shared spread-table allocations. Return values are unused by startup.
    const std::array<ExitRegistration, 19> entries{{
        {0x4339E627u, 0x433EB717u, append_exit_callback<0x433EB717u>}, //008
        {0x4341738Bu, 0x433E4A38u, append_exit_callback<0x433E4A38u>}, //00C
        {0x433B22B6u, 0x43349ECBu, append_exit_callback<0x43349ECBu>}, //010
        {0x4337DA78u, 0x43347D44u, append_exit_callback<0x43347D44u>}, //014
        {0x4340BF0Bu, 0x433F515Bu, append_exit_callback<0x433F515Bu>}, //018
        {0x433F9001u, 0x433E6194u, append_exit_callback<0x433E6194u>}, //01C
        {0x433B7724u, 0x433E4C98u, append_exit_callback<0x433E4C98u>}, //020
        {0x4337F0AFu, 0x433F4A1Du, append_exit_callback<0x433F4A1Du>}, //024
        {0x433C084Fu, 0x433F0746u, append_exit_callback<0x433F0746u>}, //028
        {0x4339FA2Bu, 0x433C9DB2u, append_exit_callback<0x433C9DB2u>}, //02C
        {0x433B261Du, 0x433F4C95u, append_exit_callback<0x433F4C95u>}, //030
        {0x4340B872u, 0x433D1E9Fu, append_exit_callback<0x433D1E9Fu>}, //034
        {0x433881D7u, 0x433C9173u, append_exit_callback<0x433C9173u>}, //038
        {0x433AB3C9u, 0x43336AD4u, append_exit_callback<0x43336AD4u>}, //03C
        {0x433A165Du, 0x43341B3Fu, append_exit_callback<0x43341B3Fu>}, //040
        {0x4332798Cu, 0x433B8C06u, append_exit_callback<0x433B8C06u>}, //044
        {0x4340B25Au, 0x4337D954u, initialize_spread_and_append}, //048
        {0x433E1038u, 0x43329A5Bu, append_exit_callback<0x43329A5Bu>}, //04C
        {0x433B4B44u, 0x4332EFADu, append_exit_callback<0x4332EFADu>}, //050
    }};
    for (const auto& entry : entries)
        if (context.cleanup.has_source_callback(entry.finalizer))
            sources.bind_initializer({entry.initializer, &context, entry.invoke});
}
}
