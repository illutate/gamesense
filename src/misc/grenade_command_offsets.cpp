#include "skeet/misc/grenade_command.hpp"
#include "skeet/engine/offset_storage.hpp"

namespace skeet::misc {
GrenadeCommandOffsets capture_grenade_command_offsets(const engine::NativeOffsetGlobals& globals) {
    const auto bind = [&](std::uint32_t pointer, std::uint32_t key, std::uint32_t salt) {
        return engine::DecodedOffset(globals, pointer, key, salt, 0);
    };
    return {
        bind(0x43467A74u, 0x434678C0u, 0xCD812654u), //4337DB03 active weapon
        bind(0x4346908Cu, 0x43468BA0u, 0x5C9F0FBBu), //43383176 definition
        bind(0x434670ECu, 0x43467B1Cu, 0xE1DF7E9Bu), //433831AF pin
        bind(0x43467AA4u, 0x43467BF4u, 0x3B93667Bu), //433831EE throw time
        bind(0x43467D1Cu, 0x43468E34u, 0x1B9654D9u), //4338ED30 strength
        bind(0x43468E78u, 0x434684E4u, 0x9D920E35u), //4337DAD7 life state
        bind(0x434688D0u, 0x43468C24u, 0xE2FBE695u), //433B7FAE flags
        bind(0x4346A8E4u, 0x434688CCu, 0x36375C23u), //4339C63C velocity
        bind(0x434693D8u, 0x43468BC4u, 0x3D10B94Cu), //4337DC2D tick base
        bind(0x434685CCu, 0x43468A84u, 0x3EB71843u), //4336964B speed limit
        bind(0x434682B8u, 0x43467D90u, 0x1BAE1168u), //433AE483 recoil
        bind(0x43468BF8u, 0x43467B8Cu, 0xD070684Cu) //4338EE42 alternate eye
    };
}
} // namespace skeet::misc
