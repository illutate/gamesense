#include "skeet/esp/map_entity_parser.hpp"
#include "skeet/esp/frame.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstdlib>
#include <cstring>

namespace skeet::esp {
MapEntityParserEndpoint* register_map_entity_parser(engine::OwnerRegistryBinding registry,
                                                    MapEntityParserEndpoint* endpoint) {
    registry.append(endpoint);
    return endpoint;
}
void dispatch_map_scalar(const engine::OwnerRegistry& registry, float value) {
    const auto callbacks = registry.entries();
    for (auto* object : callbacks) {
        auto* endpoint = static_cast<MapEntityParserEndpoint*>(object);
        endpoint->vtable->scalar(object, value);
    }
}
void dispatch_map_entity_text(const engine::OwnerRegistry& registry, void* engine) {
    const auto* value = engine::invoke<const char*>(engine, 99);
    if (!value)
        return;
    const auto callbacks = registry.entries();
    for (auto* object : callbacks) {
        auto* endpoint = static_cast<MapEntityParserEndpoint*>(object);
        endpoint->vtable->text(object, value);
    }
}
EspMapEntityParser::EspMapEntityParser(engine::OwnerRegistryBinding registry, EspFrameOwner& owner,
                                       const std::string& prefix)
    : owner_(owner), prefix_(prefix) {
    register_map_entity_parser(registry, this);
    vtable = &entries_;
}
#if defined(_M_IX86) || defined(__i386__)
void __vectorcall EspMapEntityParser::scalar(void*, float) {} //433CCE8E
void __fastcall EspMapEntityParser::text(void* object, const char* value) {
#else
void EspMapEntityParser::scalar(void*, float) {}
void EspMapEntityParser::text(void* object, const char* value) {
#endif
    auto& self = *static_cast<EspMapEntityParser*>(static_cast<MapEntityParserEndpoint*>(object));
    const auto* found = std::strstr(value, self.prefix_.c_str());
    if (!found)
        return;
    const auto parsed = std::uint32_t(std::strtoul(found + 14, nullptr, 10));
    if (!parsed || parsed == 0xffffffffu)
        return;
    const auto signed_value = std::bit_cast<std::int32_t>(parsed);
    self.owner_.bomb_peak_damage =
        float(double(signed_value) + (parsed >> 31 ? 4294967296.0 : 0.0));
}
} //namespace skeet::esp
