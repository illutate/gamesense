#include "skeet/client/runtime_owner_initialization.hpp"
#include "skeet/client/native_startup.hpp"
#include <array>

namespace skeet::client {
namespace {
// Devirtualized factory dispatch at 0x434BE957. Every primary vtable in the
// working 57-owner registry matches a reachable store in its corresponding
// constructor. The retained addresses identify recovered source bindings.
constexpr std::array<std::uint32_t, 57> owner_initializers{
    {0x434903ad, 0x4347e356, 0x4348066a, 0x43481f6a, 0x4347e13e, 0x43486705, 0x43481dc0, 0x4347dd87,
     0x4347ff2f, 0x4349081e, 0x43484a2b, 0x4347da60, 0x4348e608, 0x43481f44, 0x4348da54, 0x43481eda,
     0x434856d6, 0x4347da84, 0x4348401c, 0x4348da25, 0x4347febc, 0x4348e51f, 0x43483ff8, 0x43490389,
     0x43485ccc, 0x43484f66, 0x43485c3a, 0x43484f93, 0x4347d8d0, 0x43486bf7, 0x4349095e, 0x4348e3f9,
     0x43481eab, 0x4347e81a, 0x43484ac4, 0x43480644, 0x4348574e, 0x4348da78, 0x4348e09f, 0x43485be3,
     0x43481d50, 0x4348038f, 0x4348da01, 0x43490411, 0x43484ed9, 0x4347ea07, 0x4348e6aa, 0x4347d94f,
     0x4347f6db, 0x4347e2e3, 0x4347da14, 0x43481e08, 0x43481de4, 0x43484954, 0x43484e4e, 0x4347f902,
     0x43490982}};
}

std::span<const std::uint32_t, 57> runtime_owner_initializers() noexcept {
    return owner_initializers;
}

void preflight_runtime_owner_initialization(const NativeStartupSources& sources) {
    // Validate the complete source binding set before constructing any owner.
    // Missing implementations remain errors, never calls into retained code.
    for (std::uint32_t i = 0; i != owner_initializers.size(); ++i)
        sources.require_initializer(0x1148 + i * 4, owner_initializers[i]);
}
void initialize_runtime_owners(const NativeStartupSources& sources) {
    preflight_runtime_owner_initialization(sources);
    for (std::uint32_t i = 0; i != owner_initializers.size(); ++i)
        sources.initialize(0x1148 + i * 4, owner_initializers[i]);
}
}
