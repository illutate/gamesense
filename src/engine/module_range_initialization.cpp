#include "skeet/engine/module_range_initialization.hpp"
#include <stdexcept>

namespace skeet::engine {
namespace {
constexpr std::array<ModuleRangeRowCells, 4> cells{{{0x434675D8u, 0x4346A49Cu, 0},
                                                    {0x4346837Cu, 0x43467570u, 0},
                                                    {0x4346A58Cu, 0, 0x43468C20u},
                                                    {0x43468234u, 0, 0x43468C98u}}};
template <class Pointer> bool skipped(Pointer pointer) noexcept {
    return reinterpret_cast<std::uintptr_t>(pointer) == 1;
}
}
std::span<const ModuleRangeRowCells, 4> module_range_row_cells() noexcept {
    return cells;
}

ModuleRangeInitializationBindings native_module_range_bindings(NativeDataImage& data) {
    ModuleRangeInitializationBindings result{{}, &data.native_cell<const std::byte*>(0x4346A55Cu)};
    for (std::size_t index = 0; index < cells.size(); ++index) {
        const auto& cell = cells[index];
        result.rows[index] = {
            cell.initialize ? &data.native_cell<ModuleRangeInitialize>(cell.initialize) : nullptr,
            cell.get ? &data.native_cell<ModuleRangeGet>(cell.get) : nullptr,
            cell.indirect ? &data.native_cell<ModuleAddressRange**>(cell.indirect) : nullptr};
    }
    return result;
}

void initialize_module_ranges(const ModuleRangeInitializationBindings& bindings) {
    for (const auto& row : bindings.rows) {
        if (!row.initialize)
            continue;
        const auto initialize = *row.initialize;
        if (skipped(initialize))
            continue;
        if (!initialize)
            throw std::invalid_argument("Active module range row requires its genuine initializer");
        initialize();

        ModuleAddressRange* range;
        if (row.get) {
            const auto get = *row.get;
            if (skipped(get))
                continue;
            if (!get)
                throw std::invalid_argument("Active module range row requires its genuine getter");
            range = get();
        } else {
            if (!row.indirect)
                continue;
            auto* const holder = *row.indirect;
            if (skipped(holder))
                continue;
            if (!holder)
                throw std::invalid_argument(
                    "Active module range row requires its genuine object holder");
            range = *holder;
        }
        if (!range || range->low == 0x10000u)
            continue;
        if (!bindings.client_module)
            throw std::invalid_argument(
                "Module range initialization requires the current client module cell");
        const auto low =
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(*bindings.client_module)) +
            0x1000u;
        if (range->low != low)
            continue;
        range->low = 0x10000u;
        range->high = 0xFFF00000u;
    }
}
} // namespace skeet::engine
