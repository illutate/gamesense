#include "skeet/esp/grenade_engine.hpp"

namespace skeet::esp {
// Native43337BC0/43341683/433BB853 operands. Property use sites perform the
// actual decode through these proxies; construction reads no offset words.
GrenadeEngineOffsets::GrenadeEngineOffsets(const engine::NativeOffsetGlobals& g)
    : origin{g, 0x434670CC, 0x43468BCC, 0xA9EE6062, 0},
      velocity{g, 0x4346A8E4, 0x434688CC, 0x36375C23, 0},
      initial_velocity{g, 0x434683E8, 0x434693C0, 0x434E9E95, 0},
      simulation_time{g, 0x43469198, 0x4346A538, 0xD5FD2E2C, 0},
      effects{g, 0x43468B50, 0x43467B48, 0x38CA5228, 0},
      explosion_effect_tick{g, 0x43467210, 0x43467630, 0x01E80986, 0},
      thrower_handle{g, 0x43468C18, 0x4346A888, 0xDC743513, 0},
      owner_handle{g, 0x43467FCC, 0x43467938, 0x8D1BD8D0, 0},
      damage{g, 0x434674EC, 0x4346894C, 0x727317C4, 0},
      collision_group{g, 0x434683C8, 0x4346758C, 0xE4E2D1A7, 0},
      projectile_flags{g, 0x43467488, 0x43467DEC, 0x7418868F, 0},
      inferno_start_tick{g, 0x4346A854, 0x434678B4, 0x42088943, 0},
      team{g, 0x434688B0, 0x4346A518, 0x8B5586F6, 0},
      survival_team{g, 0x434678AC, 0x43467B7C, 0x430C8993, 0},
      cached_team_valid{g, 0x43467CB8, 0x4346A814, 0x9571D5F5, 0},
      cached_team{g, 0x434684F0, 0x4346A558, 0xC1C49A34, 0},
      physics_object{g, 0x4346720C, 0x43468298, 0xA249061A, 0xFFFFFFF4},
      fire_count{g, 0x43468E28, 0x4346893C, 0x3584E3A5, 0},
      fire_active{g, 0x43467D4C, 0x4346711C, 0xFC1EF3A1, 0},
      fire_x{g, 0x43468A58, 0x43467574, 0x24A16FF6, 0},
      fire_y{g, 0x43469058, 0x4346892C, 0x94E8C7F7, 0},
      fire_z{g, 0x434671E8, 0x43468DDC, 0xD7600AAC, 0},
      smoke_active{g, 0x4346A530, 0x43467564, 0x58AE2423, 0},
      smoke_start_tick{g, 0x43467AC0, 0x43468E60, 0xF267E956, 0} {}

GrenadeEngineOffsets capture_grenade_engine_offsets(const engine::NativeOffsetGlobals& globals) {
    return GrenadeEngineOffsets(globals);
}
} //namespace skeet::esp
