#include "skeet/engine/build_startup_convars.hpp"
#include "skeet/client/startup.hpp"
#include "skeet/engine/interface_call.hpp"
#include <array>
#include <sstream>
#include <stdexcept>
#include <string>

namespace skeet::engine {
namespace {
// Ordered fixed-build data, not captured heap pointers or a VM getter.
// Every exact name/hash and decoded object was checked against the working
// ICvar registry. Each key is the pinned static word XOR35F76E47, matching
// the native producer and independent existing consumer decoders.
constexpr std::array<BuildStartupConVarSlot, 105> slots{{
    {0xB70u, 0x4346794Cu, 0x0D26DB34u, 0xC429E1BFu, "weapon_recoil_scale"},
    {0xB78u, 0x434683ACu, 0x0E327003u, 0x0AF7B93Cu, "cl_interpolate"},
    {0xB80u, 0x434688A8u, 0x1347018Fu, 0xCDF25CE4u, "cl_extrapolate"},
    {0xB88u, 0x43467A5Cu, 0x1FF3E017u, 0xBA5BE8A8u, "name"},
    {0xB90u, 0x43468528u, 0xDD9E7FF3u, 0xF75367FEu, "sv_max_usercmd_future_ticks"},
    {0xB98u, 0x4346747Cu, 0x77619B7Du, 0x03B4819Cu, "cl_updaterate"},
    {0xBA0u, 0x434671ACu, 0x072F875Eu, 0xD8BFAEFFu, "cl_cmdrate"},
    {0xBA8u, 0x43468BA8u, 0x8656D6BCu, 0x30911EABu, "cl_smooth"},
    {0xBB0u, 0x4346A748u, 0x9263E2C4u, 0x7D9DC86Fu, "cl_interp"},
    {0xBB8u, 0x4346A330u, 0x3F0981F4u, 0xBE64D223u, "cl_interp_ratio"},
    {0xBC0u, 0x43467A54u, 0xCA292AF8u, 0xB882FC15u, "sv_gravity"},
    {0xBC8u, 0x43468C2Cu, 0x85E0EDEBu, 0x3FE3AE0Cu, "sv_maxvelocity"},
    {0xBD0u, 0x4346748Cu, 0x8F6F36C9u, 0x69283E00u, "sv_maxspeed"},
    {0xBD8u, 0x43467E28u, 0x72D57DC4u, 0xE80C95F9u, "sv_accelerate_use_weapon_speed"},
    {0xBE0u, 0x43467048u, 0x53E4D214u, 0x983081BFu, "molotov_throw_detonate_time"},
    {0xBE8u, 0x43467088u, 0x882577C9u, 0x8FD87158u, "weapon_molotov_maxdetonateslope"},
    {0xBF0u, 0x43469008u, 0x3EC4CEFDu, 0xAAA77B8Au, "sv_footsteps"},
    {0xBF8u, 0x43468F44u, 0x64E9041Cu, 0x684EE759u, "mat_postprocess_enable"},
    {0xC00u, 0x43468F98u, 0xC875E387u, 0xADB120F0u, "cl_csm_shadows"},
    {0xC08u, 0x43467950u, 0xC18F6179u, 0x66BFCA30u, "m_yaw"},
    {0xC10u, 0x43467988u, 0x80666B20u, 0x01EAAD2Fu, "m_pitch"},
    {0xC18u, 0x43467BB4u, 0x89CFF0F4u, 0x0998CE5Du, "weapon_recoil_decay2_exp"},
    {0xC20u, 0x43468D8Cu, 0x959D7974u, 0x9709DCB1u, "weapon_recoil_decay2_lin"},
    {0xC28u, 0x4346A480u, 0x02E9B330u, 0x195B42E3u, "weapon_recoil_vel_decay"},
    {0xC30u, 0x43467104u, 0x7CC02A76u, 0xE974CD65u, "weapon_recoil_decay_coefficient"},
    {0xC38u, 0x43468C44u, 0xB3A95638u, 0x1D07014Du, "cl_modelfastpath"},
    {0xC40u, 0x43467124u, 0x3A884E45u, 0xC5C9925Eu, "sv_stopspeed"},
    {0xC48u, 0x4346A820u, 0x3F40AE2Eu, 0xEAAEC541u, "sv_friction"},
    {0xC50u, 0x43467990u, 0xE8B4AAECu, 0xAF1F2C93u, "sv_jump_impulse"},
    {0xC58u, 0x4346A568u, 0x5D844595u, 0x9190E2E6u, "mp_teammates_are_enemies"},
    {0xC60u, 0x43468CACu, 0x3D9AD583u, 0xD8ED8A94u, "mp_solid_teammates"},
    {0xC68u, 0x43467FA0u, 0xACE01EB4u, 0x4B52A9BFu, "sv_enablebunnyhopping"},
    {0xC70u, 0x4346A4E4u, 0xF946C584u, 0x14F449DFu, "sv_autobunnyhopping"},
    {0xC78u, 0x434674E4u, 0xBD9F590Bu, 0xDCEAEAC8u, "sv_airaccelerate"},
    {0xC80u, 0x434683BCu, 0xE1D49A6Bu, 0xE853BE80u, "r_jiggle_bones"},
    {0xC88u, 0x43467080u, 0x9425368Fu, 0x62444FD4u, "r_player_visibility_mode"},
    {0xC90u, 0x434693BCu, 0x89AD8BCEu, 0x2729DCC3u, "r_eyegloss"},
    {0xC98u, 0x4346A780u, 0x849C73B8u, 0x359CF7E3u, "cl_lagcomp_errorcheck"},
    {0xCA0u, 0x43467A78u, 0xF748DD16u, 0x3AE06513u, "cl_lagcompensation"},
    {0xCA8u, 0x43467968u, 0x84060A5Du, 0x1632541Cu, "sv_accelerate"},
    {0xCB0u, 0x434670D0u, 0xFF4A4615u, 0x1320FFAAu, "sv_infinite_ammo"},
    {0xCB8u, 0x43468254u, 0xDF660FEEu, 0xCF54AEB3u, "sv_skirmish_id"},
    {0xCC0u, 0x43467FBCu, 0x3C49C196u, 0x96BEB183u, "rate"},
    {0xCC8u, 0x4346A39Cu, 0x746AD238u, 0x738FE7C5u, "sv_skyname"},
    {0xCD0u, 0x43468DFCu, 0x7EBAA819u, 0x18D4EC2Eu, "cam_snapto"},
    {0xCD8u, 0x43467D8Cu, 0x7C368E82u, 0x41632A61u, "cam_collision"},
    {0xCE0u, 0x43467AA0u, 0x579E58FBu, 0x8985EFCEu, "cam_idealdist"},
    {0xCE8u, 0x4346A3B0u, 0x3C7F8DBCu, 0xE66A127Fu, "developer"},
    {0xCF0u, 0x4346A694u, 0x8955D95Au, 0xD9328889u, "mp_free_armor"},
    {0xCF8u, 0x434685A0u, 0x10DA1A89u, 0xDE58CE34u, "sv_min_jump_landing_sound"},
    {0xD00u, 0x43467638u, 0x17F42093u, 0xDEAE3FAEu, "sv_stepsize"},
    {0xD08u, 0x43468E94u, 0xDCB5D665u, 0x2DC3CC20u, "bot_mimic"},
    {0xD10u, 0x43468EA4u, 0xFAF01779u, 0xFCD2DA7Au, "weapon_accuracy_nospread"},
    {0xD18u, 0x4346A478u, 0x7EA31282u, 0x3D5F961Fu, "mp_c4timer"},
    {0xD20u, 0x43468520u, 0x20E2FB4Fu, 0x394BFF56u, "weapon_accuracy_shotgun_spread_patterns"},
    {0xD28u, 0x43469238u, 0x4F986B7Cu, 0xF7E8922Fu, "cl_predictweapons"},
    {0xD30u, 0x4346A7DCu, 0x8F4F6833u, 0xB2005B9Cu, "cl_righthand"},
    {0xD38u, 0x43468C30u, 0xB4E39538u, 0x876ADF97u, "mp_flinch_punch_scale"},
    {0xD40u, 0x43469068u, 0xCDAAD240u, 0x7B3564BBu, "sv_deadtalk"},
    {0xD48u, 0x43468D88u, 0x19F7F06Bu, 0x584C3C72u, "sv_full_alltalk"},
    {0xD50u, 0x4346A850u, 0xA14C43A9u, 0x478720C2u, "cl_predict"},
    {0xD58u, 0x4346896Cu, 0xE3376ED8u, 0xA7B82DE9u, "joy_autosprint"},
    {0xD60u, 0x43467D7Cu, 0xD982577Fu, 0x4A285142u, "sv_timebetweenducks"},
    {0xD68u, 0x43467A8Cu, 0xA5D1AF91u, 0xAC9299CCu, "game_type"},
    {0xD70u, 0x4346A42Cu, 0x400F5DEAu, 0x672DFE69u, "game_mode"},
    {0xD78u, 0x4346A37Cu, 0xDFD900D9u, 0x38DF1D5Eu, "r_skybox"},
    {0xD80u, 0x4346925Cu, 0x8B3A7803u, 0x89E7CF90u, "r_drawskybox"},
    {0xD88u, 0x434693A8u, 0x33B5FAE3u, 0x27F664A6u, "r_3dsky"},
    {0xD90u, 0x43468A94u, 0xABBC3D6Du, 0x24CE2F36u, "gl_clear"},
    {0xD98u, 0x43467130u, 0x726DD366u, 0x99854835u, "net_maxroutable"},
    {0xDA0u, 0x43467A48u, 0x80F57EB3u, 0x1EEEF408u, "zoom_sensitivity_ratio_mouse"},
    {0xDA8u, 0x4346A448u, 0x5273AB65u, 0xBB72C450u, "cl_clock_correction"},
    {0xDB0u, 0x4346859Cu, 0xE008AEDEu, 0x8510F1A9u, "tv_enable"},
    {0xDB8u, 0x434674DCu, 0x542EE5CBu, 0xE3F1DF58u, "tv_enable1"},
    {0xDC0u, 0x434671B0u, 0xDED404CCu, 0x9628DBC9u, "host_timescale"},
    {0xDC8u, 0x43467C3Cu, 0xB239EC33u, 0x211963A6u, "mp_friendlyfire"},
    {0xDD0u, 0x4346A88Cu, 0xD39FDE1Du, 0xD9D937D8u, "sv_staminamax"},
    {0xDD8u, 0x43468F4Cu, 0x575B8D6Bu, 0xB0CF25E8u, "sv_staminalandcost"},
    {0xDE0u, 0x43468FB4u, 0x3F8F7262u, 0xC9B16EBBu, "sv_staminajumpcost"},
    {0xDE8u, 0x43467E4Cu, 0x6BAE0E86u, 0xFC23F1E7u, "sv_staminarecoveryrate"},
    {0xDF0u, 0x434685DCu, 0x288CA67Au, 0x1A59159Fu, "occlusion_test_margins"},
    {0xDF8u, 0x434679D0u, 0xFF62558Au, 0x3F95B88Du, "occlusion_test_camera_margins"},
    {0xE00u, 0x43468338u, 0x10E146F2u, 0x24D1CE57u, "occlusion_test_jump_margin"},
    {0xE08u, 0x434671CCu, 0x402E015Cu, 0xEDA30A71u, "occlusion_test_shadow_length"},
    {0xE10u, 0x43468A78u, 0x1AEFDFFCu, 0xA6BBEFA3u, "cl_clock_correction_adjustment_min_offset"},
    {0xE18u, 0x4346A6FCu, 0x7A564282u, 0x856034BDu, "cl_clock_correction_adjustment_max_offset"},
    {0xE20u, 0x43467588u, 0xB7A90EBDu, 0xBEF53DACu, "cl_clock_correction_adjustment_max_amount"},
    {0xE28u, 0x434670F8u, 0x6281AD55u, 0x00329FD4u, "net_fakelag"},
    {0xE30u, 0x4346A67Cu, 0x61F7DF53u, 0x3155EA46u, "cl_ragdoll_physics_enable"},
    {0xE38u, 0x4346A5B4u, 0x322214A2u, 0x7CBE81A1u, "cl_ragdoll_workaround_threshold"},
    {0xE40u, 0x434671ECu, 0xD7F1E079u, 0x3C0C5F1Cu, "ff_damage_grenade_hit"},
    {0xE48u, 0x4346A7ECu, 0xFBDF805Du, 0x6B6A9C24u, "ff_damage_reduction_grenade"},
    {0xE50u, 0x434685C8u, 0xC85EB685u, 0xD5F0540Cu, "ff_damage_reduction_other"},
    {0xE58u, 0x4346A4FCu, 0xA48831A2u, 0x02081323u, "ff_damage_reduction_grenade_self"},
    {0xE60u, 0x43468C90u, 0x42215707u, 0x98D588D4u, "ff_damage_bullet_penetration"},
    {0xE68u, 0x43467084u, 0x10C2C88Au, 0x9C2756F5u, "ff_damage_reduction_bullets"},
    {0xE70u, 0x434670E8u, 0xDE1DAA9Cu, 0x308C7E6Bu, "mp_buy_anywhere"},
    {0xE78u, 0x43468E24u, 0x0DEB6867u, 0x41FA9A70u, "cl_rebuy"},
    {0xE80u, 0x43468510u, 0xA1215558u, 0x3273B755u, "cl_mouseenable"},
    {0xE88u, 0x43468EA8u, 0x4B0F60A9u, 0xAC34F836u, "sv_talk_enemy_living"},
    {0xE90u, 0x43467A4Cu, 0x86AB32B6u, 0xFA2DD713u, "sv_talk_enemy_dead"},
    {0xE98u, 0x4346A648u, 0x494D4282u, 0x66E946DBu, "singlestep"},
    {0xEA0u, 0x43467128u, 0x619A6A03u, 0xE78AB46Au, "cl_clanid"},
    {0xEA8u, 0x43469028u, 0x7847F897u, 0x0EFD12C6u, "mp_damage_headshot_only"},
    {0xEB0u, 0x4346A534u, 0x2153ADA1u, 0xB739EB42u, "viewmodel_recoil"},
}};
class EngineBuildConVarProvider final : public BuildStartupConVarProvider {
public:
    void* find_variable(void* cvar, const char* name) override {
        return invoke<void*>(cvar, 15, name);
    }
    bool is_command(void* candidate) override {
        return invoke<bool>(candidate, 1);
    }
};
std::string failure_message(std::size_t index, const BuildStartupConVarSlot& slot,
                            BuildStartupConVarFailureReason reason) {
    std::ostringstream message;
    message << "Build ConVar " << index << " '" << slot.name << "' (hash0x" << std::hex
            << slot.name_hash << ") ";
    if (reason == BuildStartupConVarFailureReason::missing_candidate)
        message << "is unavailable";
    else
        message << "resolves to a command";
    return message.str();
}
}
BuildStartupConVarFailure::BuildStartupConVarFailure(std::size_t failed_index,
                                                     const BuildStartupConVarSlot& slot,
                                                     BuildStartupConVarFailureReason classification)
    : std::runtime_error(failure_message(failed_index, slot, classification)), index(failed_index),
      operand(slot.operand), address(slot.address), name_hash(slot.name_hash), name(slot.name),
      reason(classification) {}
std::span<const BuildStartupConVarSlot, 105> build_startup_convar_slots() noexcept {
    return slots;
}
NativeConVarReference BuildStartupConVarBindings::at(std::uint32_t address) const {
    for (const auto& slot : slots)
        if (slot.address == address)
            return {data_.native_cell<std::uint32_t>(address), slot.xor_key};
    throw std::out_of_range("Address is not a fixed-build startup ConVar cell");
}
StartupConVarBindings BuildStartupConVarBindings::startup_bindings() const noexcept {
    return StartupConVarBindings(data_);
}
client::StartupConVars BuildStartupConVarBindings::startup_variables() const {
    return startup_bindings().startup_variables();
}
NativeConVarReference BuildStartupConVarBindings::accuracy_variable() const {
    return startup_bindings().accuracy_variable();
}
BuildStartupConVarBindings discover_build_startup_convars(NativeDataImage& data, void* cvar,
                                                          BuildStartupConVarProvider& provider) {
    if (!cvar)
        throw std::invalid_argument("Build ConVar discovery requires the real ICvar interface");
    std::array<std::uint32_t*, slots.size()> destinations;
    for (std::size_t index = 0; index < slots.size(); ++index)
        destinations[index] = &data.native_cell<std::uint32_t>(slots[index].address);
    std::array<void*, slots.size()> found;
    for (std::size_t index = 0; index < slots.size(); ++index) {
        found[index] = provider.find_variable(cvar, slots[index].name);
        // Native NULL reaches4337F065 with code10 after any earlier stores.
        // The command-candidate path reaches erased4353F942. Acquisition and
        // typed failure before any publication are explicit source policy.
        if (!found[index])
            throw BuildStartupConVarFailure(index, slots[index],
                                            BuildStartupConVarFailureReason::missing_candidate);
        if (provider.is_command(found[index]))
            throw BuildStartupConVarFailure(index, slots[index],
                                            BuildStartupConVarFailureReason::candidate_is_command);
    }
    for (std::size_t index = 0; index < slots.size(); ++index)
        *destinations[index] =
            std::uint32_t(reinterpret_cast<std::uintptr_t>(found[index])) ^ slots[index].xor_key;
    return BuildStartupConVarBindings(data);
}
BuildStartupConVarBindings discover_build_startup_convars(NativeDataImage& data, void* cvar) {
    EngineBuildConVarProvider provider;
    return discover_build_startup_convars(data, cvar, provider);
}
} //namespace skeet::engine
