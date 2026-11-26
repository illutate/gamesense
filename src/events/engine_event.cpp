#include "skeet/events/engine_event.hpp"
#include "skeet/engine/interface_call.hpp"

namespace skeet::events {
namespace {
class QueryVisitor final : public EngineEventVisitor {
public:
    QueryVisitor(std::uint32_t hash, FieldType type, FieldValue value) : query{hash, type, value} {}
    FieldQuery query;
    bool pointer(const char*, void*) override {
        return true;
    } //433F1111
    bool string(const char* key, const char* value) override {
        return query.string(key, value);
    }
    bool floating(const char* key, float value) override {
        return query.floating(key, value);
    }
    bool integer(const char* key, std::int32_t value) override {
        return query.integer(key, static_cast<std::uint32_t>(value));
    }
    bool wide_integer(const char* key, std::uint64_t value) override {
        return query.wide_integer(key, value); //433ADABB
    }
    bool opaque_word(const char*, std::uint32_t) override {
        return true;
    } //433F1111
    bool boolean(const char* key, bool value) override {
        return query.boolean(key, value);
    }
};
}
const char* EngineGameEventReader::name() const {
    return engine::invoke<const char*>(event_, 1);
}
bool EngineGameEventReader::read(std::uint32_t hash, FieldType type, FieldValue& value) {
    QueryVisitor visitor(hash, type, value);
    const auto result = engine::invoke<bool, EngineEventVisitor*>(event_, 19, &visitor);
    value = visitor.query.value;
    return result;
}
void EngineGameEventReader::set_string(const char* key, const char* value) {
    engine::invoke<void, const char*, const char*>(event_, 16, key, value);
}
void EngineGameEventReader::set_boolean(const char* key, bool value) {
    engine::invoke<void, const char*, bool>(event_, 13, key, value);
}
std::int32_t EngineEventPlayers::player_for_user_id(std::uint32_t user_id) {
    return engine::invoke<std::int32_t>(engine_, 9, user_id);
}
std::int32_t EngineEventPlayers::local_player_index() {
    return engine::invoke<std::int32_t>(static_cast<std::byte*>(local_) + 8, 10);
}
bool EngineEventPlayers::player_info(std::int32_t index, std::array<std::byte, 0x158>& output) {
    return engine::invoke<bool>(engine_, 8, index, output.data());
}
} // namespace skeet::events
