#include "skeet/menu/scripts_menu.hpp"
#include "skeet/script/manager.hpp"
#include "skeet/config_storage/presets.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace skeet::ui {

void ScriptsMenu::initialize_startup(const char* command_line,
                                     std::span<const std::uint32_t, 128> hashes) {
    if (command_line && std::strstr(command_line, "-nolua")) { // 0x433DF55B
        static_cast<Checkbox&>(*controls()[0].widget).set_value(false);
        return;
    }
    // 0x4341AABA has no enabled-checkbox gate and is not the activation path.
    if (!hashes[0] && manager_.builtins_.empty())
        return;
    constexpr std::uint32_t mask = 0x92CA5DF1;
    if (const auto* settings = manager_.settings_.get()) {
        const auto& saved = settings->script_hashes;
        for (auto& module : manager_.builtins_)
            if (module.enabled &&
                std::find(saved.begin(), saved.end(),
                          script::script_name_hash(std::string_view(module.name)) ^ mask) !=
                    saved.end())
                module.enabled = false;
    }
    refresh();
    if (manager_.records_.empty())
        return;
    auto persist = [&](std::uint32_t excluded) {
        if (!manager_.persistence_)
            throw std::logic_error("Script startup persistence is not bound");
        (void)manager_.persistence_->persist(0, excluded);
    };
    // Keep the input as a live span: persistence may rewrite the same native
    // owner array while the startup loop is walking it.
    for (std::size_t i = 0; i < hashes.size() && hashes[i]; ++i) {
        const auto hash = hashes[i];
        auto record = std::find_if(manager_.records_.begin(), manager_.records_.end(),
                                   [&](const auto& entry) {
                                       return (script::script_name_hash(entry.name) ^ mask) == hash;
                                   });
        if (record == manager_.records_.end())
            continue;
        persist(hash);
        if ((record->name.empty() || record->name.front() != u'*') &&
            manager_.runtime_.ensure_state()) {
            const auto name = std::u16string_view(record->name).substr(0, record->name.find(u'\0'));
            if (manager_.load_module(config_name_to_utf8(name)) == 0) {
                manager_.loaded_files_.insert(std::u16string(name));
                persist(0);
                continue;
            }
        }
        if (!record->name.empty())
            record->startup = 0;
    }
    (void)manager_.runtime_.ensure_state();
    for (auto& option : list_->options) {
        const auto marker = std::bit_cast<std::uint32_t>(option.value);
        const auto index = marker & 0x7FFFFFFFu;
        if (index < manager_.records_.size() && manager_.loaded(manager_.records_[index].name))
            option.value = std::bit_cast<std::int32_t>(marker | 0x80000000u);
    }
    manager_.current_ui_config_ = manager_.save_ui_values();
    manager_.shutdown_.current_config = manager_.current_ui_config_;
}

} // namespace skeet::ui
