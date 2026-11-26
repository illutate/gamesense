#include "skeet/esp/grenade_live_bindings.hpp"
#include "skeet/engine/offset_storage.hpp"

namespace skeet::esp {
GrenadeLiveOffsets::GrenadeLiveOffsets(const engine::NativeOffsetGlobals& g)
    : armor(g, 0x43467908, 0x434682e4, 0x47a6f50c, 0),
      view_offset(g, 0x43468a34, 0x43468c00, 0xc246315e, 0),
      life_state(g, 0x43468e78, 0x434684e4, 0x9d920e35, 0),
      movement_type(g, 0x43468bd4, 0x4346a678, 0x7501cb91, 0),
      observer_mode(g, 0x4346a6cc, 0x43468ef8, 0x2f4e97c4, 0),
      observer_target(g, 0x43468f84, 0x43467e64, 0x3b704b41, 0),
      view_angles(g, 0x43468ea0, 0x4346a74c, 0xe23a9be1, 0),
      alternate_eye(g, 0x43468bf8, 0x43467b8c, 0xd070684c, 0),
      player_flags(g, 0x434688d0, 0x43468c24, 0xe2fbe695, 0),
      weapon_definition(g, 0x4346908c, 0x43468ba0, 0x5c9f0fbb, 0),
      weapon_throw_time(g, 0x43467aa4, 0x43467bf4, 0x3b93667b, 0),
      weapon_strength(g, 0x43467d1c, 0x43468e34, 0x1b9654d9, 0),
      weapon_state(g, 0x4346828c, 0x43468d2c, 0x9e2078a1, 0),
      recoil(g, 0x434682b8, 0x43467d90, 0x1bae1168, 0),
      game_player_override(g, 0x43467910, 0x43467c08, 0xd720aea6, 0),
      teammate_damage_disabled(g, 0x43468d1c, 0x43468500, 0xf57beaa9, 0),
      rejection_cde290d2(g, 0x4346a5dc, 0x43468430, 0xcde290d2, 0),
      rejection_af649dac(g, 0x43467dd0, 0x4346a610, 0xaf649dac, 0),
      duck_amount(g, 0x434679f8, 0x434675f8, 0x3309c0cf, 0),
      active_weapon(g, 0x43467a74, 0x434678c0, 0xcd812654, 0) {}
GrenadeLiveOffsets capture_grenade_live_offsets(const engine::NativeOffsetGlobals& globals) {
    return GrenadeLiveOffsets(globals);
}
} // namespace skeet::esp
