-- Grenade Enhancements by Onii

local ffi = require "ffi"
local bit = require "bit"

local band = bit.band
local abs, ceil, cos, exp, floor, huge = math.abs, math.ceil, math.cos, math.exp, math.floor, math.huge
local max, min, rad, sin, sqrt = math.max, math.min, math.rad, math.sin, math.sqrt

local client_camera_angles, client_eye_position, client_trace_line = client.camera_angles, client.eye_position, client.trace_line
local entity_get_all, entity_get_prop = entity.get_all, entity.get_prop
local globals_realtime, globals_tickinterval = globals.realtime, globals.tickinterval
local renderer_circle, renderer_circle_outline, renderer_text = renderer.circle, renderer.circle_outline, renderer.text
local ui_get, ui_set, ui_set_visible = ui.get, ui.set, ui.set_visible

ffi.cdef [[
	typedef uint8_t (__thiscall *ge_is_cached_fn)(void *manager, const char *name);
	typedef void    (__thiscall *ge_find_index_fn)(void *dictionary, uint16_t *index, const char *name);
	typedef void   *(__fastcall *ge_create_fn)(void *ecx, void *system, float *origin, int a, int b, int owner);
	typedef void    (__thiscall *ge_set_control_point_fn)(void *effect, int index, float *position);
	typedef void    (__thiscall *ge_set_orientation_fn)(void *collection, int index, float *forward, float *right, float *up);
	typedef void    (__thiscall *ge_stop_emission_fn)(void *collection, int infinite_only, int remove_all, int wake, int end_cap);
	typedef void    (__thiscall *ge_trace_ray_fn)(void *self, void *ray, unsigned int mask, void *filter, void *result);

	typedef const void *(__thiscall *ge_get_model_fn)(void *self, int index);
	typedef const char *(__thiscall *ge_get_model_name_fn)(void *self, const void *model);
	typedef const char *(__thiscall *ge_get_model_keyvalues_fn)(void *self, const void *model);
	typedef const void *(__thiscall *ge_get_collision_model_fn)(void *self, int index);
	typedef const char *(__thiscall *ge_get_entity_lump_fn)(void *self);
	typedef const float *(__thiscall *ge_view_matrix_fn)(void *self);
]]

--------------------------------------------------------------------------------
-- Helpers
--------------------------------------------------------------------------------

local function log(r, g, b, message)
	client.color_log(r, g, b, "[Grenade Enhancements] " .. message)
end

-- Reports the first error of a callback instead of flooding the console every frame.
local function guarded(callback)
	local failed = false
	return function(...)
		local ok, err = pcall(callback, ...)
		if not ok and not failed then
			failed = true
			log(255, 80, 80, tostring(err))
		end
	end
end

-- Server-side cvars do not exist on the client, hence the default.
local function cvar_float(name, default)
	local ok, value = pcall(function() return cvar[name]:get_float() end)
	return ok and value or default
end

local function clamp(value, limit)
	return max(-limit, min(limit, value))
end

local function address(pointer) return tonumber(ffi.cast("uint32_t", pointer)) end
local function u8(addr) return ffi.cast("uint8_t *", addr)[0] end
local function i32(addr) return ffi.cast("int32_t *", addr)[0] end
local function u32(addr) return ffi.cast("uint32_t *", addr)[0] end
local function align16(addr) return floor((addr + 15) / 16) * 16 end

local function has_bytes(addr, ...)
	for i = 1, select("#", ...) do
		if u8(addr + i - 1) ~= select(i, ...) then return false end
	end
	return true
end

-- Destination of the `call rel32` at addr.
local function call_target(addr)
	return addr + 5 + i32(addr + 1)
end

local function vfunc(instance, index)
	return u32(u32(instance) + index * 4)
end

--------------------------------------------------------------------------------
-- Constants
--------------------------------------------------------------------------------

-- m_iItemDefinitionIndex of each grenade, as the simulation's kind
local ITEM_KIND = {[43] = "flash", [44] = "he", [45] = "smoke", [46] = "molotov", [47] = "decoy", [48] = "molotov"}

-- Every grenade projectile shares these. The types differ in their fuse, in what touching something does
-- (see simulate) and in the molotov's contact detonation.
local THROW_VELOCITY  = 750            -- items_game.txt "throw velocity"
local THROW_DAMPENING = 0.3            -- GRENADE_SECONDARY_DAMPENING
local THROW_LOWER     = 12             -- GRENADE_SECONDARY_LOWER
local GRENADE_SIZE    = 2              -- GRENADE_DEFAULT_SIZE, half extent of the collision box
local GRAVITY_SCALE   = 0.4            -- GetGrenadeGravity
local ELASTICITY      = 0.45           -- GetGrenadeElasticity
local SLEEP_SPEED_SQR = 20 * 20        -- kSleepVelocitySquared
local STOP_EPSILON    = 0.1            -- PhysicsClipVelocity
local MAX_BOUNCES     = 20             -- GRENADE_FAILSAFE_MAX_BOUNCES
local THINK_INTERVAL  = 0.2            -- DangerSoundThink
-- Seconds from the throw to the first detonation check (molotov: molotov_throw_detonate_time; smoke and decoy:
-- SetTimer in their Create).
local FUSE = {he = 1.5, flash = 1.5, molotov = 2.0, smoke = 1.5, decoy = 2.0}
-- Smoke and decoy re-check every 0.2 s after the fuse and go off once slower than this (Think_Detonate).
local STILL_SPEED = {smoke = 0.1, decoy = 0.2}

local DETONATE_SLOPE  = cos(rad(30))   -- weapon_molotov_maxdetonateslope
local BREAK_DAMAGE    = 10             -- club damage to a breakable in the way, ResolveFlyCollisionCustom
local BREAK_SLOWDOWN  = 0.4            -- velocity kept after going through one
local TOUCH_DAMAGE    = 1              -- CBaseGrenade::BounceTouch
local TOUCH_SPEED     = 100            -- slowest it still does damage at
local TOUCH_INTERVAL  = 1.0            -- seconds between two of them

local MASK = {
	GRENADE        = 0x4208608B,   -- CBaseCSGrenadeProjectile::PhysicsSolidMaskForEntity
	GRENADE_DEBRIS = 0x0008400B,   -- the same once it is debris: (CONTENTS_GRENADECLIP | MASK_SOLID) & ~CONTENTS_MONSTER
	THROW          = 0x0208400B,   -- MASK_SOLID | CONTENTS_GRENADECLIP
	SOLID          = 0x0200400B,
	LINE           = 0x0008400B,   -- PhysicsPushEntity's retry when starting in solid
	SHOT           = 0x46004003,
	SHOT_HULL      = 0x0600400B,   -- CBaseGrenade::Detonate's look for the ground
	FLASH          = 0x06004001,   -- PercentageOfFlashForPlayer: MASK_OPAQUE_AND_NPCS | CONTENTS_DEBRIS, less CONTENTS_OPAQUE
	FIRE_SIGHT     = 0x02004003,   -- INFERNO_MASK_LOS_CHECK
}

local COLLISION_GROUP_PROJECTILE = 13
local COLLISION_GROUP_DEBRIS     = 1
local PLAYER_ELASTICITY          = 0.3          -- a player's surface elasticity
local FSOLID_NOT_STANDABLE       = 0x10
local SURF_SKY                   = 0x4
local MAX_IGNORED                = 32  -- entities one simulation can take out of its traces

-- client.dll signatures, \xCC being the wildcard.
local SIGNATURES = {
	-- CNewParticleEffect *create(Vector *origin, int, int, int owner), particle system in edx
	create            = "\x55\x8B\xEC\x83\xEC\x0C\x53\x56\x8B\xF2\x89\x75\xF8\x57\x85\xF6\x75\x09",
	set_control_point = "\x55\x8B\xEC\x53\x8B\x5D\x0C\x56\x8B\xF1\xF6\x86",
	set_orientation   = "\x55\x8B\xEC\x51\x53\x8B\x5D\x10\x56\x8B\xF1\x57\x8B\x7D\x08\x89\x75\xFC\x8B\x4E\x64",
	stop_emission     = "\x55\x8B\xEC\x51\x57\x8B\xF9\x8B\x47\x48\x85\xC0\x0F\x84\x0C\x01",
	-- the smoke grenade's effect helper, which references the particle manager and its name lookups
	smoke_effect      = "\x55\x8B\xEC\x51\x56\x8B\x35\xCC\xCC\xCC\xCC\x8B\xCE\x68\xCC\xCC\xCC\xCC\xE8\xCC\xCC\xCC\xCC\x84\xC0",
}

-- client.dll offsets with no usable signature. The module base comes from the create signature.
local RVA = {
	create                  = 0x32BCB0,
	world_filter_vtable     = 0xBFB97C,   -- CTraceFilterWorldAndPropsOnly
	world_filter_should_hit = 0x19C260,
	list_filter_vtable      = 0xBEA984,   -- CTraceFilterSimpleList
	list_filter_should_hit  = 0x379610,
	game_rules              = 0x379308,   -- operand of `mov ecx, g_pGameRules` in CTraceFilterSimple::ShouldHitEntity
}

-- Offsets into game structures.
local OFFSET = {
	entity_index      = 0x64,             -- C_BaseEntity
	trace_surface     = 66,               -- trace_t::surface.flags
	trace_entity      = 76,               -- trace_t::m_pEnt
	collision_text    = 8,                -- vcollide_t::pKeyValues
}

-- Virtual function indices.
local VFUNC = {
	trace_ray           = 5,              -- IEngineTrace
	get_model           = 1,              -- IVModelInfoClient
	get_model_name      = 3,
	get_collision_model = 6,
	get_model_keyvalues = 21,
	get_entity_lump     = 99,             -- IVEngineClient::GetMapEntitiesString
	view_matrix         = 37,             -- IVEngineClient::WorldToScreenMatrix
}

--------------------------------------------------------------------------------
-- Tracing
--------------------------------------------------------------------------------

-- IEngineTrace::TraceRay through one of two of the game's filters:
--   CTraceFilterWorldAndPropsOnly  the world and static props, no entities
--   CTraceFilterSimpleList         entities as well, by the rules a projectile collides with them, except for
--                                  the ones on its ignore list
-- The second one calls into the game rules, so it only works while those exist (trace.entities).
local trace = {ready = false, enabled = false, checked = false, entities = false}

-- CTraceFilterSimpleList: +8 collision group, +16 the ignored entities, +28 how many of them.
local function init_entity_filter(base)
	local vtable, should_hit = base + RVA.list_filter_vtable, base + RVA.list_filter_should_hit
	if u32(vtable) ~= should_hit then return end
	-- mov esi, [edi + 1Ch] for the count, and the game rules being loaded where their address is read from
	if not has_bytes(should_hit + 12, 0x8B, 0x77, 0x1C) or not has_bytes(base + RVA.game_rules - 2, 0x8B, 0x0D) then return end

	trace.ignored = ffi.new("uint32_t[?]", MAX_IGNORED)
	trace.entity_filter_data = ffi.new("uint32_t[9]")
	trace.entity_filter_data[0] = vtable
	trace.entity_filter_data[2] = COLLISION_GROUP_PROJECTILE
	trace.entity_filter_data[4] = address(trace.ignored)
	trace.entity_filter_data[5] = MAX_IGNORED
	trace.entity_filter = ffi.cast("void *", trace.entity_filter_data)
	trace.game_rules = u32(base + RVA.game_rules)
end

local function init_trace(base)
	-- the filter's vtable has to start with its ShouldHitEntity: xor al, al / ret 8
	local vtable, should_hit = base + RVA.world_filter_vtable, base + RVA.world_filter_should_hit
	if not has_bytes(base, 0x4D, 0x5A) then return end
	if u32(vtable) ~= should_hit or not has_bytes(should_hit, 0x32, 0xC0, 0xC2) then return end

	local interface = client.create_interface("engine.dll", "EngineTraceClient004")
	if not interface then return end

	local instance = address(interface)
	trace.self = ffi.cast("void *", instance)
	trace.call = ffi.cast("ge_trace_ray_fn", vfunc(instance, VFUNC.trace_ray))

	trace.filter_data = ffi.new("uint32_t[4]")
	trace.filter_data[0] = vtable
	trace.filter = ffi.cast("void *", trace.filter_data)
	init_entity_filter(base)

	-- Ray_t has to be 16 byte aligned
	trace.ray_data = ffi.new("uint8_t[128]")
	local ray = align16(address(trace.ray_data))
	trace.ray = ffi.cast("float *", ray)
	trace.ray_flags = ffi.cast("uint8_t *", ray)
	trace.ray_ptr = ffi.cast("void *", ray)

	trace.result_data = ffi.new("uint8_t[192]")
	local result = align16(address(trace.result_data))
	trace.result = ffi.cast("float *", result)
	trace.result_flags = ffi.cast("uint8_t *", result)
	trace.result_ptr = ffi.cast("void *", result)

	trace.ready = true
end

-- Sweeps a box of the given half extent (0 for a ray) from x, y, z along dx, dy, dz, through the entity
-- filter if asked to. Returns the fraction, the plane normal, the end position and whether it started in solid.
-- With `tall`, the box is a player's: half extent `half` across, standing `tall` high on x, y, z (Ray_t::Init
-- with mins and maxs; the end position is then that of the box's centre).
local function trace_hull(x, y, z, dx, dy, dz, mask, half, entities, tall)
	local ray = trace.ray
	local up = tall and tall / 2 or 0
	ray[0], ray[1], ray[2] = x, y, z + up                             -- m_Start
	ray[4], ray[5], ray[6] = dx, dy, dz                               -- m_Delta
	ray[8], ray[9], ray[10] = 0, 0, -up                               -- m_StartOffset
	ray[12], ray[13], ray[14] = half, half, tall and up or half       -- m_Extents
	trace.ray_flags[68] = half == 0 and 1 or 0                        -- m_IsRay
	trace.ray_flags[69] = (dx ~= 0 or dy ~= 0 or dz ~= 0) and 1 or 0  -- m_IsSwept

	ffi.fill(trace.result_data, 192)
	trace.call(trace.self, trace.ray_ptr, mask, entities and trace.entity_filter or trace.filter, trace.result_ptr)

	-- trace_t: endpos +12, plane.normal +24, fraction +44, startsolid +55
	local result = trace.result
	return result[11], result[6], result[7], result[8], result[3], result[4], result[5], trace.result_flags[55] ~= 0
end

-- What the last trace_hull ran into: the entity's index and address. The world and static props are entity 0.
local function trace_entity()
	local pointer = ffi.cast("uint32_t *", ffi.cast("uint8_t *", trace.result_ptr) + OFFSET.trace_entity)[0]
	if pointer == 0 then return 0, 0 end
	return i32(pointer + OFFSET.entity_index), pointer
end

local function trace_hit_sky()
	if not trace.enabled then return false end
	local flags = ffi.cast("uint16_t *", ffi.cast("uint8_t *", trace.result_ptr) + OFFSET.trace_surface)[0]
	return band(flags, SURF_SKY) ~= 0
end

local function reset_ignored()
	if trace.entity_filter_data then trace.entity_filter_data[7] = 0 end
end

-- Takes an entity out of the traces that follow. False once the list is full.
local function ignore_entity(pointer)
	local filter = trace.entity_filter_data
	local count = filter[7]
	if count >= MAX_IGNORED then return false end
	trace.ignored[count] = pointer
	filter[7] = count + 1
	return true
end

-- Players the simulated grenade passes through: its thrower (PassServerEntityFilter), and all of them once it
-- has gone debris. Every other player is solid to it, whatever their team: the team masks in
-- CCSPlayer::ShouldCollide only apply to player movement.
local trace_owner, trace_debris = 0, false

local function is_player(entindex)
	return entindex >= 1 and entindex <= globals.maxplayers()
end

-- trace_hull against everything a projectile collides with, stepping past the players above. Also returns the
-- index and address of the entity in the way, 0 for the world.
local function trace_solid(x, y, z, dx, dy, dz, mask, half, tall)
	if not trace.entities then
		local fraction, nx, ny, nz, ex, ey, ez, start_solid = trace_hull(x, y, z, dx, dy, dz, mask, half, false, tall)
		return fraction, nx, ny, nz, ex, ey, ez, start_solid, 0, 0
	end

	while true do
		local fraction, nx, ny, nz, ex, ey, ez, start_solid = trace_hull(x, y, z, dx, dy, dz, mask, half, true, tall)
		local entindex, pointer = trace_entity()
		local passes = is_player(entindex) and (entindex == trace_owner or trace_debris)
		if not passes or not ignore_entity(pointer) then
			return fraction, nx, ny, nz, ex, ey, ez, start_solid, entindex, pointer
		end
	end
end

-- PhysicsPushEntity: a sweep that starts in solid is retried as a ray from one step back to one step ahead.
local function sweep_hull(x, y, z, dx, dy, dz)
	-- PhysicsSolidMaskForEntity and the collision group both change once the grenade is debris
	local filter = trace.entity_filter_data
	if filter then filter[2] = trace_debris and COLLISION_GROUP_DEBRIS or COLLISION_GROUP_PROJECTILE end
	local fraction, nx, ny, nz, ex, ey, ez, start_solid, entindex, pointer =
		trace_solid(x, y, z, dx, dy, dz, trace_debris and MASK.GRENADE_DEBRIS or MASK.GRENADE, GRENADE_SIZE)
	if start_solid then
		fraction, nx, ny, nz, ex, ey, ez, start_solid, entindex, pointer =
			trace_solid(x - dx, y - dy, z - dz, dx * 2, dy * 2, dz * 2, MASK.LINE, 0)
	end
	if filter then filter[2] = COLLISION_GROUP_PROJECTILE end
	return fraction, nx, ny, nz, ex, ey, ez, entindex, pointer
end

-- Fallback without the engine trace: the box becomes a bundle of client.trace_line rays through its centre
-- and corners, and the plane normal is rebuilt from two more rays next to the first contact.
local HULL_POINTS = {{0, 0, 0}}
for _, x in ipairs {-GRENADE_SIZE, GRENADE_SIZE} do
	for _, y in ipairs {-GRENADE_SIZE, GRENADE_SIZE} do
		for _, z in ipairs {-GRENADE_SIZE, GRENADE_SIZE} do
			HULL_POINTS[#HULL_POINTS + 1] = {x, y, z}
		end
	end
end
local PROBE_SIGNS = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}}

local trace_ignore = 0   -- entity client.trace_line skips, set to the local player by the callers

-- Players are not part of the simulation, so a ray stopped by one is traced again without it.
local function trace_ray(x, y, z, dx, dy, dz)
	local ex, ey, ez = x + dx, y + dy, z + dz
	local fraction, entindex = client_trace_line(trace_ignore, x, y, z, ex, ey, ez)
	if fraction < 1 and entindex and entindex >= 1 and entindex <= 64 and entindex ~= trace_ignore then
		fraction = client_trace_line(entindex, x, y, z, ex, ey, ez)
	end
	return fraction
end

local function probe_normal(x, y, z, dx, dy, dz, fraction)
	local length = sqrt(dx * dx + dy * dy + dz * dz)
	local ux, uy, uz = dx / length, dy / length, dz / length
	local rx, ry, rz = 0, 0, 1
	if abs(uz) > 0.9 then rx, ry, rz = 1, 0, 0 end

	-- two axes perpendicular to the ray
	local ax, ay, az = uy * rz - uz * ry, uz * rx - ux * rz, ux * ry - uy * rx
	local a_length = sqrt(ax * ax + ay * ay + az * az)
	ax, ay, az = ax / a_length, ay / a_length, az / a_length
	local bx, by, bz = uy * az - uz * ay, uz * ax - ux * az, ux * ay - uy * ax

	for _, sign in ipairs(PROBE_SIGNS) do
		local sa, sb = sign[1], sign[2]
		local fa = trace_ray(x + ax * sa, y + ay * sa, z + az * sa, dx, dy, dz)
		local fb = trace_ray(x + bx * sb, y + by * sb, z + bz * sb, dx, dy, dz)
		if fa < 1 and fb < 1 then
			-- the plane through the three hit points
			local px, py, pz = ax * sa + dx * (fa - fraction), ay * sa + dy * (fa - fraction), az * sa + dz * (fa - fraction)
			local qx, qy, qz = bx * sb + dx * (fb - fraction), by * sb + dy * (fb - fraction), bz * sb + dz * (fb - fraction)
			local nx, ny, nz = py * qz - pz * qy, pz * qx - px * qz, px * qy - py * qx
			local n_length = sqrt(nx * nx + ny * ny + nz * nz)
			if n_length > 1e-6 then
				nx, ny, nz = nx / n_length, ny / n_length, nz / n_length
				if nx * ux + ny * uy + nz * uz > 0 then nx, ny, nz = -nx, -ny, -nz end
				return nx, ny, nz
			end
		end
	end
	return -ux, -uy, -uz
end

local function sweep_rays(x, y, z, dx, dy, dz)
	local fraction, first = 2, 0
	for i = 1, #HULL_POINTS do
		local point = HULL_POINTS[i]
		local hit = trace_ray(x + point[1], y + point[2], z + point[3], dx, dy, dz)
		if hit < fraction then fraction, first = hit, i end
	end
	if fraction >= 1 then return 1, 0, 0, 0, x + dx, y + dy, z + dz end

	local point = HULL_POINTS[first]
	local nx, ny, nz = probe_normal(x + point[1], y + point[2], z + point[3], dx, dy, dz, fraction)
	return fraction, nx, ny, nz, x + dx * fraction, y + dy * fraction, z + dz * fraction
end

-- Fraction, plane normal and end position of the grenade's box swept along dx, dy, dz, then the index and
-- address of the entity it ran into. Without the engine trace everything counts as the world.
local function sweep(x, y, z, dx, dy, dz)
	if trace.enabled then return sweep_hull(x, y, z, dx, dy, dz) end
	local fraction, nx, ny, nz, ex, ey, ez = sweep_rays(x, y, z, dx, dy, dz)
	return fraction, nx, ny, nz, ex, ey, ez, 0, 0
end

-- CMolotovProjectile::Detonate away from the world: the fire starts on whatever is up to 128 units below,
-- otherwise it bursts in the air.
local function find_burn_point(x, y, z)
	if trace.enabled then
		local fraction, _, _, _, _, _, ground = trace_solid(x, y, z + 10, 0, 0, -138, MASK.SOLID, 0)
		if fraction < 1 then return x, y, ground + 2 end
	else
		local fraction = client_trace_line(trace_ignore, x, y, z + 10, x, y, z - 128)
		if fraction < 1 then return x, y, z + 10 - 138 * fraction + 2 end
	end
end

-- CBaseGrenade::Detonate, which an HE grenade goes through: the blast moves onto whatever is between 8 units
-- above it and 24 below, pulled 0.6 units out of the surface. Starting inside something, it looks from the
-- grenade itself instead.
local function find_blast_point(x, y, z)
	if trace.enabled then
		local fraction, nx, ny, nz, ex, ey, ez, start_solid = trace_solid(x, y, z + 8, 0, 0, -32, MASK.SHOT_HULL, 0)
		if start_solid then fraction, nx, ny, nz, ex, ey, ez = trace_solid(x, y, z, 0, 0, -32, MASK.SHOT_HULL, 0) end
		if fraction < 1 then return ex + nx * 0.6, ey + ny * 0.6, ez + nz * 0.6 end
	else
		local fraction = client_trace_line(trace_ignore, x, y, z + 8, x, y, z - 24)
		if fraction < 1 then return x, y, z + 8 - 32 * fraction + 0.6 end
	end
	return x, y, z
end

-- Ray_t and trace_t are laid out by hand, so the engine trace has to agree with client.trace_line before it
-- is trusted. trace_line also stops at entities and uses another mask, so a ray may well differ: two that
-- hit the same spot are enough.
local function trace_is_sane(player)
	local ex, ey, ez = client_eye_position()
	local pitch, yaw = client_camera_angles()
	if not ex or not pitch then return false end

	local cp, sp, cy, sy = cos(rad(pitch)), sin(rad(pitch)), cos(rad(yaw)), sin(rad(yaw))
	local matches = 0
	for _, direction in ipairs {{0, 0, -1}, {0, 0, 1}, {cp * cy, cp * sy, -sp}, {cy, sy, 0}, {-cy, -sy, 0}, {sy, -cy, 0}} do
		local dx, dy, dz = direction[1] * 1500, direction[2] * 1500, direction[3] * 1500
		local expected = client_trace_line(player, ex, ey, ez, ex + dx, ey + dy, ez + dz)
		local fraction, nx, ny, nz = trace_hull(ex, ey, ez, dx, dy, dz, MASK.SOLID, 0)
		if fraction < 1 and abs(sqrt(nx * nx + ny * ny + nz * nz) - 1) > 0.02 then return false end
		if fraction < 1 and abs(fraction - expected) <= 0.03 then matches = matches + 1 end
	end
	return matches >= 2
end

-- A failed check is repeated a second later: the player may just have stood among entities.
local function check_trace(player)
	local now = globals_realtime()
	if trace.ready and not trace.enabled and now >= (trace.checked or 0) then
		trace.checked = now + 1
		trace.enabled = trace_is_sane(player)
	end
	trace.entities = trace.enabled and trace.entity_filter ~= nil and u32(trace.game_rules) ~= 0
end

--------------------------------------------------------------------------------
-- Game data
--------------------------------------------------------------------------------

local model_info, engine_client
do
	local interface = client.create_interface("engine.dll", "VModelInfoClient004")
	if interface then
		local instance = address(interface)
		model_info = {
			self          = ffi.cast("void *", instance),
			get_model     = ffi.cast("ge_get_model_fn", vfunc(instance, VFUNC.get_model)),
			get_name      = ffi.cast("ge_get_model_name_fn", vfunc(instance, VFUNC.get_model_name)),
			get_keyvalues = ffi.cast("ge_get_model_keyvalues_fn", vfunc(instance, VFUNC.get_model_keyvalues)),
			get_collision = ffi.cast("ge_get_collision_model_fn", vfunc(instance, VFUNC.get_collision_model)),
		}
	end

	interface = client.create_interface("engine.dll", "VEngineClient014")
	if interface then
		local instance = address(interface)
		engine_client = {
			self            = ffi.cast("void *", instance),
			get_entity_lump = ffi.cast("ge_get_entity_lump_fn", vfunc(instance, VFUNC.get_entity_lump)),
			view_matrix     = ffi.cast("ge_view_matrix_fn", vfunc(instance, VFUNC.view_matrix)),
		}
	end
end

local function lookup_model_name(index)
	if not model_info or index <= 0 then return "" end
	local model = model_info.get_model(model_info.self, index)
	if model == nil then return "" end
	local name = model_info.get_name(model_info.self, model)
	return name ~= nil and ffi.string(name):lower() or ""
end

-- Lower case model name by model index, "" if it cannot be looked up.
local model_names = {}

local function model_name(index)
	local name = model_names[index]
	if not name then
		local found, result = pcall(lookup_model_name, index)
		name = found and result or ""
		model_names[index] = name
	end
	return name
end

-- The text sources below return nil when there is nothing to read.

-- The keyvalues compiled into a model, which is where its prop_data lives.
local function model_keyvalues(index)
	if not model_info or index <= 0 then return end
	local model = model_info.get_model(model_info.self, index)
	if model == nil then return end
	local text = model_info.get_keyvalues(model_info.self, model)
	if text ~= nil then return ffi.string(text) end
end

-- The text section of a model's collision data, which lists the gibs it breaks into.
local function collision_keyvalues(index)
	if not model_info or index <= 0 then return end
	local collision = model_info.get_collision(model_info.self, index)
	if collision == nil then return end

	local packed = band(ffi.cast("const uint16_t *", collision)[0], 0x8000) ~= 0   -- vcollide_t::isPacked
	local text = u32(address(collision) + OFFSET.collision_text)
	if not packed and text ~= 0 then return ffi.string(ffi.cast("const char *", text)) end
end

-- The map's entity lump: every entity's keyvalues as the server spawns them.
local function entity_lump()
	if not engine_client then return end
	local text = engine_client.get_entity_lump(engine_client.self)
	if text ~= nil then return ffi.string(text) end
end

-- Valve KeyValues text as nested tables. A value is a string, a section a list of every block of that name.
-- Keys are lower case, and of a repeated value the first one counts, as with KeyValues::FindKey.
local function parse_keyvalues(text)
	local position, length = 1, #text

	-- returns the token and whether it is text rather than a brace
	local function token()
		while true do
			position = text:find("%S", position) or length + 1
			if text:sub(position, position + 1) ~= "//" then break end
			position = (text:find("\n", position, true) or length) + 1
		end
		if position > length then return end

		local first = text:sub(position, position)
		if first == '"' then
			local close = text:find('"', position + 1, true) or length + 1
			local value = text:sub(position + 1, close - 1)
			position = close + 1
			return value, true
		elseif first == "{" or first == "}" then
			position = position + 1
			return first, false
		end

		local stop = text:find('[%s{}"]', position) or length + 1
		local value = text:sub(position, stop - 1)
		position = stop
		return value, true
	end

	local function section()
		local result = {}
		while true do
			local key, key_is_text = token()
			if not key or (key == "}" and not key_is_text) then return result end
			key = key:lower()

			local value, value_is_text = token()
			if not value then return result end
			if value == "{" and not value_is_text then
				local blocks = result[key]
				if type(blocks) ~= "table" then
					blocks = {}
					result[key] = blocks
				end
				blocks[#blocks + 1] = section()
			elseif result[key] == nil then
				result[key] = value
			end
		end
	end

	return section()
end

local function first_section(tree, key)
	local blocks = tree[key]
	return type(blocks) == "table" and blocks[1] or nil
end

-- atoi
local function to_integer(text)
	local number = tonumber(text) or 0
	return number < 0 and ceil(number) or floor(number)
end

--------------------------------------------------------------------------------
-- Friendly fire
--------------------------------------------------------------------------------

-- The player a networked handle (m_hThrower, m_hOwnerEntity) points at, nil for none or a non-player.
local function player_from_handle(handle)
	local index = handle and band(handle, 0x7FF) or 0
	if index >= 1 and index <= globals.maxplayers() then return index end
end

-- CCSPlayer::GetAssociatedTeamNumber: a coach counts for the team they coach. The player resource also
-- knows the teams of players out of view.
local function team_of(entindex)
	local coaching = entity_get_prop(entindex, "m_iCoachingTeam") or 0
	if coaching ~= 0 then return coaching end
	local resource = entity.get_player_resource()
	return resource and entity_get_prop(resource, "m_iTeam", entindex) or entity_get_prop(entindex, "m_iTeamNum")
end

-- CCSPlayer::OnTakeDamage_Alive: the share of a grenade's damage that reaches the player, 0 for none.
--   kind  "he" for an HE grenade's blast, "fire" for a molotov's or incendiary's flames
-- Every cvar involved is replicated, so these are the server's values, whatever the game mode set them to.
local function damage_scale(kind, attacker, player)
	if not attacker then return 1 end   -- no attacking player: not friendly fire

	-- only an HE grenade is scaled for its thrower; their own fire burns them in full
	if attacker == player then return kind == "he" and cvar_float("ff_damage_reduction_grenade_self", 1) or 1 end

	if team_of(attacker) ~= team_of(player) or cvar_float("mp_teammates_are_enemies", 0) ~= 0 then return 1 end

	-- a teammate: nothing with friendly fire off, during the freeze time or in warmup
	if cvar_float("mp_friendlyfire", 0) == 0 then return 0 end
	local rules = entity.get_game_rules()
	if rules and (entity_get_prop(rules, "m_bFreezePeriod") == 1 or entity_get_prop(rules, "m_bWarmupPeriod") == 1) then
		return 0
	end
	if kind == "he" then return cvar_float("ff_damage_reduction_grenade", 0.85) end
	return cvar_float("ff_damage_reduction_other", 0.4)
end

--------------------------------------------------------------------------------
-- Breakables
--------------------------------------------------------------------------------

-- A grenade damages the breakable it runs into and goes through it if that breaks it. Whether it does is up
-- to the entity's health, which the server does not network, so it is worked out here the way the server does
-- on spawn: from the map's entity lump for brushes, and from the model's prop data for props. Damage dealt
-- earlier in the round is unknown, everything is taken to be at full health.
--
--   func_breakable_surf       breaks at the first hit
--   func_breakable            "health" from the map, club damage scaled by func_breakdmg_club
--   prop_dynamic,             health and damage scale from the model's prop_data and its base in propdata.txt;
--   prop_physics_multiplayer  only breakable if it has health and something to break into
local CLASSES = {
	CBreakableSurface = "glass", CBaseEntity = "brush", CDynamicProp = "dynamic", CPhysicsPropMultiplayer = "physics",
}

local PROPDATA_FILE = "csgo/scripts/propdata.txt"

-- func_breakable's "propdata" keyvalue is an index into this list (pFGDPropData)
local BRUSH_PROPDATA = {
	"Wooden.Tiny", "Wooden.Small", "Wooden.Medium", "Wooden.Large", "Wooden.Huge", "Metal.Small", "Metal.Medium",
	"Metal.Large", "Cardboard.Small", "Cardboard.Medium", "Cardboard.Large", "Stone.Small", "Stone.Medium",
	"Stone.Large", "Stone.Huge", "Glass.Small", "Plastic.Small", "Plastic.Medium", "Plastic.Large", "Pottery.Small",
	"Pottery.Medium", "Pottery.Large", "Pottery.Huge", "Glass.Window",
}
local MATERIAL_GLASS, MATERIAL_UNBREAKABLE_GLASS = 0, 7
local SF_BREAK_TRIGGER_ONLY = 1

-- The interactions that let a prop with health take damage even if it has no gibs (CBreakableProp::Spawn).
local INTERACTIONS = {
	{"physgun_interactions", "onbreak", "explode_fire"}, {"physgun_interactions", "onbreak", "explode_ice"},
	{"physgun_interactions", "onfirstimpact", "break"}, {"fire_interactions", "flammable", "yes"},
	{"fire_interactions", "ignite", "halfhealth"}, {"fire_interactions", "explosive_resist", "yes"},
	{"fire_interactions", "melee_immune", "yes"},
}

local GLASS = {health = 1, club = 1, minimum = 0}

local breakables = {}   -- per map: propdata.txt bases, props by model index, brushes by model name

local function propdata_base(name)
	local bases = breakables.bases
	if not bases then
		bases = {}
		breakables.bases = bases

		local found, text = pcall(function() return readfile(PROPDATA_FILE) end)
		local root = found and type(text) == "string" and first_section(parse_keyvalues(text), "propdata.txt")
		if root then
			for key, blocks in pairs(root) do
				if type(blocks) == "table" then bases[key] = blocks[1] end
			end
		else
			log(255, 180, 80, "could not read " .. PROPDATA_FILE .. ", breakable props are treated as solid")
		end
	end
	return bases[name:lower()]
end

-- CPropData::ParsePropFromKV: the base first, then whatever the section sets itself. The interaction
-- sections sit next to prop_data in a model, but inside the section of a base.
local function apply_propdata(prop, section, interactions, depth)
	local base = section.base
	base = type(base) == "string" and base ~= "" and depth < 8 and propdata_base(base)
	if base then apply_propdata(prop, base, base, depth + 1) end

	prop.club = tonumber(section["dmg.club"]) or prop.club
	if section.health then prop.health = to_integer(section.health) end
	if section.breakable_model then prop.gib_model = section.breakable_model end
	if section.breakable_count then prop.gib_count = to_integer(section.breakable_count) end

	for _, interaction in ipairs(INTERACTIONS) do
		local block = first_section(interactions, interaction[1])
		local value = block and block[interaction[2]]
		if type(value) == "string" and value:lower() == interaction[3] then
			prop.interacts = true
			if interaction[2] == "melee_immune" then prop.melee_immune = true end
		end
	end
end

-- CBreakableProp::Spawn. Nil for a prop that takes no damage.
local function read_prop(model_index)
	local text = model_keyvalues(model_index)
	if not text then return end

	local root = parse_keyvalues(text)
	root = first_section(root, "mdlkeyvalue") or root
	local section = first_section(root, "prop_data")
	if not section then return end

	local prop = {health = 0, club = 1, minimum = 0, gib_count = 0}
	apply_propdata(prop, section, root, 0)

	-- its own gibs, or else generic ones if the prop data names any. The client does not always keep the
	-- collision text; a prop with health is then taken to have gibs, which is the rule for anything a map
	-- puts in a grenade's way.
	local collision = collision_keyvalues(model_index)
	local gibs = collision and parse_keyvalues(collision)["break"]
	local gib_count = type(gibs) == "table" and #gibs or 0
	if not collision then gib_count = 1 end
	if gib_count == 0 and prop.gib_model and prop.gib_model ~= "" then gib_count = prop.gib_count end

	if prop.health ~= 0 and (gib_count > 0 or prop.interacts) then return prop end
end

local function prop_data(model_index)
	local props = breakables.props
	if not props then
		props = {}
		breakables.props = props
	end

	local prop = props[model_index]
	if prop == nil then
		prop = read_prop(model_index) or false
		props[model_index] = prop
	end
	return prop or nil
end

-- How an entity is recognised in both the entity lump and the game, which does not send entity names to the
-- client: a brush entity by its model ("*12"), anything else by its model and where it stands.
local function identity(model, x, y, z)
	model = model:lower():gsub("\\", "/")
	if model:sub(1, 1) == "*" then return model end
	return string.format("%s@%d %d %d", model, floor(x + 0.5), floor(y + 0.5), floor(z + 0.5))
end

-- From the entity lump: the keyvalues of every func_breakable by brush model, and which entities break along
-- with which. A map often pairs a visible model with an invisible func_breakable through OnBreak outputs that
-- fire each other's Break input, so whichever the grenade breaks, the other is gone as well.
local function read_map()
	local brushes, named, outputs = {}, {}, {}
	for block in (entity_lump() or ""):gmatch("%b{}") do
		local function value(key) return block:match('"' .. key .. '"%s+"([^"]*)"') end
		local model = value("model")
		if model then
			local x, y, z = (value("origin") or ""):match("(%S+)%s+(%S+)%s+(%S+)")
			local id = identity(model, tonumber(x) or 0, tonumber(y) or 0, tonumber(z) or 0)

			local name = value("targetname")
			if name then
				named[name] = named[name] or {}
				table.insert(named[name], id)
			end
			-- target, input, parameter, delay, times; separated by commas in older maps
			for output in block:gmatch('"OnBreak"%s+"([^"]*)"') do
				local target, input = output:match("^([^\27,]*)[\27,]([^\27,]*)")
				if input and input:lower() == "break" then outputs[#outputs + 1] = {id, target} end
			end

			if value("classname") == "func_breakable" and id:find("^%*%d+$") then
				local function number(key) return to_integer(value(key)) end
				brushes[id] = {
					health = number("health"), material = number("material"), flags = number("spawnflags"),
					propdata = number("propdata"), minimum = number("minhealthdmg"),
				}
			end
		end
	end

	local links = {}
	for _, output in ipairs(outputs) do
		for _, id in ipairs(named[output[2]] or {}) do
			links[output[1]] = links[output[1]] or {}
			table.insert(links[output[1]], id)
		end
	end
	return brushes, links
end

local function map_breakables()
	if not breakables.brushes then breakables.brushes, breakables.links = read_map() end
	return breakables.brushes, breakables.links
end

-- CBreakable::Spawn. func_breakable has no client class of its own: it is a CBaseEntity, like other brush
-- entities, and only the entity lump tells them apart. Nil for a brush that is not one.
local function brush_data(model_index)
	map_breakables()

	local keys = breakables.brushes[model_name(model_index)]
	if not keys then return end

	local brush = {health = keys.health, club = cvar_float("func_breakdmg_club", 1.5), minimum = keys.minimum}
	local base = BRUSH_PROPDATA[keys.propdata]
	base = base and propdata_base(base)
	if base then apply_propdata(brush, base, base, 0) end

	if brush.health == 0 or band(keys.flags, SF_BREAK_TRIGGER_ONLY) ~= 0 then
		brush.immune = true
		if keys.material == MATERIAL_GLASS then brush.health = 1 end
	end
	if keys.material == MATERIAL_UNBREAKABLE_GLASS then brush.immune = true end
	return brush
end

-- The state of a breakable during one simulation, nil for any other entity:
--   kind        "glass", "brush", "dynamic" or "physics"
--   health      what it has left, 0 for a prop that takes no damage
--   damageable  whether damage reaches its health at all
--   broken      set once the simulated grenade has broken it, or one that breaks along with it
--   id          its identity, for the links between breakables
local function breakable(entindex)
	local kind = CLASSES[entity.get_classname(entindex) or ""]
	if not kind then return end

	local model_index = entity_get_prop(entindex, "m_nModelIndex") or 0
	local ox, oy, oz = entity_get_prop(entindex, "m_vecOrigin")
	local data
	if kind == "glass" then
		data = GLASS
	elseif kind == "brush" then
		data = brush_data(model_index)
		if not data then return end
	else
		data = prop_data(model_index)
	end

	return {
		kind = kind, data = data, broken = false,
		health = data and data.health or 0,
		damageable = data ~= nil and not data.immune,
		id = ox and identity(model_name(model_index), ox, oy, oz),
	}
end

-- A club hit of the given damage: CBreakableSurface::TraceAttack, CBreakable::OnTakeDamage or
-- CBreakableProp::OnTakeDamage.
local function club(target, damage)
	if target.broken then return end
	if target.kind == "glass" then
		target.health, target.broken = 0, true
		return
	end

	local data = target.data
	if not target.damageable or damage < data.minimum then return end
	damage = damage * data.club
	if target.kind ~= "brush" and (damage < data.minimum or data.melee_immune) then return end

	-- health is an integer the damage is subtracted from as a float
	local health = target.health - damage
	target.health = health < 0 and ceil(health) or floor(health)
	target.broken = target.health <= 0
end

local function is_standable(entindex)
	return band(entity_get_prop(entindex, "m_usSolidFlags") or 0, FSOLID_NOT_STANDABLE) == 0
end

local function forget_map()
	breakables = {}
	model_names = {}
end

client.set_event_callback("level_init", forget_map)
client.set_event_callback("cs_game_disconnected", forget_map)

--------------------------------------------------------------------------------
-- Simulation
--------------------------------------------------------------------------------

-- Steps a projectile through PhysicsToss until it detonates.
--   kind  "he", "flash", "molotov", "smoke" or "decoy". What sets them apart:
--           he       1.5 s fuse; only damages a breakable by running into it
--           flash    1.5 s fuse; its touch does a point of damage on top of that, at most once a second
--           molotov  2 s fuse; no touch damage, but it detonates on touching a surface flatter than 30
--                    degrees unless that is a func_breakable. The result is then where the fire starts,
--                    which is nil for an air burst
--           smoke    touches like a flash; goes off once it has (nearly) stopped, checked every 0.2 s from 1.5 s
--           decoy    the same from 2 s

--   age   ticks since the throw; the detonation think runs on a schedule that starts there
--   owner the thrower's entindex, whom the grenade passes through
--   path  optional array that receives x, y, z of every tick, and of every bounce in between
--   hits  optional array that receives {x, y, z, entindex} of every bounce
--   fires optional burning flames {x, y, z}, which a smoke that bounces near them drops onto and goes off
-- Other players are where they stand now: the grenade bounces off them weakly, never comes to rest on them,
-- and a molotov does not go off on them. Hitting the same player twice in a row turns it into debris, which
-- passes through players and props from then on.
-- Returns x, y, z and the time left until detonation.
local function fly(x, y, z, vx, vy, vz, kind, age, owner, path, hits, fires)
	local molotov = kind == "molotov"
	local dt = globals_tickinterval()
	local fuse = FUSE[kind]
	local still = STILL_SPEED[kind]
	local gravity = GRAVITY_SCALE * cvar.sv_gravity:get_float()
	local speed_limit = cvar.sv_maxvelocity:get_float()
	local think_ticks = max(1, floor(0.5 + THINK_INTERVAL / dt))
	local fuse_ticks = ceil(fuse / dt)
	-- shortcut: a smoke or decoy still rolling after 20 s is cut off there; a real one comes to rest well before
	local last_tick = ceil((still and 20 or fuse + 1) / dt)
	local asleep, bounces = false, 0
	local touched = {}       -- breakables by entindex, false for any other entity
	local gone = {}          -- identities of the breakables broken so far, and of those that broke with them
	local next_touch = 0     -- m_flNextAttack
	local last_hit           -- m_lastHitPlayer: whatever it bounced off last

	reset_ignored()
	trace_owner, trace_debris = owner or 0, false

	local function record()
		if path then
			local n = #path
			path[n + 1], path[n + 2], path[n + 3] = x, y, z
		end
	end

	local function detonate(time)
		record()
		if kind == "he" then
			local bx, by, bz = find_blast_point(x, y, z)
			return bx, by, bz, time
		end
		if not molotov then return x, y, z, time end
		local bx, by, bz = find_burn_point(x, y, z)
		return bx, by, bz, time
	end

	-- CSmokeGrenadeProjectile::OnBounced: a smoke that bounces with a burning flame near the ground below it
	-- (up to SmokeGrenadeRadius down) drops onto that ground and goes off there. Not twice within 3 ticks.
	local last_bounce = -huge
	local function smoke_drops(tick)
		if tick - last_bounce <= 3 then return end
		last_bounce = tick
		if kind ~= "smoke" or not fires or #fires == 0 then return end

		local dx, dy, dz = x, y, z
		local fraction, _, _, _, ex, ey, ez, start_solid
		if trace.enabled then
			fraction, _, _, _, ex, ey, ez, start_solid = trace_hull(x, y, z, 0, 0, -166, MASK.SOLID, 0)
		else
			fraction = client_trace_line(trace_ignore, x, y, z, x, y, z - 166)
			ex, ey, ez, start_solid = x, y, z - 166 * fraction, false
		end
		if not start_solid then
			if fraction >= 1 then return end
			if fraction > 0.001 then dx, dy, dz = ex, ey, ez end
		end

		-- CInferno::BShouldExtinguishSmokeGrenadeBounce: a flame's centre within 60 units, in sight of it
		for _, flame in ipairs(fires) do
			local cx, cy, cz = flame[1], flame[2], flame[3] + 40
			if (dx - cx) ^ 2 + (dy - cy) ^ 2 + (dz - cz) ^ 2 < 60 * 60 then
				local clear
				if trace.enabled then
					clear = trace_hull(cx, cy, cz + 30, dx - cx, dy - cy, dz - cz - 30, MASK.FIRE_SIGHT, 0) >= 1
						or trace_hull(cx, cy, cz, dx - cx, dy - cy, dz - cz, MASK.FIRE_SIGHT, 0) >= 1
				else
					clear = client_trace_line(trace_ignore, cx, cy, cz + 30, dx, dy, dz) >= 1
				end
				if clear then return dx, dy, dz end
			end
		end
	end

	local function breakable_at(entindex)
		if entindex <= 0 then return end
		local target = touched[entindex]
		if target == nil then
			target = breakable(entindex) or false
			touched[entindex] = target
		end
		return target or nil
	end

	local function broke(target)
		if not target.id or gone[target.id] then return end
		gone[target.id] = true
		local _, links = map_breakables()
		for _, id in ipairs(links[target.id] or {}) do
			gone[id] = true
		end
	end

	-- A hit on a breakable that already broke along with another: the server has removed it, so the grenade
	-- carries on through. It does so from the point of contact, which loses the rest of this tick's move.
	local function vanished(entindex, pointer)
		local target = breakable_at(entindex)
		if not target or target.broken or not gone[target.id] then return false end
		target.broken, target.health = true, 0
		ignore_entity(pointer)
		return true
	end

	-- CMolotovProjectile::BounceTouch: whether the surface the last sweep ended on sets it off.
	local function ignites(nz, target, entindex)
		if not molotov or nz < DETONATE_SLOPE or trace_hit_sky() or is_player(entindex) then return false end
		return not (target and (target.kind == "glass" or target.kind == "brush"))
	end

	-- CMolotovProjectile::Detonate: on the world the fire starts right there
	local function ignite(time, entindex)
		if entindex > 0 then return detonate(time) end
		record()
		return x, y, z, time
	end

	-- ResolveFlyCollisionCustom's first half: a breakable in the way is hit, and gone through if that was
	-- the end of it. A prop the touch already broke only lets a prop_dynamic's killer through.
	local function breaks_through(target)
		if target.kind == "physics" and target.broken then return false end
		if (target.kind == "dynamic" or target.kind == "physics") and not target.damageable then return false end
		club(target, BREAK_DAMAGE)
		return target.health <= 0
	end

	for tick = 0, last_tick do
		-- DangerSoundThink
		local elapsed = age + tick
		if still then
			-- Think_Detonate
			if elapsed >= fuse_ticks and (elapsed - fuse_ticks) % think_ticks == 0
				and vx * vx + vy * vy + vz * vz <= still * still then
				return detonate(tick * dt)
			end
		elseif elapsed % think_ticks == 0 and elapsed * dt > fuse then
			return detonate(tick * dt)
		end
		record()

		if not asleep then
			-- PhysicsAddGravityMove
			local falling = vz - gravity * dt
			local mx, my, mz = vx * dt, vy * dt, (vz + falling) / 2 * dt
			vx, vy, vz = clamp(vx, speed_limit), clamp(vy, speed_limit), clamp(falling, speed_limit)

			-- PhysicsPushEntity
			local fraction, nx, ny, nz, entindex, pointer
			fraction, nx, ny, nz, x, y, z, entindex, pointer = sweep(x, y, z, mx, my, mz)

			if fraction < 1 and vanished(entindex, pointer) then fraction = 1 end
			if fraction < 1 then
				local target = breakable_at(entindex)

				-- the touch function comes first
				if ignites(nz, target, entindex) then
					return ignite((tick + fraction) * dt, entindex)
				end
				if kind ~= "he" and kind ~= "molotov" and target and not target.broken and (target.kind ~= "brush" or target.damageable)
					and tick * dt >= next_touch and vx * vx + vy * vy + vz * vz > TOUCH_SPEED * TOUCH_SPEED then
					club(target, TOUCH_DAMAGE)
					next_touch = tick * dt + TOUCH_INTERVAL
				end

				local through = target and breaks_through(target)
				if target and target.broken then
					ignore_entity(pointer)
					broke(target)
				end

				local player = is_player(entindex)
				if through then
					vx, vy, vz = vx * BREAK_SLOWDOWN, vy * BREAK_SLOWDOWN, vz * BREAK_SLOWDOWN
				elseif player and last_hit == entindex then
					-- the same player twice in a row: it goes on as debris, without bouncing
					trace_debris = true
				else
					last_hit = entindex
					-- the bounce goes into the path too, so that a line along it turns at the surface
					record()
					if hits then hits[#hits + 1] = {x, y, z, entindex} end
					local elasticity = player and ELASTICITY * PLAYER_ELASTICITY or ELASTICITY
					-- ResolveFlyCollisionCustom: reflect off the plane and lose energy
					local backoff = (vx * nx + vy * ny + vz * nz) * 2
					vx, vy, vz = vx - nx * backoff, vy - ny * backoff, vz - nz * backoff
					if abs(vx) < STOP_EPSILON then vx = 0 end
					if abs(vy) < STOP_EPSILON then vy = 0 end
					if abs(vz) < STOP_EPSILON then vz = 0 end
					vx, vy, vz = vx * elasticity, vy * elasticity, vz * elasticity

					local speed_sqr = vx * vx + vy * vy + vz * vz
					local floor_like = nz > 0.7 or (nz > 0.1 and speed_sqr < SLEEP_SPEED_SQR)
					if floor_like and (entindex <= 0 or is_standable(entindex)) then
						if speed_sqr < SLEEP_SPEED_SQR then
							vx, vy, vz, asleep = 0, 0, 0, true
						elseif speed_sqr > 96000 then
							-- a hard hit on the ground bounces less the steeper it comes in
							local along = (vx * nx + vy * ny + vz * nz) / sqrt(speed_sqr)
							if along > 0.5 then
								local keep = (1 - along) + 0.5
								vx, vy, vz = vx * keep, vy * keep, vz * keep
							end
						end
					end

					if not asleep then
						-- the rest of the tick is spent on the new velocity
						local rest = (1 - fraction) * dt
						fraction, nx, ny, nz, x, y, z, entindex = sweep(x, y, z, vx * rest, vy * rest, vz * rest)
						if fraction < 1 and ignites(nz, breakable_at(entindex), entindex) then
							return ignite((tick + 1) * dt, entindex)
						end
					end

					local sx, sy, sz = smoke_drops(tick)
					if sx then
						x, y, z = sx, sy, sz
						return detonate((tick + 1) * dt)
					end

					bounces = bounces + 1
					if bounces > MAX_BOUNCES then return detonate(tick * dt) end
				end
			end
		end
	end

	return detonate(last_tick * dt)
end

local function simulate(...)
	local x, y, z, time_left = fly(...)
	trace_owner, trace_debris = 0, false
	return x, y, z, time_left
end

--------------------------------------------------------------------------------
-- Particle effects
--------------------------------------------------------------------------------

local particles = {ready = false}

local function find_signature(pattern)
	local match = client.find_signature("client.dll", pattern) or client.find_signature("client_panorama.dll", pattern)
	return match and address(match)
end

local function init_particles(create, base)
	local set_control_point = find_signature(SIGNATURES.set_control_point)
	local set_orientation = find_signature(SIGNATURES.set_orientation)
	local stop_emission = find_signature(SIGNATURES.stop_emission)
	local smoke_effect = find_signature(SIGNATURES.smoke_effect)

	if not (create and set_control_point and set_orientation and stop_emission and smoke_effect) then
		return "client.dll signatures not found"
	end
	if not (has_bytes(smoke_effect + 0x12, 0xE8) and has_bytes(smoke_effect + 0x28, 0xE8)) then
		return "unexpected client.dll layout"
	end

	-- the smoke helper loads the particle manager and calls its two name lookups
	particles.manager    = u32(smoke_effect + 7)
	particles.is_cached  = ffi.cast("ge_is_cached_fn", call_target(smoke_effect + 0x12))
	particles.find_index = ffi.cast("ge_find_index_fn", call_target(smoke_effect + 0x28))

	-- create is cdecl with the particle system in edx. Declared fastcall, which puts the system there, it leaves
	-- its 16 bytes of stack arguments behind. The interpreter's FFI call restores the stack from its frame
	-- pointer, which makes that harmless; compiled code trusts the convention and would carry on with the stack
	-- 16 bytes off. Its caller is therefore never compiled (jit.off below), and without jit.off there is no effect.
	if not (jit and jit.off) then return "jit.off is unavailable" end
	particles.create = ffi.cast("ge_create_fn", create)

	particles.set_control_point = ffi.cast("ge_set_control_point_fn", set_control_point)
	particles.set_orientation   = ffi.cast("ge_set_orientation_fn", set_orientation)
	particles.stop_emission     = ffi.cast("ge_stop_emission_fn", stop_emission)
	particles.ready = true
end

do
	local create = find_signature(SIGNATURES.create)
	local base = create and create - RVA.create

	local problem = init_particles(create, base)
	if problem then log(255, 80, 80, "Trajectory effect is unavailable: " .. problem) end

	-- without the engine trace the simulation falls back to client.trace_line
	if base then init_trace(base) end
end

--------------------------------------------------------------------------------
-- Surface lines
--------------------------------------------------------------------------------

-- gamesense rounds the end points of renderer.line to whole pixels, which kinks a curve. The game's own
-- ISurface::DrawTexturedPolygon keeps floats, so a line drawn through it as a strip of quads, with a texture
-- that fades across the strip, stays smooth; a dot is one quad with a disc texture. surface_draw.line and
-- surface_draw.dot, or nil when this vguimatsurface.dll does not match.
local surface_draw = (function()
	ffi.cdef [[
		typedef struct { float x, y, u, v; } ge_vertex;
		typedef void (__thiscall *ge_draw_set_color_fn)(void *self, int r, int g, int b, int a);
		typedef void (__thiscall *ge_draw_set_rgba_fn)(void *self, int id, const unsigned char *rgba, int w, int h);
		typedef void (__thiscall *ge_draw_set_texture_fn)(void *self, int id);
		typedef int  (__thiscall *ge_new_texture_fn)(void *self, bool procedural);
		typedef void (__thiscall *ge_polygon_fn)(void *self, int count, ge_vertex *vertices, bool clip);
	]]

	-- CMatSystemSurface: vtable index, and the bytes the function starts with in this build
	local FUNCTIONS = {
		set_color   = {15,  "ge_draw_set_color_fn",   {0x55, 0x8B, 0xEC, 0x66, 0x0F, 0x6E, 0x45, 0x14, 0x8A, 0x45, 0x08}},
		set_rgba    = {37,  "ge_draw_set_rgba_fn",    {0x55, 0x8B, 0xEC, 0xA1}},
		set_texture = {38,  "ge_draw_set_texture_fn", {0x55, 0x8B, 0xEC, 0x56, 0x8B, 0x75, 0x08, 0x57, 0x8B, 0xF9, 0x3B, 0xB7}},
		new_texture = {43,  "ge_new_texture_fn",      {0x55, 0x8B, 0xEC, 0xB9}},
		polygon     = {106, "ge_polygon_fn",          {0x55, 0x8B, 0xEC, 0x56, 0x57, 0x8B, 0x7D, 0x08, 0x8B, 0xF1, 0x85, 0xFF}},
	}
	local BOUND_TEXTURE, COLOR = 0x2A8, 0x2C   -- m_iBoundTexture, and the r, g, b, a bytes DrawSetColor writes
	local TEXTURE_W, TEXTURE_H = 4, 64

	local interface = client.create_interface("vguimatsurface.dll", "VGUI_Surface031")
	if not interface then return end
	local surface = ffi.cast("void *", interface)
	local fn = {}
	for name, f in pairs(FUNCTIONS) do
		local addr = vfunc(address(surface), f[1])
		if not has_bytes(addr, unpack(f[3])) then
			log(255, 80, 80, "Grenade trajectory glow is unavailable: vguimatsurface.dll does not match")
			return
		end
		fn[name] = ffi.cast(f[2], addr)
	end

	-- Alpha across the strip. solid: opaque over `width`, fading out over a pixel each side. glow: a gaussian.
	local textures, buffers = {}, {}
	local function texture_for(profile, width)
		local key = profile .. width
		if textures[key] then return textures[key] end
		local strip = profile == "solid" and width + 2 or width
		local pixels = ffi.new("unsigned char[?]", TEXTURE_W * TEXTURE_H * 4)
		for y = 0, TEXTURE_H - 1 do
			local d = abs((y + 0.5) / TEXTURE_H - 0.5) * strip
			local coverage = profile == "solid" and max(0, min(1, width / 2 + 0.5 - d)) or exp(-d * d / (2 * (strip / 6) ^ 2))
			for x = 0, TEXTURE_W - 1 do
				local i = (y * TEXTURE_W + x) * 4
				pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3] = 255, 255, 255, floor(255 * coverage + 0.5)
			end
		end
		local id = fn.new_texture(surface, true)
		fn.set_rgba(surface, id, pixels, TEXTURE_W, TEXTURE_H)
		textures[key], buffers[key] = id, pixels
		return id
	end

	-- A disc `radius` pixels across at its own size, its edge anti-aliased, with a pixel to spare around it.
	local function disc_for(radius)
		local key = "disc" .. radius
		if textures[key] then return textures[key] end
		local size = 2 * ceil(radius + 1)
		local pixels = ffi.new("unsigned char[?]", size * size * 4)
		for y = 0, size - 1 do
			for x = 0, size - 1 do
				local d = sqrt((x + 0.5 - size / 2) ^ 2 + (y + 0.5 - size / 2) ^ 2)
				local i = (y * size + x) * 4
				pixels[i], pixels[i + 1], pixels[i + 2] = 255, 255, 255
				pixels[i + 3] = floor(255 * max(0, min(1, radius + 0.5 - d)) + 0.5)
			end
		end
		local id = fn.new_texture(surface, true)
		fn.set_rgba(surface, id, pixels, size, size)
		textures[key], buffers[key] = {id, size}, pixels
		return textures[key]
	end

	local quad = ffi.new("ge_vertex[4]")
	local nx, ny = {}, {}

	-- The surface caches its texture and colour; gamesense's drawing after ours expects them unchanged.
	local base = ffi.cast("uint8_t *", surface)
	local saved_texture, saved_color = 0, {}
	local function save()
		saved_texture = ffi.cast("int32_t *", base + BOUND_TEXTURE)[0]
		for i = 0, 3 do saved_color[i] = base[COLOR + i] end
	end
	local function restore()
		fn.set_texture(surface, saved_texture)
		for i = 0, 3 do base[COLOR + i] = saved_color[i] end
	end

	local function corner(k, x, y, u, v)
		quad[k].x, quad[k].y, quad[k].u, quad[k].v = x, y, u, v
	end

	-- x, y: the centre on screen; radius in pixels.
	local function dot(x, y, radius, r, g, b, a)
		local disc = disc_for(radius)
		local half = disc[2] / 2
		save()
		fn.set_texture(surface, disc[1])
		fn.set_color(surface, r, g, b, a)
		corner(0, x - half, y - half, 0, 0)
		corner(1, x + half, y - half, 1, 0)
		corner(2, x + half, y + half, 1, 1)
		corner(3, x - half, y + half, 0, 1)
		fn.polygon(surface, 4, quad, false)
		restore()
	end

	-- points: x1, y1, x2, y2, ... on screen, from first to last. width: the line's, or the whole glow's.
	local function line(points, count, width, r, g, b, a, profile)
		if count < 2 then return end
		local half = (profile == "solid" and width + 2 or width) / 2

		save()
		fn.set_texture(surface, texture_for(profile, width))
		fn.set_color(surface, r, g, b, a)

		-- a normal per point from its neighbours, so that neighbouring quads share an edge
		for i = 1, count do
			local p, q = max(1, i - 1), min(count, i + 1)
			local dx, dy = points[2 * q - 1] - points[2 * p - 1], points[2 * q] - points[2 * p]
			local length = sqrt(dx * dx + dy * dy)
			if length < 1e-6 then length, dx, dy = 1, 1, 0 end
			nx[i], ny[i] = -dy / length * half, dx / length * half
		end

		for i = 1, count - 1 do
			local px, py, qx, qy = points[2 * i - 1], points[2 * i], points[2 * i + 1], points[2 * i + 2]
			quad[0].x, quad[0].y, quad[0].u, quad[0].v = px + nx[i], py + ny[i], 0.5, 0
			quad[1].x, quad[1].y, quad[1].u, quad[1].v = qx + nx[i + 1], qy + ny[i + 1], 0.5, 0
			quad[2].x, quad[2].y, quad[2].u, quad[2].v = qx - nx[i + 1], qy - ny[i + 1], 0.5, 1
			quad[3].x, quad[3].y, quad[3].u, quad[3].v = px - nx[i], py - ny[i], 0.5, 1
			-- the surface wants them clockwise on screen (y down): a positive shoelace sum
			local area = 0
			for k = 0, 3 do
				local e = (k + 1) % 4
				area = area + quad[k].x * quad[e].y - quad[e].x * quad[k].y
			end
			if area < 0 then
				quad[1].x, quad[1].y, quad[1].v, quad[3].x, quad[3].y, quad[3].v = quad[3].x, quad[3].y, quad[3].v, quad[1].x, quad[1].y, quad[1].v
			end
			fn.polygon(surface, 4, quad, false)
		end
		restore()
	end

	return {line = line, dot = dot}
end)()

--------------------------------------------------------------------------------
-- Projection
--------------------------------------------------------------------------------

-- A function from world to screen positions for this frame, through the engine's view matrix: to the fraction of
-- a pixel where renderer.world_to_screen rounds to whole ones, and for points off screen too, which it leaves
-- out. Points behind the camera give nil. The matrix is checked against world_to_screen at x, y, z and only used
-- if the two agree: the same spot where world_to_screen has one, off screen where it has none.
local function screen_projector(width, height, x, y, z)
	local m = engine_client and engine_client.view_matrix(engine_client.self)
	if m == nil then return renderer.world_to_screen end
	local hw, hh = width / 2, height / 2
	local function project(px, py, pz)
		local w = m[12] * px + m[13] * py + m[14] * pz + m[15]
		if w < 0.001 then return end
		return hw * (1 + (m[0] * px + m[1] * py + m[2] * pz + m[3]) / w), hh * (1 - (m[4] * px + m[5] * py + m[6] * pz + m[7]) / w)
	end
	local ax, ay = renderer.world_to_screen(x, y, z)
	local bx, by = project(x, y, z)
	if ax then
		if not bx or abs(ax - bx) > 1.5 or abs(ay - by) > 1.5 then return renderer.world_to_screen end
	elseif bx and bx > 1 and bx < width - 1 and by > 1 and by < height - 1 then
		return renderer.world_to_screen
	end
	return project
end

--------------------------------------------------------------------------------
-- Grenade effects
--------------------------------------------------------------------------------

-- What a detonation does to a player, for the trajectory line and the warnings alike. A table rather than
-- locals, which the main chunk is short of.
local effects = {
	HE_DAMAGE         = 99,    -- items_game.txt
	HE_RADIUS         = 350,
	HE_ARMOR_RATIO    = 1.2,
	HE_MIN_RADIUS     = 100,   -- flashbangs keep CBaseGrenade's default radius of 100
	PLAYER_HEIGHT     = 72,    -- CCSGameRules::GetAmountOfEntityVisible
	PLAYER_HALF_WIDTH = 16,
	FLASH_RADIUS      = 3000,  -- RadiusFlash
}

-- CCSGameRules::GetExplosionDamageAdjustment: 1 if nothing in the world blocks the blast, else 0.
function effects.exposed(x, y, z, tx, ty, tz)
	if trace.enabled then
		local fraction = trace_hull(x, y, z, tx - x, ty - y, tz - z, MASK.SHOT, 0)
		return fraction >= 1 and 1 or 0
	end

	-- client.trace_line also stops at entities, the grenade included: step through them
	local ignore = trace_ignore
	for _ = 1, 5 do
		local fraction, entindex = client_trace_line(ignore, x, y, z, tx, ty, tz)
		if fraction >= 1 then return 1 end
		if not entindex or entindex <= 0 then return 0 end
		x, y, z = x + (tx - x) * fraction, y + (ty - y) * fraction, z + (tz - z) * fraction
		ignore = entindex
	end
	return 0
end

-- CCSPlayer::OnTakeDamage_Alive: what reaches the player's health after the friendly fire scale and armor.
function effects.after_armor(player, damage, scale)
	local heavy = entity_get_prop(player, "m_bHasHeavyArmor") == 1
	damage = damage * scale
	if heavy then damage = damage * 0.85 end

	local armor = entity_get_prop(player, "m_ArmorValue") or 0
	if armor <= 0 or damage <= 0 then return damage end

	local ratio, bonus, heavy_bonus = 0.5 * effects.HE_ARMOR_RATIO, 0.5, 1
	if heavy then ratio, bonus, heavy_bonus = ratio * 0.5, 0.33, 0.33 end

	local to_health = damage * ratio
	local to_armor = (damage - to_health) * (bonus * heavy_bonus)
	if to_armor > armor then to_health = damage - armor / bonus end
	return to_health
end

-- Another player's eye and view angles.
function effects.eye(player)
	local ox, oy, oz = entity_get_prop(player, "m_vecOrigin")
	if not ox then return end
	local pitch, yaw = entity_get_prop(player, "m_angEyeAngles")
	return ox, oy, oz + (entity_get_prop(player, "m_vecViewOffset[2]") or 64), pitch or 0, yaw or 0
end

-- CCSGameRules::RadiusDamage against a player, from their eye looking along yaw; the local view when no eye
-- is given. Returns the damage a blast at x, y, z does and the most the grenade could do at point blank.
function effects.blast_damage(player, x, y, z, damage, radius, scale, ex, ey, ez, yaw)
	if not ex then
		ex, ey, ez = client_eye_position()
		local _
		_, yaw = client_camera_angles()
	end
	local ox, oy, oz = entity_get_prop(player, "m_vecOrigin")
	if not ex or not ox then return end

	local exposed, half_width = effects.exposed, effects.PLAYER_HALF_WIDTH
	z = z + 1   -- the game lifts the blast off the ground
	local rx, ry = sin(rad(yaw)) * half_width, -cos(rad(yaw)) * half_width
	local chest = ez - oz

	-- chest, head, feet and both sides
	local visible = 0.40 * exposed(x, y, z, ex, ey, ez)
		+ 0.20 * exposed(x, y, z, ox, oy, oz + effects.PLAYER_HEIGHT)
		+ 0.20 * exposed(x, y, z, ox, oy, oz)
		+ 0.10 * exposed(x, y, z, ox - rx, oy - ry, oz + chest)
		+ 0.10 * exposed(x, y, z, ox + rx, oy + ry, oz + chest)

	local point_blank = effects.after_armor(player, damage, scale)
	if visible <= 0 then return 0, point_blank end

	-- gaussian falloff, the radius being three standard deviations
	local dx, dy, dz = ex - x, ey - y, ez - z
	local sigma = radius / 3
	local falloff = exp(-(dx * dx + dy * dy + dz * dz) / (2 * sigma * sigma))
	return max(0, floor(effects.after_armor(player, damage * falloff * visible, scale) + 0.5)), point_blank
end

-- Whether a flash's light gets from one point to another: through the world and props, not through players.
local function flash_clear(x, y, z, tx, ty, tz)
	if trace.enabled then
		local fraction, _, _, _, ex, ey, ez = trace_hull(x, y, z, tx - x, ty - y, tz - z, MASK.FLASH, 0)
		return fraction >= 1, ex, ey, ez
	end
	local fraction, entindex = client_trace_line(trace_ignore, x, y, z, tx, ty, tz)
	local clear = fraction >= 1 or (entindex or 0) > 0
	return clear, x + (tx - x) * fraction, y + (ty - y) * fraction, z + (tz - z) * fraction
end

-- RadiusFlash on a player: how long they stay blind, as a share of the longest a flash can blind anyone (at
-- point blank, looking at it, in full view). 0 when it does not reach them.
function effects.flash(player, x, y, z)
	local ex, ey, ez, pitch, yaw = effects.eye(player)
	if not ex then return 0 end
	local strength = floor(cvar_float("sv_flashbang_strength", 3.55))
	z = z + 1   -- in case it lies on the ground

	local dx, dy, dz = x - ex, y - ey, z - ez
	local distance = sqrt(dx * dx + dy * dy + dz * dz)
	local adjusted = strength - distance * strength / effects.FLASH_RADIUS
	if adjusted <= 0 or distance < 1e-3 then return 0 end

	-- PercentageOfFlashForPlayer: all of it in plain view; otherwise a sixth for each of three detours, up,
	-- up and right, up and left of the line from the flash to the eye
	local seen = 1
	if not flash_clear(x, y, z, ex, ey, ez) then
		seen = 0
		local fx, fy, fz = -dx / distance, -dy / distance, -dz / distance
		local p = math.atan2(-fz, sqrt(fx * fx + fy * fy))
		local w = math.atan2(fy, fx)
		local rxx, rxy = sin(w), -cos(w)                                    -- right
		local ux, uy, uz = sin(p) * cos(w), sin(p) * sin(w), cos(p)         -- up
		for _, offset in ipairs {{0, 50}, {75, 10}, {-75, 10}} do
			local px, py, pz = x + rxx * offset[1] + ux * offset[2], y + rxy * offset[1] + uy * offset[2], z + uz * offset[2]
			local _, hx, hy, hz = flash_clear(x, y, z, px, py, pz)
			if flash_clear(hx, hy, hz, ex, ey, ez) then seen = seen + 0.167 end
		end
		if seen <= 0 then return 0 end
	end

	-- how directly they face it
	local cp = cos(rad(pitch))
	local facing = (dx * cp * cos(rad(yaw)) + dy * cp * sin(rad(yaw)) - dz * sin(rad(pitch))) / distance
	local scale = facing >= 0.6 and 2.5 or facing >= 0.3 and 1.75 or facing >= -0.2 and 1 or 0.5
	return adjusted * scale * seen / (strength * 2.5)
end

-- CInferno::IsFirePosInSmokeCloud: the cloud is a squashed sphere, narrower at the top and bottom.
function effects.fire_in_cloud(x, y, z, sx, sy, sz)
	local above = z - sz
	local below_limit = 2 * 30 + 4   -- a flame's width plus 4
	if above < -below_limit or above > 120 then return false end

	local radius_sqr = 150 * 150
	if above > 120 * 0.6 then
		local t = min(1, (above - 120 * 0.6) / (120 * 0.4))
		radius_sqr = radius_sqr + (100 * 100 - radius_sqr) * t
	elseif above < 120 * 0.15 then
		local t = max(0, min(1, (above - 120 * 0.1) / (-below_limit - 120 * 0.1)))
		radius_sqr = radius_sqr + (128 * 128 - radius_sqr) * t
	end
	return (x - sx) ^ 2 + (y - sy) ^ 2 <= radius_sqr
end

-- Whether a fire starting at x, y, z in `seconds` would start inside a smoke that is still there, which keeps it
-- from starting at all (CInferno::CreateFire). A smoke counts until its entity is removed: 12.5 s, then its
-- alpha fades one step a tick from 255, then 1 s more.
function effects.smothered(x, y, z, seconds)
	local dt = globals_tickinterval()
	local lands = globals.tickcount() + seconds / dt
	for _, entindex in ipairs(entity_get_all("CSmokeGrenadeProjectile")) do
		local begin = entity_get_prop(entindex, "m_nSmokeEffectTickBegin") or 0
		if begin > 0 or entity_get_prop(entindex, "m_bDidSmokeEffect") == 1 then
			local expires = begin > 0 and begin + ceil(12.5 / dt) + 255 + ceil(1 / dt) or huge
			local sx, sy, sz = entity_get_prop(entindex, "m_vecOrigin")
			if sx and expires > lands and effects.fire_in_cloud(x, y, z, sx, sy, sz) then return true end
		end
	end
	return false
end

-- Every burning flame, {x, y, z}, and every fire that still has one, {x, y, z} of where it started.
function effects.fires()
	local flames, starts = {}, {}
	for _, entindex in ipairs(entity_get_all("CInferno")) do
		local ox, oy, oz = entity_get_prop(entindex, "m_vecOrigin")
		local burning = false
		for i = 0, (entity_get_prop(entindex, "m_fireCount") or 0) - 1 do
			if ox and entity_get_prop(entindex, "m_bFireIsBurning", i) == 1 then
				burning = true
				flames[#flames + 1] = {
					ox + (entity_get_prop(entindex, "m_fireXDelta", i) or 0),
					oy + (entity_get_prop(entindex, "m_fireYDelta", i) or 0),
					oz + (entity_get_prop(entindex, "m_fireZDelta", i) or 0),
				}
			end
		end
		if burning then starts[#starts + 1] = {ox, oy, oz} end
	end
	return flames, starts
end

-- CInferno::ExtinguishFlamesAroundSmokeGrenade: whether a smoke going off at x, y, z puts out any flame, either
-- one inside its cloud or a whole fire that started inside it.
function effects.extinguishes(x, y, z, flames, starts)
	for _, list in ipairs {flames, starts} do
		for _, p in ipairs(list) do
			if effects.fire_in_cloud(p[1], p[2], p[3], x, y, z) then return true end
		end
	end
	return false
end

--------------------------------------------------------------------------------
-- Trajectory effect
--------------------------------------------------------------------------------

do
	-- Misc > Miscellaneous > Super toss throws along the view direction whatever the player's movement.
	local super_toss, super_toss_hotkey
	do
		local found, checkbox, hotkey = pcall(ui.reference, "MISC", "Miscellaneous", "Super toss")
		if found then super_toss, super_toss_hotkey = checkbox, hotkey end
	end

	local function super_toss_active()
		if not super_toss or not ui_get(super_toss) then return false end
		return super_toss_hotkey == nil or (ui_get(super_toss_hotkey) and true or false)
	end

	-- Where the eye will be when the grenade leaves the hand, and the player's velocity then. Letting go only
	-- starts the throw (StartGrenadeThrow): the grenade goes on the first tick more than 0.1 s later, after that
	-- tick's movement, and the player moves on in the meantime - over 25 units strafing at full speed. Those
	-- ticks are run through a port of the game's own movement (CGameMovement::FullWalkMove with
	-- CCSGameMovement's parameters, acceleration, jump and stamina), the keys held now taken as held throughout.
	-- shortcut: crouching or standing up during those ticks, ladders, water, surfaces with their own friction
	-- and moving ground are not modelled
	local throw_origin = (function()
		local HALF_WIDTH, STAND, CROUCH = 16, 72, 54
		local MASK_PLAYERSOLID = 0x0201400B
		local COLLISION_GROUP_PLAYER_MOVEMENT = 8
		local DIST_EPSILON, MINIMUM_MOVE_FRACTION, HORIZONTAL_Z = 0.03125, 0.0001, 0.0001
		local MAX_CLIP_PLANES = 5
		local WALK, DUCK = 0.52, 0.34              -- CS_PLAYER_SPEED_WALK_MODIFIER, CS_PLAYER_SPEED_DUCK_MODIFIER
		local STAMINA_RANGE = 100
		local FL_ONGROUND, FL_DUCKING = 1, 2

		-- the keys of the latest command, before anti-aim and the like (setup_command runs ahead of them)
		local keys = {forward = 0, side = 0, jump = false, duck = false, walk = false}
		local function held(value) return value == true or value == 1 end
		client.set_event_callback("setup_command", function(cmd)
			keys.forward, keys.side = cmd.forwardmove or 0, cmd.sidemove or 0
			keys.jump, keys.duck, keys.walk = held(cmd.in_jump), held(cmd.in_duck), held(cmd.in_speed)
		end)

		-- MISC > Movement > Bunny hop lets go of jump in the air and presses it on landing: an automatic re-jump
		local bunny_hop
		do
			local found, reference = pcall(ui.reference, "MISC", "Movement", "Bunny hop")
			if found then bunny_hop = reference end
		end

		-- the movement state, as CMoveData and the player hold it
		local s = {}
		local tall

		-- TracePlayerBBox: the player's box from one origin to another. Returns the fraction, the normal, the end
		-- origin, startsolid and allsolid.
		local function box(x, y, z, tx, ty, tz)
			local dx, dy, dz = tx - x, ty - y, tz - z
			local fraction, nx, ny, nz, _, _, _, start_solid = trace_solid(x, y, z, dx, dy, dz, MASK_PLAYERSOLID, HALF_WIDTH, tall)
			local all_solid = trace.result_flags[54] ~= 0
			return fraction, nx, ny, nz, x + dx * fraction, y + dy * fraction, z + dz * fraction, start_solid, all_solid
		end

		local function check_velocity()
			local limit = cvar_float("sv_maxvelocity", 3500)
			s.vx, s.vy, s.vz = clamp(s.vx, limit), clamp(s.vy, limit), clamp(s.vz, limit)
		end

		local function clip_velocity(vx, vy, vz, nx, ny, nz, overbounce)
			local backoff = (vx * nx + vy * ny + vz * nz) * overbounce
			vx, vy, vz = vx - nx * backoff, vy - ny * backoff, vz - nz * backoff
			local adjust = vx * nx + vy * ny + vz * nz
			if adjust < 0 then
				adjust = min(adjust, -DIST_EPSILON)
				vx, vy, vz = vx - nx * adjust, vy - ny * adjust, vz - nz * adjust
			end
			return vx, vy, vz
		end

		-- TryPlayerMove: moves along the velocity for the tick, sliding along up to MAX_CLIP_PLANES surfaces
		local function try_player_move(first_dest, first)
			local planes = {}
			local ovx, ovy, ovz = s.vx, s.vy, s.vz   -- original_velocity
			local pvx, pvy, pvz = s.vx, s.vy, s.vz   -- primal_velocity
			local all_fraction, time_left = 0, s.dt
			local nvx, nvy, nvz = 0, 0, 0
			for _ = 1, 4 do
				if s.vx == 0 and s.vy == 0 and s.vz == 0 then break end
				local ex, ey, ez = s.x + s.vx * time_left, s.y + s.vy * time_left, s.z + s.vz * time_left
				local fraction, nx, ny, nz, px, py, pz, _, all_solid
				if first_dest and ex == first_dest[1] and ey == first_dest[2] and ez == first_dest[3] then
					fraction, nx, ny, nz, px, py, pz, _, all_solid = unpack(first)
				else
					fraction, nx, ny, nz, px, py, pz, _, all_solid = box(s.x, s.y, s.z, ex, ey, ez)
				end
				if fraction > 0 and fraction < MINIMUM_MOVE_FRACTION then fraction = 0 end
				all_fraction = all_fraction + fraction
				if all_solid then
					s.vx, s.vy, s.vz = 0, 0, 0
					return
				end
				if fraction > 0 then
					if fraction == 1 then
						-- a swept box can end up stuck in terrain: check the end on its own
						local stuck_fraction, _, _, _, _, _, _, stuck_start = box(px, py, pz, px, py, pz)
						if stuck_start or stuck_fraction ~= 1 then
							s.vx, s.vy, s.vz = 0, 0, 0
							break
						end
					end
					s.x, s.y, s.z = px, py, pz
					ovx, ovy, ovz = s.vx, s.vy, s.vz
					planes = {}
				end
				if fraction == 1 then break end

				if abs(nz) < HORIZONTAL_Z then nz = 0 end
				time_left = time_left - time_left * fraction
				if #planes >= MAX_CLIP_PLANES then
					s.vx, s.vy, s.vz = 0, 0, 0
					break
				end
				planes[#planes + 1] = {nx, ny, nz}

				if #planes == 1 and not s.ground then
					-- in the air, the first surface bounces it off (sv_bounce 0 makes that a slide)
					local p = planes[1]
					nvx, nvy, nvz = clip_velocity(ovx, ovy, ovz, p[1], p[2], p[3], 1)
					s.vx, s.vy, s.vz = nvx, nvy, nvz
					ovx, ovy, ovz = nvx, nvy, nvz
				else
					local i = 1
					while i <= #planes do
						local p = planes[i]
						s.vx, s.vy, s.vz = clip_velocity(ovx, ovy, ovz, p[1], p[2], p[3], 1)
						local j = 1
						while j <= #planes do
							local q = planes[j]
							if j ~= i and s.vx * q[1] + s.vy * q[2] + s.vz * q[3] < 0 then break end
							j = j + 1
						end
						if j > #planes then break end
						i = i + 1
					end
					if i > #planes then
						-- along the crease of two planes, or stuck in a corner of more
						if #planes ~= 2 then
							s.vx, s.vy, s.vz = 0, 0, 0
							break
						end
						local a, b = planes[1], planes[2]
						local cx, cy, cz = a[2] * b[3] - a[3] * b[2], a[3] * b[1] - a[1] * b[3], a[1] * b[2] - a[2] * b[1]
						local length = sqrt(cx * cx + cy * cy + cz * cz)
						if length > 0 then cx, cy, cz = cx / length, cy / length, cz / length end
						local d = cx * s.vx + cy * s.vy + cz * s.vz
						s.vx, s.vy, s.vz = cx * d, cy * d, cz * d
					end
					if s.vx * pvx + s.vy * pvy + s.vz * pvz <= 0 then
						s.vx, s.vy, s.vz = 0, 0, 0
						break
					end
				end
			end
			if all_fraction == 0 then s.vx, s.vy, s.vz = 0, 0, 0 end
		end

		-- StayOnGround: keeps a walking player on the ground down slopes and steps
		local function stay_on_ground()
			local _, _, _, _, ux, uy, uz = box(s.x, s.y, s.z, s.x, s.y, s.z + 2)
			local fraction, _, _, nz, ex, ey, ez, start_solid = box(ux, uy, uz, s.x, s.y, s.z - s.step)
			if fraction > 0 and fraction < 1 and not start_solid and nz >= 0.7 and abs(s.z - ez) > 0.5 / 32 then
				s.x, s.y, s.z = ex, ey, ez
			end
		end

		-- StepMove: the better of moving straight and moving up a step, across and back down
		local function step_move(dest, first)
			local x0, y0, z0, vx0, vy0, vz0 = s.x, s.y, s.z, s.vx, s.vy, s.vz
			try_player_move(dest, first)
			local dx, dy, dz, dvx, dvy, dvz = s.x, s.y, s.z, s.vx, s.vy, s.vz

			s.x, s.y, s.z, s.vx, s.vy, s.vz = x0, y0, z0, vx0, vy0, vz0
			local _, _, _, _, ex, ey, ez, start_solid, all_solid = box(s.x, s.y, s.z, s.x, s.y, s.z + s.step + DIST_EPSILON)
			if not start_solid and not all_solid then s.x, s.y, s.z = ex, ey, ez end
			try_player_move()

			local _, _, _, nz
			_, _, _, nz, ex, ey, ez, start_solid, all_solid = box(s.x, s.y, s.z, s.x, s.y, s.z - s.step - DIST_EPSILON)
			if nz < 0.7 then
				s.x, s.y, s.z, s.vx, s.vy, s.vz = dx, dy, dz, dvx, dvy, dvz
				return
			end
			if not start_solid and not all_solid then s.x, s.y, s.z = ex, ey, ez end
			if (dx - x0) ^ 2 + (dy - y0) ^ 2 > (s.x - x0) ^ 2 + (s.y - y0) ^ 2 then
				s.x, s.y, s.z, s.vx, s.vy, s.vz = dx, dy, dz, dvx, dvy, dvz
			else
				s.vz = dvz
			end
		end

		-- the wish direction and speed from the keys, flat, and capped at the move's max speed
		local function wish()
			local cy, sy = cos(rad(s.yaw)), sin(rad(s.yaw))
			local wx, wy = cy * s.forward + sy * s.side, sy * s.forward - cy * s.side
			local speed = sqrt(wx * wx + wy * wy)
			if speed == 0 then return 0, 0, 0 end
			local dirx, diry = wx / speed, wy / speed
			return dirx, diry, min(speed, s.max_speed)
		end

		-- CCSGameMovement::Accelerate, with SV_ACCELERATE_EXPONENT_TIME 0 as shipped
		local function accelerate(dirx, diry, speed, accel)
			local current = s.vx * dirx + s.vy * diry
			local add = speed - current
			if add <= 0 then return end
			current = max(0, current)
			local walking = s.walk and not s.ducking
			local scale = max(250, speed)
			local goal = scale
			if cvar_float("sv_accelerate_use_weapon_speed", 1) ~= 0 then
				goal = goal * min(1, s.weapon_speed / 250)
				if not s.ducking and not walking then scale = scale * min(1, s.weapon_speed / 250) end
			end
			if s.ducking then scale, goal = scale * DUCK, goal * DUCK end
			if walking then scale, goal = scale * WALK, goal * WALK end
			if walking and current > goal - 5 then
				accel = accel * max(0, min(1, 1 - max(0, current - (goal - 5)) / 5))
			end
			local amount = min(add, accel * s.dt * scale * s.surface_friction)
			s.vx, s.vy = s.vx + amount * dirx, s.vy + amount * diry
		end

		local function friction()
			local speed = sqrt(s.vx * s.vx + s.vy * s.vy + s.vz * s.vz)
			if speed < 0.1 then return end
			local control = max(speed, cvar_float("sv_stopspeed", 80))
			local drop = control * cvar_float("sv_friction", 5.2) * s.surface_friction * s.dt
			local scale = max(0, speed - drop) / speed
			s.vx, s.vy, s.vz = s.vx * scale, s.vy * scale, s.vz * scale
		end

		local function walk_move()
			local dirx, diry, speed = wish()
			s.vz = 0
			accelerate(dirx, diry, speed, cvar_float("sv_accelerate", 5.5))
			s.vz = 0
			local length = sqrt(s.vx * s.vx + s.vy * s.vy)
			if length > s.max_speed then s.vx, s.vy = s.vx * s.max_speed / length, s.vy * s.max_speed / length end
			if sqrt(s.vx * s.vx + s.vy * s.vy) < 1 then
				s.vx, s.vy, s.vz = 0, 0, 0
				return
			end
			local dest = {s.x + s.vx * s.dt, s.y + s.vy * s.dt, s.z}
			local first = {box(s.x, s.y, s.z, dest[1], dest[2], dest[3])}
			if first[1] == 1 then
				s.x, s.y, s.z = first[5], first[6], first[7]
				stay_on_ground()
				return
			end
			step_move(dest, first)
			stay_on_ground()
		end

		local function air_move()
			local dirx, diry, speed = wish()
			-- AirAccelerate: the wish is capped at 30 units/s, the acceleration is not
			local add = min(speed, 30) - (s.vx * dirx + s.vy * diry)
			if add > 0 then
				local amount = min(add, cvar_float("sv_airaccelerate", 12) * speed * s.dt * s.surface_friction)
				s.vx, s.vy = s.vx + amount * dirx, s.vy + amount * diry
			end
			try_player_move()
		end

		-- CategorizePosition: on the ground or not, snapping down onto it
		local function categorize()
			s.surface_friction = 1
			if s.vz > 140 then
				s.ground = false
				return
			end
			local reach = 2 + (s.ground and s.step or 0)
			local fraction, _, _, nz, ex, ey, ez, start_solid = box(s.x, s.y, s.z, s.x, s.y, s.z - reach)
			local snap = s.ground
			if fraction < 1 and nz >= 0.7 then
				s.ground, s.vz = true, 0   -- SetGroundEntity
			else
				s.ground = false
				if s.vz > 0 then s.surface_friction = 0.25 end
				snap = false
			end
			if snap and not start_solid and fraction > 0 and fraction < 1 then s.x, s.y, s.z = ex, ey, ez end
		end

		-- CCSGameMovement::CheckJumpButton on the ground
		local function jump()
			if s.jumped and not s.auto_jump then return end
			if cvar_float("sv_enablebunnyhopping", 0) == 0 then
				-- PreventBunnyJumping
				local limit = 1.1 * s.weapon_speed
				local speed = sqrt(s.vx * s.vx + s.vy * s.vy + s.vz * s.vz)
				if limit > 0 and speed > limit then
					s.vx, s.vy, s.vz = s.vx * limit / speed, s.vy * limit / speed, s.vz * limit / speed
				end
			end
			s.ground = false
			local start = s.vz
			local impulse = cvar_float("sv_jump_impulse", 301.993377)
			-- ducked or on the way down (m_bDucking): the impulse replaces the vertical speed
			if s.ducked or s.duck_amount > 0 then s.vz = impulse else s.vz = s.vz + impulse end
			if s.stamina > 0 then s.vz = s.vz * max(0, min(1, 1 - s.stamina / STAMINA_RANGE)) end
			s.vz = s.vz - s.gravity * s.dt * 0.5   -- FinishGravity
			s.stamina = max(0, min(cvar_float("sv_staminamax", 80), s.stamina + cvar_float("sv_staminajumpcost", 0.08) * (s.vz - start)))
			s.jumped = true
		end

		-- one tick of CCSGameMovement::PlayerMove for MOVETYPE_WALK
		local function tick()
			-- CheckParameters
			local max_speed = s.weapon_speed
			if s.walk and not s.ducking and sqrt(s.vx * s.vx + s.vy * s.vy + s.vz * s.vz) < max_speed * WALK + 25 then
				max_speed = max_speed * WALK
			end
			if s.ground then max_speed = max_speed * s.velocity_modifier end
			if s.stamina > 0 then max_speed = max_speed * max(0, min(1, 1 - s.stamina / STAMINA_RANGE)) ^ 2 end
			s.forward, s.side = s.keys_forward, s.keys_side
			local wanted = sqrt(s.forward * s.forward + s.side * s.side)
			if wanted ~= 0 and wanted > max_speed then
				s.forward, s.side = s.forward * max_speed / wanted, s.side * max_speed / wanted
			end
			s.max_speed = max_speed

			-- ReduceTimers
			if s.stamina > 0 then s.stamina = max(0, s.stamina - s.dt * cvar_float("sv_staminarecoveryrate", 60)) end

			-- sv_optimizedmovement: only a fast rise takes the player off the ground here
			if s.vz > 250 then s.ground = false end
			if not s.ground then s.fall = -s.vz end

			-- Duck, ducked throughout: HandleDuckingSpeedCrop
			if s.ducking then
				local crop = DUCK * s.duck_amount + 1 - s.duck_amount
				s.forward, s.side, s.max_speed = s.forward * crop, s.side * crop, s.max_speed * crop
			end

			-- FullWalkMove
			s.vz = s.vz - s.gravity * 0.5 * s.dt   -- StartGravity
			check_velocity()
			if s.jump then
				if s.ground then jump() else s.jumped = true end
			else
				s.jumped = false
			end
			if s.ground then
				s.vz, s.fall = 0, 0
				friction()
			end
			check_velocity()
			if s.ground then walk_move() else air_move() end
			categorize()
			check_velocity()
			s.vz = s.vz - s.gravity * s.dt * 0.5   -- FinishGravity
			if s.ground then s.vz = 0 end
			-- CheckFalling: landing costs stamina
			if s.ground and s.fall > 0 then
				s.stamina = max(0, min(cvar_float("sv_staminamax", 80), s.stamina + cvar_float("sv_staminalandcost", 0.05) * s.fall))
				s.fall = 0
			end
			if s.ground and s.velocity_modifier < 1 then s.velocity_modifier = min(1, s.velocity_modifier + s.dt / 2.5) end
		end

		return function(player, weapon)
			local ex, ey, ez = client_eye_position()
			local vx, vy, vz = entity_get_prop(player, "m_vecVelocity")
			local _, yaw = client_camera_angles()
			if not ex or not vx or not yaw then return end
			local view = entity_get_prop(player, "m_vecViewOffset[2]") or 64

			local dt = globals_tickinterval()
			local ticks = floor(0.1 / dt) + 2   -- the tick letting go, then the wait
			local started = entity_get_prop(weapon, "m_fThrowTime") or 0
			if started > 0 then ticks = max(0, floor((started - globals.curtime()) / dt) + 1) end

			local flags = entity_get_prop(player, "m_fFlags") or FL_ONGROUND
			local duck_amount = entity_get_prop(player, "m_flDuckAmount") or 0
			s = {
				x = ex, y = ey, z = ez - view, vx = vx, vy = vy, vz = vz, dt = dt, yaw = yaw,
				ground = band(flags, FL_ONGROUND) ~= 0,
				ducked = band(flags, FL_DUCKING) ~= 0,
				ducking = keys.duck or band(flags, FL_DUCKING) ~= 0 or duck_amount > 0,
				duck_amount = duck_amount,
				keys_forward = keys.forward, keys_side = keys.side, jump = keys.jump, walk = keys.walk,
				jumped = keys.jump,   -- the latest command's jump counts as held already
				auto_jump = cvar_float("sv_autobunnyhopping", 0) ~= 0 or (bunny_hop ~= nil and ui_get(bunny_hop)),
				weapon_speed = entity_get_prop(player, "m_flMaxspeed") or 250,
				velocity_modifier = entity_get_prop(player, "m_flVelocityModifier") or 1,
				stamina = entity_get_prop(player, "m_flStamina") or 0,
				fall = entity_get_prop(player, "m_flFallVelocity") or 0,
				step = entity_get_prop(player, "m_flStepSize") or 18,
				gravity = cvar_float("sv_gravity", 800),
				surface_friction = 1,
			}
			if not trace.enabled then
				-- no box traces without the engine's: steady movement instead
				local t = ticks * dt
				return ex + vx * t, ey + vy * t, ez + vz * t, vx, vy, vz
			end

			tall = s.ducked and CROUCH or STAND
			reset_ignored()
			trace_owner = player   -- the player's own box is not in the way
			local filter = trace.entity_filter_data
			if filter then filter[2] = COLLISION_GROUP_PLAYER_MOVEMENT end
			-- the filter and owner are shared with the grenade simulations: put back even if a tick fails
			local ok, err = pcall(function() for _ = 1, ticks do tick() end end)
			if filter then filter[2] = COLLISION_GROUP_PROJECTILE end
			trace_owner = 0
			reset_ignored()
			if not ok then error(err, 0) end
			return s.x, s.y, s.z + view, s.vx, s.vy, s.vz
		end
	end)()

	-- CBaseCSGrenade::ThrowGrenade: where the projectile starts and its velocity, from throw_inputs' eye, view and
	-- velocity at the moment of the throw.
	local function throw(player, weapon, inputs)
		local ex, ey, ez, pitch, yaw, vx, vy, vz = unpack(inputs, 1, 8)

		local strength = max(0, min(1, entity_get_prop(weapon, "m_flThrowStrength") or 1))

		-- thrown 10 degrees above the view at the horizon, less towards straight up or down
		if pitch > 90 then pitch = pitch - 360 elseif pitch < -90 then pitch = pitch + 360 end
		pitch = pitch - 10 * (90 - abs(pitch)) / 90

		local cp, sp, cy, sy = cos(rad(pitch)), sin(rad(pitch)), cos(rad(yaw)), sin(rad(yaw))
		local dx, dy, dz = cp * cy, cp * sy, -sp
		local speed = max(15, min(750, THROW_VELOCITY * 0.9)) * (THROW_DAMPENING + (1 - THROW_DAMPENING) * strength)

		-- the thrower's own velocity carries over
		local ix, iy, iz = (vx or 0) * 1.25, (vy or 0) * 1.25, (vz or 0) * 1.25

		if super_toss_active() then
			-- throw in the direction that ends up along the view once the inherited velocity is added:
			-- speed * direction + inherited = k * view, with |direction| = 1
			local along = dx * ix + dy * iy + dz * iz
			local discriminant = along * along - (ix * ix + iy * iy + iz * iz) + speed * speed
			if discriminant >= 0 then
				local k = along + sqrt(discriminant)
				dx, dy, dz = (k * dx - ix) / speed, (k * dy - iy) / speed, (k * dz - iz) / speed
			end
		end

		-- released 16 units out: a hull trace over 22 units, pulled back by 6. Weak throws start lower.
		local x, y, z = ex, ey, ez + (-THROW_LOWER + THROW_LOWER * strength)
		trace_ignore = player
		reset_ignored()
		local fraction
		if trace.enabled then
			-- the game's trace skips the thrower; from the eye, it would otherwise stop inside the player at once
			trace_owner = player
			fraction = trace_solid(x, y, z, dx * 22, dy * 22, dz * 22, MASK.THROW, GRENADE_SIZE)
			trace_owner = 0
		else
			fraction = sweep_rays(x, y, z, dx * 22, dy * 22, dz * 22)
		end
		x, y, z = x + dx * 22 * fraction - dx * 6, y + dy * 22 * fraction - dy * 6, z + dz * 22 * fraction - dz * 6

		return x, y, z, dx * speed + ix, dy * speed + iy, dz * speed + iz
	end

	-- What a throw estimate depends on, and how much each may change before the prediction is redone. Below these
	-- the result would only shake: a slightly different start can tip a bounce off an edge one way or the other.
	local function throw_inputs(player, weapon)
		local ex, ey, ez, vx, vy, vz = throw_origin(player, weapon)
		local pitch, yaw = client_camera_angles()
		if not ex or not pitch then return end
		return {ex, ey, ez, pitch, yaw, vx, vy, vz, entity_get_prop(weapon, "m_flThrowStrength") or 1,
			super_toss_active() and 1 or 0, globals_tickinterval()}
	end

	-- Anti-aim's micro-movement swings the velocity between about -1 and +1 every tick, hence 2.5 units/s.
	local THROW_TOLERANCE = {0.25, 0.25, 0.25, 0.02, 0.02, 2.5, 2.5, 2.5, 0.01, 0, 0}
	local function throw_moved(old, new)
		if not old then return true end
		for i = 1, #new do
			if abs(new[i] - old[i]) > THROW_TOLERANCE[i] then return true end
		end
		return false
	end

	local EFFECT      = "molotov_child_flame01b"
	local EFFECT_LIFE = 12   -- seconds; the effect is replaced before it burns out by itself

	local enabled = ui.new_checkbox("VISUALS", "Other ESP", "Trajectory effect")
	local line_enabled = ui.new_checkbox("VISUALS", "Other ESP", "Grenade trajectory glow")
	-- colour pickers sit beside the item before them, hence the labels
	local line_colors = {
		ui.new_color_picker("VISUALS", "Other ESP", "Grenade trajectory glow color", 255, 50, 50, 179),
		ui.new_label("VISUALS", "Other ESP", "Trajectory hit"),
		ui.new_color_picker("VISUALS", "Other ESP", "Trajectory hit color", 150, 200, 59, 255),
	}
	local primed_only = ui.new_checkbox("VISUALS", "Other ESP", "Pin pulled only")

	-- gamesense's own line, which ours replaces: switched off whenever ours is on, and back on when the script is
	-- unloaded if it was this script that switched it off
	local builtin_line, builtin_line_restore
	do
		local found, reference = pcall(ui.reference, "VISUALS", "Other ESP", "Grenade trajectory")
		if found then builtin_line = reference end
	end

	local function update_menu()
		ui_set_visible(primed_only, ui_get(enabled) or ui_get(line_enabled))
		for i = 2, #line_colors do ui_set_visible(line_colors[i], ui_get(line_enabled)) end
		if builtin_line and ui_get(line_enabled) and ui_get(builtin_line) then
			ui_set(builtin_line, false)
			builtin_line_restore = true
		end
	end
	client.set_event_callback("shutdown", function()
		if builtin_line_restore and builtin_line then ui_set(builtin_line, true) end
	end)
	update_menu()
	ui.set_callback(enabled, update_menu)
	ui.set_callback(line_enabled, update_menu)

	-- The held grenade's flight: path holds x, y, z of every tick and bounce, hits the bounces, x, y, z where it
	-- goes off (for a molotov, where its fire starts; nil if it never does). Only recomputed when the throw
	-- changes, or for a smoke when a flame it could land by goes out or catches.
	local prediction = {}

	local function predict(player, weapon, kind)
		local inputs = throw_inputs(player, weapon)
		if not inputs then return end

		check_trace(player)

		-- only a smoke cares about fires: it can land by one, and putting one out is its hit
		local flames, starts, fire_key = nil, nil, 0
		if kind == "smoke" then
			flames, starts = effects.fires()
			fire_key = #flames
			for _, flame in ipairs(flames) do fire_key = fire_key + (flame[1] + flame[2] * 3 + flame[3] * 7) * 1e-3 end
		end
		if kind ~= prediction.kind or fire_key ~= prediction.fire_key or throw_moved(prediction.inputs, inputs) then
			prediction = {inputs = inputs, kind = kind, fire_key = fire_key}
			local x, y, z, tx, ty, tz = throw(player, weapon, inputs)
			if x then
				prediction.path, prediction.hits = {}, {}
				prediction.x, prediction.y, prediction.z, prediction.time = simulate(x, y, z, tx, ty, tz, kind, 0, player, prediction.path,
					prediction.hits, flames)
			end
		end
		prediction.flames, prediction.starts = flames, starts
		return prediction
	end

	-- The prediction for the grenade in hand, if there is one to show.
	local function held_grenade()
		local player = entity.get_local_player()
		if not player or not entity.is_alive(player) then return end

		local weapon = entity.get_player_weapon(player)
		if not weapon then return end

		local kind = ITEM_KIND[band(entity_get_prop(weapon, "m_iItemDefinitionIndex") or 0, 0xFFFF)]
		if not kind then return end
		if ui_get(primed_only) and entity_get_prop(weapon, "m_bPinPulled") ~= 1 then return end

		return predict(player, weapon, kind)
	end

	-- Draws the prediction: the line, a dot on every bounce and one where it goes off, in the hit colours when it
	-- does what it is thrown for, with a word on it above the dot. A function, so that its locals do not count
	-- towards the main chunk's 200.
	local draw_trajectory = (function()
		local LINE_GLOW  = 8     -- pixels across the glow, at 1080p
		local GLOW_SHARE = 0.35  -- the glow's opacity, as a share of the line's
		local DOT_RADIUS = 2     -- pixels at 1080p; the black shadow behind a dot is a pixel wider
		local DETONATION, DETONATION_HIT = {255, 0, 40}, {0, 255, 40}
		local FOOT = 16
		local FIRE_REACH = 14.5 * FOOT   -- molotov: a hit with an enemy this close to where the fire starts,
		local FIRE_CLOSE = 12.5 * FOOT   -- and green text closer than this
		local HE_GOOD    = 50            -- HE: green text from this much damage, or from less that kills
		local OUTCOME_INTERVAL = 0.05    -- seconds between checks of what the throw does to the enemies
		local screen_points = {}
		local last = {time = -huge}      -- the latest outcome, and the prediction it was for

		-- Whether the throw does its job where it goes off, and if so the text for it and whether that is green:
		--   molotov  an enemy within FIRE_REACH of the fire, if a smoke does not keep it from starting; the
		--            nearest one's distance, green inside FIRE_CLOSE
		--   he       damage to an enemy; the most, green from HE_GOOD or from less that kills, a kill first
		--   flash    an enemy blinded; the most blinded one's share of a full flash
		--   smoke    a burning flame put out; no text
		local function outcome(player, held)
			local kind, x, y, z = held.kind, held.x, held.y, held.z
			if kind == "smoke" then return effects.extinguishes(x, y, z, held.flames, held.starts) end
			if kind == "decoy" then return false end
			if kind == "molotov" and effects.smothered(x, y, z, held.time or 0) then return false end

			local best, green
			for _, enemy in ipairs(entity.get_players(true)) do
				if kind == "molotov" then
					local ox, oy, oz = entity_get_prop(enemy, "m_vecOrigin")
					local distance = ox and sqrt((ox - x) ^ 2 + (oy - y) ^ 2 + (oz - z) ^ 2)
					if distance and distance <= FIRE_REACH and (not best or distance < best) then best = distance end
				elseif kind == "he" then
					local ex, ey, ez, _, yaw = effects.eye(enemy)
					local damage = ex and effects.blast_damage(enemy, x, y, z, effects.HE_DAMAGE, effects.HE_RADIUS,
						damage_scale("he", player, enemy), ex, ey, ez, yaw)
					if damage and damage >= 1 then
						local kills = damage >= min(HE_GOOD, entity_get_prop(enemy, "m_iHealth") or 100)
						if not best or (kills and not green) or (kills == green and damage > best) then best, green = damage, kills end
					end
				else
					local share = effects.flash(enemy, x, y, z)
					if share > 0 and (not best or share > best) then best = share end
				end
			end
			if not best then return false end
			if kind == "molotov" then return true, string.format("%.1f ft", best / FOOT), best < FIRE_CLOSE end
			if kind == "he" then return true, string.format("%d dmg", best), green end
			return true, string.format("%d%%", floor(best * 100 + 0.5)), false
		end

		local function draw_dot(x, y, radius, r, g, b, a)
			surface_draw.dot(x, y, radius + 1, 0, 0, 0, a)
			surface_draw.dot(x, y, radius, r, g, b, a)
		end

		return function(player, held, colors)
			local path = held.path
			-- the outcome traces against every enemy, so it is redone every OUTCOME_INTERVAL, not every frame,
			-- and at once for a new prediction (a new table)
			local now = globals_realtime()
			if held ~= last.held or now - last.time >= OUTCOME_INTERVAL then
				last.held, last.time = held, now
				last.hit, last.text, last.green = outcome(player, held)
			end
			local hit, text, green = last.hit, last.text, last.green
			local r, g, b, a = ui_get(colors[hit and 3 or 1])
			local width, height = client.screen_size()
			local scale = height / 1080
			local glow = LINE_GLOW * scale
			local mid = 3 * floor(#path / 6) + 1   -- checked on a point away from the thrower's eye
			local to_screen = screen_projector(width, height, path[mid], path[mid + 1], path[mid + 2])

			-- the line, each stretch in front of the camera on its own
			local count = 0
			local function flush()
				if count >= 2 then
					surface_draw.line(screen_points, count, glow, r, g, b, floor(a * GLOW_SHARE), "glow")
					surface_draw.line(screen_points, count, 1, r, g, b, a, "solid")
				end
				count = 0
			end
			for i = 1, #path, 3 do
				local sx, sy = to_screen(path[i], path[i + 1], path[i + 2])
				if sx then
					-- a grenade at rest repeats its position, which would make empty quads
					if count == 0 or abs(sx - screen_points[2 * count - 1]) + abs(sy - screen_points[2 * count]) > 0.01 then
						count = count + 1
						screen_points[2 * count - 1], screen_points[2 * count] = sx, sy
					end
				else
					flush()
				end
			end
			flush()

			local radius = DOT_RADIUS * scale
			for _, bounce in ipairs(held.hits) do
				local x, y = to_screen(bounce[1], bounce[2], bounce[3])
				if x then draw_dot(x, y, radius, 255, 255, 255, 128) end
			end

			-- where it goes off: a molotov's fire on the ground below an air burst, an HE's blast moved onto the
			-- ground under it, a smoke dropped by a fire it bounced near
			local x, y = to_screen(held.x, held.y, held.z)
			if x then
				local color = hit and DETONATION_HIT or DETONATION
				draw_dot(x, y, radius, color[1], color[2], color[3], 255)
				if text then
					local tr, tg, tb = 255, 255, 255
					if green then tr, tg, tb = ui_get(colors[3]) end
					renderer_text(x, y - radius - 10 * scale, tr, tg, tb, 255, "c", 0, text)
				end
			end
		end
	end)()

	local warned = false

	-- The particle system definition for a name, looked up the way the game's own effect code does it.
	local function find_system(name)
		local manager = ffi.cast("void **", particles.manager)[0]
		if manager == nil then return end

		if particles.is_cached(manager, name) == 0 then
			if not warned then
				warned = true
				log(255, 180, 80, "particle system '" .. name .. "' is not loaded on this map")
			end
			return
		end

		local base = address(manager)
		local index = ffi.new("uint16_t[1]", 0xFFFF)
		particles.find_index(ffi.cast("void *", base + 0x14), index, name)
		if index[0] == 0xFFFF or index[0] >= i32(base + 0xC) then return end

		local system = u32(u32(base) + index[0] * 4)
		if system ~= 0 then return ffi.cast("void *", system) end
	end

	local position = ffi.new("float[3]")
	local FORWARD, RIGHT, UP = ffi.new("float[3]", 1, 0, 0), ffi.new("float[3]", 0, -1, 0), ffi.new("float[3]", 0, 0, 1)

	-- The effect on screen: the CNewParticleEffect's address, plus its vtable to recognise it by. The game
	-- frees effects on its own (a map change, for one), so the handle is checked before every use.
	local effect

	-- The game frees an effect the frame after flagging it for removal, and freed memory keeps its old contents,
	-- so the vtable and owner alone would pass for an effect that is gone. The remove flag catches it while it
	-- is on its way out; once it is set the handle is dropped for good.
	local EFFECT_FLAGS, FLAG_REMOVE = 0x3B0, 0x02

	local function alive(handle)
		if handle.gone then return false end
		local live = u32(handle.address) == handle.vtable
			and i32(handle.address + 0x4E0) == -1   -- the owner id it was created with
			and band(ffi.cast("uint8_t *", handle.address + EFFECT_FLAGS)[0], FLAG_REMOVE) == 0
		if not live then handle.gone = true end
		return live
	end

	local function spawn(x, y, z)
		local system = find_system(EFFECT)
		if not system then return end

		position[0], position[1], position[2] = x, y, z
		local created = particles.create(nil, system, position, 0, 0, -1)
		if created == nil then return end

		local addr = address(created)
		local origin = ffi.cast("float *", addr + 0x3B8)
		origin[0], origin[1], origin[2] = x, y, z
		particles.set_control_point(created, 0, position)
		particles.set_control_point(created, 1, position)
		particles.set_orientation(ffi.cast("void *", addr + 0x10), 0, FORWARD, RIGHT, UP)   -- its CParticleCollection

		return {address = addr, vtable = u32(addr), x = x, y = y, z = z, spawned = globals.curtime()}
	end
	if particles.ready then jit.off(spawn) end   -- see init_particles: the create call must only ever run interpreted

	local function move(handle, x, y, z)
		if abs(handle.x - x) + abs(handle.y - y) + abs(handle.z - z) < 0.5 then return end
		handle.x, handle.y, handle.z = x, y, z

		position[0], position[1], position[2] = x, y, z
		local origin = ffi.cast("float *", handle.address + 0x3B8)
		origin[0], origin[1], origin[2] = x, y, z

		local pointer = ffi.cast("void *", handle.address)
		particles.set_control_point(pointer, 0, position)
		particles.set_control_point(pointer, 1, position)
	end

	local function stop(handle)
		if handle and particles.ready and alive(handle) then
			-- only the emitters stop, so the flames already burning die down instead of vanishing
			particles.stop_emission(ffi.cast("void *", handle.address + 0x10), 0, 0, 1, 0)
		end
	end

	local function clear()
		local handle = effect
		effect = nil
		stop(handle)
	end

	-- Starts the new effect before stopping the old one, so the fire never goes out in between.
	local function replace(x, y, z)
		local fresh = spawn(x, y, z)
		if fresh then
			stop(effect)
			effect = fresh
		end
	end

	local function on_paint()
		local effect_on = particles.ready and ui_get(enabled)
		local line_on = surface_draw and ui_get(line_enabled)
		local held = (effect_on or line_on) and held_grenade()

		-- a molotov that never reaches the ground goes off in the air without a fire: nothing to show
		local shown = held and held.path and held.x
		if line_on and shown then draw_trajectory(entity.get_local_player(), held, line_colors) end

		local x, y, z
		-- no fire either where a smoke keeps it from starting
		if effect_on and held and held.kind == "molotov" and held.x and not effects.smothered(held.x, held.y, held.z, held.time or 0) then
			x, y, z = held.x, held.y, held.z
		end
		if not x then return clear() end

		if effect and not alive(effect) then effect = nil end

		if not effect then
			effect = spawn(x, y, z)
		elseif globals.curtime() - effect.spawned >= EFFECT_LIFE then
			replace(x, y, z)
		else
			move(effect, x, y, z)
		end
	end

	local function forget()
		effect = nil
	end

	client.set_event_callback("paint", guarded(on_paint))
	client.set_event_callback("shutdown", clear)

	-- every effect is gone after these, so the handle is dropped without being touched
	client.set_event_callback("level_init", forget)
	client.set_event_callback("cs_game_disconnected", forget)
end

--------------------------------------------------------------------------------
-- Neverlose shapes
--------------------------------------------------------------------------------

local ICON_SVG   -- the game's grenade icons, at the end of the file

-- The marker's look, and the textures it is drawn from. Built once per size and cached.
local PIN, icon, pin_body, timer_shape
do
	PIN = {
		RADIUS       = 29,                  -- pixels at 1080p, border included
		BORDER       = 3,
		STALK        = 14,                  -- height of the pointer under the marker
		ICON         = 26,                  -- icon height on its own
		ICON_SMALL   = 15,                  -- icon height under a damage number
		TIMER_WIDTH  = 4,
		TIMER_START  = 135,                 -- degrees, clockwise from the right: the fixed end, low on the left
		TIMER_SPAN   = 270,                 -- degrees it covers when full
		BORDER_COLOR = {34, 34, 34, 204},   -- 80% opacity
		FACE_COLOR   = {22, 22, 22, 235},
		DANGER_COLOR = {190, 18, 18},
		CALM_COLOR   = {60, 105, 160},      -- the glow when the grenade cannot hurt
		FADE_TIME    = 0.3,                 -- seconds a marker takes to fade in or out
		SHRINK_TIME  = 0.2,                 -- seconds the icon takes to make room for the damage
		GLOW_STEPS   = 10,                  -- circles the glow's falloff is built from
		GLOW_LEVELS  = 20,                  -- strengths it is cached at, from harmless to full danger
		TURN_STEP    = 3,                   -- degrees between the textures of a turned marker
		JOIN         = 38,                  -- degrees either side of straight down where the stalk leaves the circle
		TIP_ROUNDING = 0.09,                -- radius of the rounded tip, as a share of the marker's
	}

	local icons = {}   -- textures by kind and height

	-- An icon texture and its width, for a height in pixels.
	function icon(kind, height)
		local svg = ICON_SVG[kind] or ICON_SVG.he
		local width = floor(height * svg[1] + 0.5)
		local key = kind .. height
		if icons[key] == nil then icons[key] = renderer.load_svg(svg[2], width, height) or false end
		return icons[key] or nil, width
	end

	-- Shapes drawn as white SVGs the renderer tints: it draws triangles without anti-aliasing, which turns a
	-- few pixels of curve into stair steps, while SVGs are rasterised smooth. Paths use only M, C and Z, the
	-- commands the game's own icons use, so the SVG loader is not asked for anything they do not need.
	local svg_failed = false

	local function load_shape(path, width, height)
		local svg = string.format('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">'
			.. '<path fill="#fff" d="%s"/></svg>', width, height, width, height, path)
		local texture = renderer.load_svg(svg, width, height)
		if not texture and not svg_failed then
			svg_failed = true
			log(255, 180, 80, "the Neverlose shapes failed to load, falling back to plain circles")
		end
		return texture or false
	end

	local function point(x, y) return string.format("%.2f %.2f", x, y) end

	-- Cubic segments along a circle from angle a0 to a1, in degrees clockwise from the right. Either way round.
	local function arc_to(parts, cx, cy, r, a0, a1)
		local segments = max(1, ceil(abs(a1 - a0) / 90))
		local step = (a1 - a0) / segments
		local k = 4 / 3 * math.tan(rad(step) / 4) * r
		for i = 0, segments - 1 do
			local s0, s1 = rad(a0 + step * i), rad(a0 + step * (i + 1))
			local x0, y0 = cx + cos(s0) * r, cy + sin(s0) * r
			local x1, y1 = cx + cos(s1) * r, cy + sin(s1) * r
			parts[#parts + 1] = "C" .. point(x0 - k * sin(s0), y0 + k * cos(s0)) .. " "
				.. point(x1 + k * sin(s1), y1 - k * cos(s1)) .. " " .. point(x1, y1)
		end
	end

	-- a quadratic curve as the cubic it equals
	local function quad_to(parts, x0, y0, qx, qy, x1, y1)
		parts[#parts + 1] = "C" .. point(x0 + (qx - x0) * 2 / 3, y0 + (qy - y0) * 2 / 3) .. " "
			.. point(x1 + (qx - x1) * 2 / 3, y1 + (qy - y1) * 2 / 3) .. " " .. point(x1, y1)
	end

	-- The marker's body, circle and stalk in one, its stalk turned `rotation` degrees clockwise from straight
	-- down. The stalk's sides leave the circle along its tangent, then run nearly straight to the tip. The
	-- texture is a square of 2 * (radius + stalk) around the circle's centre. One per size and rotation.
	local bodies = {}

	function pin_body(radius, stalk, rotation)
		local key = radius .. "x" .. stalk .. "x" .. rotation
		if bodies[key] == nil then
			local half = radius + stalk
			local turn_cos, turn_sin = cos(rad(rotation)), sin(rad(rotation))
			-- built around the centre with the stalk down, then turned and moved into the square
			local function place(x, y)
				return half + x * turn_cos - y * turn_sin, half + x * turn_sin + y * turn_cos
			end
			local tip = radius + stalk
			local function join(side)
				local angle = rad(90 - side * PIN.JOIN)
				local px, py = cos(angle) * radius, sin(angle) * radius
				local tx, ty = -sin(angle) * side, cos(angle) * side
				local reach = sqrt(px ^ 2 + (tip - py) ^ 2) * 0.3
				return px, py, px + tx * reach, py + ty * reach
			end
			local rx, ry, rqx, rqy = join(1)
			local lx, ly, lqx, lqy = join(-1)
			-- the sides end on a small circle at the tip, which rounds it off
			local cap = max(1.5, radius * PIN.TIP_ROUNDING)
			local cap_x, cap_y = 0, tip - cap
			local Rx, Ry = place(rx, ry)
			local Rqx, Rqy = place(rqx, rqy)
			local Ax, Ay = place(cap_x + cos(rad(40)) * cap, cap_y + sin(rad(40)) * cap)
			local Cx, Cy = place(cap_x, cap_y)
			local Bx, By = place(cap_x + cos(rad(140)) * cap, cap_y + sin(rad(140)) * cap)
			local Lqx, Lqy = place(lqx, lqy)
			local Lx, Ly = place(lx, ly)
			local parts = {"M" .. point(Rx, Ry)}
			quad_to(parts, Rx, Ry, Rqx, Rqy, Ax, Ay)
			arc_to(parts, Cx, Cy, cap, 40 + rotation, 140 + rotation)
			quad_to(parts, Bx, By, Lqx, Lqy, Lx, Ly)
			arc_to(parts, half, half, radius, 90 + PIN.JOIN + rotation, 450 - PIN.JOIN + rotation)
			parts[#parts + 1] = "Z"
			bodies[key] = load_shape(table.concat(parts, " "), 2 * half, 2 * half)
		end
		return bodies[key] or nil
	end

	-- The timer: a band along the inside of the face from `start` round `span` degrees, with round ends. Cached
	-- per whole degree, so it is drawn in steps of one degree.
	local timers = {}

	function timer_shape(face, width, start, span)
		local key = face .. "x" .. width .. "x" .. start .. "x" .. span
		if timers[key] == nil then
			local outer, inner, middle, cap = face, face - width, face - width / 2, width / 2
			local finish = start + span
			local parts = {"M" .. point(face + cos(rad(start)) * outer, face + sin(rad(start)) * outer)}
			arc_to(parts, face, face, outer, start, finish)
			arc_to(parts, face + cos(rad(finish)) * middle, face + sin(rad(finish)) * middle, cap, finish, finish + 180)
			arc_to(parts, face, face, inner, finish, start)
			arc_to(parts, face + cos(rad(start)) * middle, face + sin(rad(start)) * middle, cap, start + 180, start + 360)
			parts[#parts + 1] = "Z"
			timers[key] = load_shape(table.concat(parts, " "), 2 * face, 2 * face)
		end
		return timers[key] or nil
	end
end

-- Paint callbacks run in the order they are registered, and the molotov area goes under the warnings: its
-- callback is registered here, ahead of theirs, though the area itself is defined further down.
local paint_molotov_area
client.set_event_callback("paint", function()
	if paint_molotov_area then paint_molotov_area() end
end)

--------------------------------------------------------------------------------
-- Grenade proximity warning
--------------------------------------------------------------------------------

-- A function rather than a do block: its locals then do not count towards the main chunk's 200.
local function proximity_warning()
	local NAME = "Grenade proximity warning"

	local FOOT            = 16         -- units
	local RANGE           = 30 * FOOT
	local MAX_WARNINGS    = 4
	local OPACITY         = 0.9
	local DANGER_OPACITY  = 0.8        -- the red layer at full danger
	local RADIUS          = 30         -- pixels at 1080p, without the timer ring
	local RADIUS_GROWTH   = 1.5        -- at full danger
	local PATH_TOLERANCE  = 20         -- units a grenade may stray from its predicted path
	local RESIMULATE_GAP  = 0.1        -- seconds
	local TRACK_MEMORY    = 3          -- game seconds a grenade out of view is remembered
	local DAMAGE_INTERVAL = 0.05       -- seconds

	local HE_DAMAGE, HE_RADIUS, HE_MIN_RADIUS = effects.HE_DAMAGE, effects.HE_RADIUS, effects.HE_MIN_RADIUS
	local blast_damage = effects.blast_damage

	local EF_NODRAW   = 32             -- set on a grenade once it has exploded
	local PROJECTILES = {"CBaseCSGrenadeProjectile", "CMolotovProjectile"}
	local ALPHA       = floor(255 * OPACITY)

	----------------------------------------------------------------------------
	-- Menu: the built-in checkbox is hidden and driven by the combobox that takes its place
	----------------------------------------------------------------------------

	local builtin, tab, container
	for _, path in ipairs {
		{"VISUALS", "Other ESP"}, {"VISUALS", "Player ESP"}, {"VISUALS", "Effects"},
		{"VISUALS", "Colored models"}, {"MISC", "Miscellaneous"}, {"MISC", "Settings"},
	} do
		local found, reference = pcall(ui.reference, path[1], path[2], NAME)
		if found and reference then
			builtin, tab, container = reference, path[1], path[2]
			break
		end
	end
	if not builtin then
		tab, container = "VISUALS", "Other ESP"
		log(255, 180, 80, "built-in '" .. NAME .. "' not found, Default does nothing")
	end

	local builtin_initial = builtin and ui_get(builtin)
	local mode = ui.new_combobox(tab, container, NAME, "Off", "Legacy", "Neverlose", "CS2", "Lunar", "Default")
	ui_set(mode, builtin_initial and "Default" or "Off")
	if builtin then ui_set_visible(builtin, false) end

	local pin_timer_label = ui.new_label(tab, container, "Timer")
	local pin_timer = ui.new_color_picker(tab, container, "Timer colour", 150, 196, 240, 255)
	local exclamation_label = ui.new_label(tab, container, "Warning")
	local exclamation = ui.new_color_picker(tab, container, "Warning colour", 193, 196, 81, 255)

	local function update_builtin()
		if builtin then ui_set(builtin, ui_get(mode) == "Default") end
		ui_set_visible(pin_timer_label, ui_get(mode) == "Neverlose")
		ui_set_visible(pin_timer, ui_get(mode) == "Neverlose")
		ui_set_visible(exclamation_label, ui_get(mode) == "Legacy")
		ui_set_visible(exclamation, ui_get(mode) == "Legacy")
	end
	update_builtin()
	ui.set_callback(mode, update_builtin)

	----------------------------------------------------------------------------
	-- Projectiles
	----------------------------------------------------------------------------

	-- An HE grenade and a flashbang are both a CBaseCSGrenadeProjectile, the model tells them apart.
	local function grenade_kind(entindex, radius)
		local model = model_name(entity_get_prop(entindex, "m_nModelIndex") or 0)
		if model:find("frag", 1, true) then return "he" end
		if model:find("flash", 1, true) then return "flash" end
		-- no model name to go by: fall back to the damage radius
		if radius > 0 and radius <= HE_MIN_RADIUS then return "flash" end
		return "he"
	end

	-- Flashbangs blind everyone, the thrower included (RadiusFlash checks no teams); the rest only matter if
	-- they can hurt the player.
	local function is_threat(kind, thrower, player)
		if kind == "flash" then return true end
		return damage_scale(kind == "he" and "he" or "fire", thrower, player) > 0
	end

	local function distance_to_path(path, x, y, z)
		local nearest = huge
		for i = 1, #path, 3 do
			local dx, dy, dz = path[i] - x, path[i + 1] - y, path[i + 2] - z
			nearest = min(nearest, dx * dx + dy * dy + dz * dz)
		end
		return sqrt(nearest)
	end

	local tracked = {}   -- predictions by projectile entindex

	local function reset()
		tracked = {}
	end

	local smothered = effects.smothered

	local function add_projectiles(warnings, player)
		local now, dt = globals_realtime(), globals_tickinterval()
		-- A grenade's age is counted in game time, which runs at the server's speed (host_timescale included);
		-- real time only paces the re-simulation. The latency is real time too, so it is converted at the rate
		-- game time is passing this frame.
		local game_now = globals.curtime()
		local game_rate = globals.frametime() / max(globals.absoluteframetime(), 1e-6)
		local half_latency = (client.latency() or 0) / 2 * game_rate
		-- entity origins are interpolated, so they trail the server by the interpolation time
		local lerp = max(cvar_float("cl_interp", 0.015625),
			cvar_float("cl_interp_ratio", 2) / max(cvar_float("cl_updaterate", 64), 1))
		local seen = {}

		for _, class in ipairs(PROJECTILES) do
			local molotov = class == "CMolotovProjectile"

			for _, entindex in ipairs(entity_get_all(class)) do
				local x, y, z = entity_get_prop(entindex, "m_vecOrigin")
				local exploded = band(entity_get_prop(entindex, "m_fEffects") or 0, EF_NODRAW) ~= 0
				local thrower = player_from_handle(entity_get_prop(entindex, "m_hThrower"))
				local radius = entity_get_prop(entindex, "m_DmgRadius") or 0
				local kind = molotov and "molotov" or grenade_kind(entindex, radius)

				-- an exploded grenade is done with; it must not live on as a remembered warning
				if exploded then tracked[entindex] = nil end

				if x and not exploded and is_threat(kind, thrower, player) then
					seen[entindex] = true

					-- A grenade that drops out of view for a moment (out of the server's view of the player, or
					-- not a threat for a frame) keeps its age; starting over would restart its fuse. Its initial
					-- velocity tells it apart from a new grenade that took over the same entity index.
					local ivx, ivy, ivz = entity_get_prop(entindex, "m_vInitialVelocity")
					local identity = string.format("%.1f %.1f %.1f", ivx or 0, ivy or 0, ivz or 0)
					local grenade = tracked[entindex]
					if not grenade or grenade.identity ~= identity then
						-- the throw time is not networked: assume the grenade is one network trip old
						grenade = {first_seen = game_now, initial_age = half_latency + dt, simulated = -1, identity = identity,
							class = class}
						tracked[entindex] = grenade
					end
					grenade.last_seen = game_now
					local age = grenade.initial_age + (game_now - grenade.first_seen)

					-- simulated once, then again only if the grenade strays from the predicted path
					local outdated = grenade.simulated < 0
					if not outdated and now - grenade.simulated >= RESIMULATE_GAP then
						outdated = not grenade.path or distance_to_path(grenade.path, x, y, z) > PATH_TOLERANCE
					end
					if outdated then
						local vx, vy, vz = entity_get_prop(entindex, "m_vecVelocity")
						local age_ticks = max(0, floor((age - lerp) / dt + 0.5))
						local path = {}
						local px, py, pz, time_left = simulate(x, y, z, vx or 0, vy or 0, vz or 0, kind, age_ticks, thrower, path)

						grenade.simulated, grenade.path = now, path
						grenade.x, grenade.y, grenade.z = px, py, pz
						grenade.fuse = px and age_ticks * dt + time_left   -- from the throw to the detonation
						grenade.damage, grenade.damage_time = nil, nil
					end

					-- the damage depends on where the player stands, so it follows them around
					if grenade.x and kind == "he" and now - (grenade.damage_time or -1) >= DAMAGE_INTERVAL then
						local damage = entity_get_prop(entindex, "m_flDamage") or 0
						grenade.damage_time = now
						grenade.damage, grenade.max_damage = blast_damage(player, grenade.x, grenade.y, grenade.z,
							damage >= 1 and damage or HE_DAMAGE,
							radius >= HE_MIN_RADIUS and radius or HE_RADIUS,
							damage_scale("he", thrower, player))
					end

					local put_out = kind == "molotov" and grenade.x
						and smothered(grenade.x, grenade.y, grenade.z, grenade.fuse - age)
					grenade.warning = nil
					if grenade.x and age <= grenade.fuse + 0.1 and not put_out then
						grenade.warning = {
							x = grenade.x, y = grenade.y, z = grenade.z,
							progress = min(1, age / max(grenade.fuse, 1e-3)), time_left = max(0, grenade.fuse - age),
							kind = kind, damage = grenade.damage, max_damage = grenade.max_damage, key = entindex,
							icon = molotov and entity_get_prop(entindex, "m_bIsIncGrenade") == 1 and "incgrenade" or nil,
						}
						warnings[#warnings + 1] = grenade.warning
					end
				end
			end
		end

		for entindex, grenade in pairs(tracked) do
			if not seen[entindex] then
				-- out of view for now: the warning carries on from the last prediction, its timer still running
				local age = grenade.initial_age + (game_now - grenade.first_seen)
				local warning = grenade.warning
				-- a deleted grenade (a molotov or flashbang that went off) is gone from the entity list; one that
				-- is only out of view is still listed
				if game_now - grenade.last_seen > TRACK_MEMORY or entity.get_classname(entindex) ~= grenade.class then
					tracked[entindex] = nil
				elseif warning and age <= grenade.fuse + 0.1 then
					warning.progress = min(1, age / max(grenade.fuse, 1e-3))
					warning.time_left = max(0, grenade.fuse - age)
					warnings[#warnings + 1] = warning
				end
			end
		end
	end

	-- A burning fire keeps its warning, with the ring unwinding over the time the flames have left.
	local function add_fires(warnings, player)
		local dt = globals_tickinterval()
		local lifetime = cvar_float("inferno_flame_lifetime", 7)

		for _, entindex in ipairs(entity_get_all("CInferno")) do
			local x, y, z = entity_get_prop(entindex, "m_vecOrigin")
			local ignited = entity_get_prop(entindex, "m_nFireEffectTickBegin")
			if x and ignited and ignited > 0 then
				local owner = player_from_handle(entity_get_prop(entindex, "m_hOwnerEntity"))
				local burning = max(0, (globals.tickcount() - ignited) * dt)
				if damage_scale("fire", owner, player) > 0 and burning < lifetime then
					warnings[#warnings + 1] = {x = x, y = y, z = z, progress = 1 - burning / lifetime, time_left = lifetime - burning,
						kind = "fire", key = -entindex}
				end
			end
		end
	end

	----------------------------------------------------------------------------
	-- Drawing
	----------------------------------------------------------------------------

	-- 0 to 1: an HE grenade's damage against the most it could do to the player, or how deep the player is
	-- inside the reach of a fire.
	local function danger_of(warning, fire_reach)
		if warning.kind == "he" then
			return warning.damage and min(1, warning.damage / max(warning.max_damage or HE_DAMAGE, 1)) or 0
		elseif warning.kind == "fire" or warning.kind == "molotov" then
			return max(0, min(1, (RANGE - warning.distance) / max(RANGE - fire_reach, 1)))
		end
		return 0
	end

	-- Where a warning goes: over its world position, kept inside the ellipse that touches the middle of each
	-- screen edge, or on that ellipse in its direction when the position is off screen.
	local function screen_position(x, y, z, width, height, margin)
		local half_w, half_h = width / 2 - margin, height / 2 - margin

		local screen_x, screen_y = renderer.world_to_screen(x, y, z)
		if screen_x then
			local dx, dy = screen_x - width / 2, screen_y - height / 2
			local reach = sqrt((dx / half_w) ^ 2 + (dy / half_h) ^ 2)
			if reach > 1 then dx, dy = dx / reach, dy / reach end
			return width / 2 + dx, height / 2 + dy
		end

		local ex, ey, ez = client_eye_position()
		local pitch, yaw = client_camera_angles()
		if not ex or not pitch then return width / 2, height - margin end

		-- the direction to the position along the view's right and up axes
		local cp, sp, cy, sy = cos(rad(pitch)), sin(rad(pitch)), cos(rad(yaw)), sin(rad(yaw))
		local dx, dy, dz = x - ex, y - ey, z - ez
		local right = dx * sy - dy * cy
		local down = -(dx * (sp * cy) + dy * (sp * sy) + dz * cp)
		local length = sqrt(right * right + down * down)
		if length < 1e-4 then right, down, length = 0, 1, 1 end
		right, down = right / length, down / length

		local scale = 1 / sqrt((right / half_w) ^ 2 + (down / half_h) ^ 2)
		return width / 2 + right * scale, height / 2 + down * scale
	end

	-- The share of a warning's time still to run, 1 to 0. A grenade's progress is its fuse burning, 0 to 1; a
	-- fire's is the share of its burn left already.
	local function time_left(warning)
		return max(0, min(1, warning.kind == "fire" and warning.progress or 1 - warning.progress))
	end

	local function ring_width(radius)
		return max(2, floor(radius * 0.11))
	end

	local function draw_warning(x, y, radius, progress, label, danger)
		local width = ring_width(radius)
		local ring = radius + width / 2   -- circle_outline strokes around its radius

		-- the timer goes first, so that the red layer cannot bleed onto its track
		renderer_circle_outline(x, y, 0, 0, 0, ALPHA, ring, 0, 1, width)
		if progress > 0 then
			renderer_circle_outline(x, y, 255, 255, 255, ALPHA, ring, 0, min(1, progress), width)
		end

		renderer_circle(x, y, 0, 0, 0, ALPHA, radius, 0, 1)
		if danger > 0 then
			renderer_circle(x, y, 235, 30, 30, floor(255 * DANGER_OPACITY * danger), radius, 0, 1)
		end

		local r, g, b, a = ui_get(exclamation)
		renderer_text(x, y - radius * 0.20, r, g, b, floor(a * OPACITY), "c+b", 0, "!")
		renderer_text(x, y + radius * 0.38, 255, 255, 255, ALPHA, "c-b", 0, label)
	end

	-- The warnings within RANGE of the eye, nearest first, each given its distance.
	local function in_range(warnings, ex, ey, ez)
		local nearby = {}
		for _, warning in ipairs(warnings) do
			local dx, dy, dz = warning.x - ex, warning.y - ey, warning.z - ez
			warning.distance = sqrt(dx * dx + dy * dy + dz * dz)
			if warning.distance <= RANGE then nearby[#nearby + 1] = warning end
		end
		table.sort(nearby, function(a, b) return a.distance < b.distance end)
		return nearby
	end

	local function draw_warnings(warnings, ex, ey, ez)
		local nearby = in_range(warnings, ex, ey, ez)
		local width, height = client.screen_size()
		local base_radius = max(22, RADIUS * height / 1080)
		local fire_reach = cvar_float("inferno_max_range", 150)

		-- farthest first, which leaves the nearest one on top
		for i = min(#nearby, MAX_WARNINGS), 1, -1 do
			local warning = nearby[i]
			local danger = danger_of(warning, fire_reach)
			local radius = base_radius * (1 + (RADIUS_GROWTH - 1) * danger)
			local margin = radius + ring_width(radius) + 6
			local x, y = screen_position(warning.x, warning.y, warning.z + 8, width, height, margin)

			-- an HE grenade shows the damage it would do, and its distance while it would do none
			local label
			if warning.kind == "he" and warning.damage and warning.damage > 0 then
				label = string.format("%d dmg", warning.damage)
			else
				label = string.format("%d ft", floor(warning.distance / FOOT + 0.5))
			end
			-- the timer counts down, but fills up while a molotov or incendiary is still in the air
			local timer = warning.kind == "molotov" and min(1, warning.progress) or time_left(warning)
			draw_warning(x, y, radius, timer, label, danger)
		end
	end

	----------------------------------------------------------------------------
	-- Neverlose: a marker on a stalk over where the grenade goes off
	----------------------------------------------------------------------------

	local pin_states = {}   -- by warning key: how far the icon has shrunk, and the last damage shown

	-- How far a marker's icon has made room for an HE grenade's damage: 0 to 1 over PIN.SHRINK_TIME, eased.
	-- Keeps the last damage in state.label, so that the number can fade out with it.
	local function shrink_of(state)
		local warning = state.warning
		local damage = warning.kind == "he" and warning.damage and warning.damage > 0 and warning.damage
		if damage then state.label = tostring(damage) end
		local step = globals.frametime() / PIN.SHRINK_TIME
		state.shrink = damage and min(1, state.shrink + step) or max(0, state.shrink - step)
		return state.shrink * state.shrink * (3 - 2 * state.shrink)
	end

	-- The pin's glow at a strength level, 0 to PIN.GLOW_LEVELS, white for tinting: the falloff of PIN.GLOW_STEPS
	-- circles stacked from the rim inwards, each `t` of the way in at `strength * t` opacity, as one texture.
	local glows = {}
	local function glow_texture(face, level)
		local key = face .. "x" .. level
		if glows[key] ~= nil then return glows[key] or nil end
		local strength = 0.06 + 0.30 * level / PIN.GLOW_LEVELS
		local size = 2 * ceil(face * 1.08) + 2
		local half, pixels = size / 2, {}
		for py = 0, size - 1 do
			for px = 0, size - 1 do
				local d = sqrt((px + 0.5 - half) ^ 2 + (py + 0.5 - half) ^ 2)
				local clear = 1
				for step = 1, PIN.GLOW_STEPS do
					local t = step / PIN.GLOW_STEPS
					clear = clear * (1 - strength * t * max(0, min(1, face * (1.08 - t) - d + 0.5)))
				end
				pixels[#pixels + 1] = string.char(255, 255, 255, floor(255 * (1 - clear) + 0.5))
			end
		end
		local ok, texture = pcall(renderer.load_rgba, table.concat(pixels), size, size)
		glows[key] = ok and texture and {texture, size} or false
		return glows[key] or nil
	end

	local function draw_pin(x, y, scale, state, anchored, rotation)
		local warning, danger, fade = state.warning, state.danger, state.fade
		local radius = floor(PIN.RADIUS * scale + 0.5)
		local border = max(2, floor(PIN.BORDER * scale + 0.5))
		local stalk = floor(PIN.STALK * scale + 0.5)
		local face = radius - border
		local bc, fc = PIN.BORDER_COLOR, PIN.FACE_COLOR

		-- on screen the stalk's tip sits on the spot; off screen the marker is pinned to the edge, its stalk
		-- pointing the way to the grenade
		local cx, cy = x, y
		if anchored then cy = y - stalk - radius end
		local body = pin_body(radius, stalk, anchored and 0 or rotation)
		if body then
			local half = radius + stalk
			renderer.texture(body, floor(cx - half + 0.5), floor(cy - half + 0.5), 2 * half, 2 * half,
				bc[1], bc[2], bc[3], floor(bc[4] * fade), "f")
		else
			renderer_circle(cx, cy, bc[1], bc[2], bc[3], floor(bc[4] * fade), radius, 0, 1)
		end
		renderer_circle(cx, cy, fc[1], fc[2], fc[3], floor(fc[4] * fade), face, 0, 1)

		-- a soft glow from the middle: blue while the grenade cannot hurt, turning a deep red by 40% danger
		-- (Neverlose's is already solid red at under half its damage; a slow blend went through a muddy purple)
		local calm, hot = PIN.CALM_COLOR, PIN.DANGER_COLOR
		local level = floor(min(1, danger * 2.5) * PIN.GLOW_LEVELS + 0.5)
		local mix = level / PIN.GLOW_LEVELS
		local glow = glow_texture(face, level)
		if glow then
			renderer.texture(glow[1], floor(cx - glow[2] / 2 + 0.5), floor(cy - glow[2] / 2 + 0.5), glow[2], glow[2],
				calm[1] + (hot[1] - calm[1]) * mix, calm[2] + (hot[2] - calm[2]) * mix, calm[3] + (hot[3] - calm[3]) * mix,
				floor(255 * fade), "f")
		end

		-- the time left: an arc along the inside edge of the face, from a fixed end low on the left, round
		-- over the top; its other end winds back as time runs out
		local left = time_left(warning)
		if left > 0 then
			local r, g, b, a = ui_get(pin_timer)
			a = floor(a * fade)
			local width = max(2, floor(PIN.TIMER_WIDTH * scale + 0.5))
			local span = floor(PIN.TIMER_SPAN * left + 0.5)
			local shape = span >= 1 and timer_shape(face, width, PIN.TIMER_START, span)
			if shape then
				renderer.texture(shape, floor(cx - face + 0.5), floor(cy - face + 0.5), 2 * face, 2 * face, r, g, b, a, "f")
			elseif span >= 1 then
				local arc = face - width / 2   -- circle_outline strokes around its radius
				renderer_circle_outline(cx, cy, r, g, b, a, arc, PIN.TIMER_START, span / 360, width)
			end
		end

		-- the icon fills the middle on its own, and shrinks and moves down to make room for the damage number,
		-- which fades in with it
		local t = shrink_of(state)
		local height = floor((PIN.ICON + (PIN.ICON_SMALL - PIN.ICON) * t) * scale + 0.5)
		local icon_y = cy - height / 2 + face * 0.28 * t
		if t > 0 and state.label then
			renderer_text(cx, cy - face * 0.38, 255, 255, 255, floor(255 * t * fade), "cb", 0, state.label)
		end
		local texture, width = icon(warning.icon or warning.kind, height)
		if texture then
			renderer.texture(texture, floor(cx - width / 2), floor(icon_y), width, height, 255, 255, 255, floor(255 * fade), "f")
		end
	end

	-- The markers fade in and out over PIN.FADE_TIME, and stay on screen while they fade out. With `marker`, the
	-- markers are drawn by marker(x, y, state) instead of as pins: centred on the spot, `margin` from the edges.
	local function draw_pins(warnings, ex, ey, ez, marker, marker_margin)
		local nearby = in_range(warnings, ex, ey, ez)
		local width, height = client.screen_size()
		local scale = height / 1080
		local fire_reach = cvar_float("inferno_max_range", 150)
		local margin = marker_margin or (PIN.RADIUS + PIN.STALK) * scale + 6

		for _, state in pairs(pin_states) do state.seen = false end
		for i = 1, min(#nearby, MAX_WARNINGS) do
			local warning = nearby[i]
			local key = warning.key or 0
			local state = pin_states[key]
			if not state then
				state = {shrink = 0, fade = 0}
				pin_states[key] = state
			end
			state.seen, state.warning, state.danger = true, warning, danger_of(warning, fire_reach)
		end

		local step = globals.frametime() / PIN.FADE_TIME
		local order = {}
		for key, state in pairs(pin_states) do
			state.fade = state.seen and min(1, state.fade + step) or state.fade - step
			if state.fade <= 0 then
				pin_states[key] = nil
			else
				order[#order + 1] = state
			end
		end
		-- farthest first, which leaves the nearest one on top
		table.sort(order, function(a, b) return a.warning.distance > b.warning.distance end)

		for _, state in ipairs(order) do
			local warning = state.warning
			local sx, sy = renderer.world_to_screen(warning.x, warning.y, warning.z)
			if marker then
				if not (sx and sx > margin and sx < width - margin and sy > margin and sy < height - margin) then
					sx, sy = screen_position(warning.x, warning.y, warning.z, width, height, margin)
				end
				marker(sx, sy, state)
			else
				local anchored = sx and sx > margin and sx < width - margin and sy > margin * 2 and sy < height - 6
				local rotation = 0
				if not anchored then
					sx, sy = screen_position(warning.x, warning.y, warning.z, width, height, margin)
					-- away from the middle of the screen, from straight down, in steps that share textures
					local angle = math.deg(math.atan2(sy - height / 2, sx - width / 2)) - 90
					rotation = (floor(angle / PIN.TURN_STEP + 0.5) * PIN.TURN_STEP) % 360
				end
				draw_pin(sx, sy, scale, state, anchored, rotation)
			end
		end
	end

	----------------------------------------------------------------------------
	-- CS2: the icon in a glow that pulses, yellow while harmless and red at full danger, with the damage and
	-- the time left under it, and an arrow towards the spot while that is off screen
	----------------------------------------------------------------------------

	local CS2 = {
		RADIUS    = 64,                -- of the glow's texture, pixels at 1080p; the glow fades out well inside it
		PULSE     = 0.65,              -- seconds per pulse
		LOW       = 0.4,               -- the glow's opacity in the middle, through a pulse
		HIGH      = 0.75,
		CALM      = {255, 205, 40},
		DANGER    = {235, 30, 30},
		ICON      = 24,                -- icon height, pixels at 1080p
		LINE      = 14,                -- text line height
		ARROW     = 16,                -- the arrow's texture, pixels at 1080p
		ARROW_GAP = 0.62,              -- how far out the arrow sits, as a share of RADIUS
	}

	-- The glow: a white texture whose alpha falls off like a gaussian to nothing at its edge, tinted when drawn.
	-- One textured quad is smooth where stacked circles show their edges. Built in a function, whose locals do not
	-- count towards the main chunk's 200.
	function CS2.load_glow()
		local SIZE = 64
		local sigma, edge = SIZE / 5, math.exp(-(SIZE / 2) ^ 2 / (2 * (SIZE / 5) ^ 2))
		local pixels = {}
		for y = 0, SIZE - 1 do
			for x = 0, SIZE - 1 do
				local dx, dy = x + 0.5 - SIZE / 2, y + 0.5 - SIZE / 2
				local value = (exp(-(dx * dx + dy * dy) / (2 * sigma * sigma)) - edge) / (1 - edge)
				pixels[#pixels + 1] = string.char(255, 255, 255, floor(255 * max(0, value) + 0.5))
			end
		end
		local ok, texture = pcall(renderer.load_rgba, table.concat(pixels), SIZE, SIZE)
		if not ok or not texture then log(255, 180, 80, "the CS2 glow failed to load, its warnings are drawn without it") end
		return ok and texture or nil
	end
	CS2.glow = CS2.load_glow()

	-- The arrow: a triangle pointing `angle` degrees clockwise from the right, as an SVG so that its edges are
	-- smooth. Cached per size and per PIN.TURN_STEP degrees. Straight sides as cubics, see load_shape.
	do
		local arrows = {}

		function CS2.arrow(size, angle)
			local key = size .. "x" .. angle
			if arrows[key] == nil then
				local c, s = cos(rad(angle)), sin(rad(angle))
				local function point(px, py)
					return string.format("%.2f %.2f", size / 2 + px * c - py * s, size / 2 + px * s + py * c)
				end
				local tip, back, wing = point(size * 0.42, 0), point(-size * 0.3, size * 0.34), point(-size * 0.3, -size * 0.34)
				local svg = string.format('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">'
					.. '<path fill="#fff" d="M%s C%s %s %s C%s %s %s C%s %s %s Z"/></svg>', size, size, size, size,
					tip, tip, back, back, back, wing, wing, wing, tip, tip)
				arrows[key] = renderer.load_svg(svg, size, size) or false
			end
			return arrows[key] or nil
		end
	end

	local function draw_cs2(warnings, ex, ey, ez)
		local nearby = in_range(warnings, ex, ey, ez)
		local width, height = client.screen_size()
		local scale = height / 1080
		local radius = floor(CS2.RADIUS * scale + 0.5)
		local arrow_size = floor(CS2.ARROW * scale + 0.5)
		local arrow_gap = radius * CS2.ARROW_GAP
		local fire_reach = cvar_float("inferno_max_range", 150)
		local margin = arrow_gap + arrow_size

		-- the glow's opacity in the middle, eased between LOW and HIGH
		local pulse = CS2.LOW + (CS2.HIGH - CS2.LOW) * (0.5 - 0.5 * cos(2 * math.pi * globals_realtime() / CS2.PULSE))

		-- farthest first, which leaves the nearest one on top
		for i = min(#nearby, MAX_WARNINGS), 1, -1 do
			local warning = nearby[i]
			local danger = danger_of(warning, fire_reach)
			local calm, hot = CS2.CALM, CS2.DANGER
			local r = calm[1] + (hot[1] - calm[1]) * danger
			local g = calm[2] + (hot[2] - calm[2]) * danger
			local b = calm[3] + (hot[3] - calm[3]) * danger

			local x, y = renderer.world_to_screen(warning.x, warning.y, warning.z)
			local on_screen = x and x > margin and x < width - margin and y > margin and y < height - margin
			if not on_screen then x, y = screen_position(warning.x, warning.y, warning.z, width, height, margin) end
			local px, py = floor(x + 0.5), floor(y + 0.5)

			if CS2.glow then
				renderer.texture(CS2.glow, px - radius, py - radius, 2 * radius, 2 * radius, r, g, b, floor(255 * pulse), "f")
			else
				renderer_circle(px, py, r, g, b, floor(255 * pulse * 0.5), radius * 0.6, 0, 1)
			end

			local lines = {}
			if warning.kind == "he" and warning.damage and warning.damage > 0 then
				lines[#lines + 1] = string.format("-%d HP", warning.damage)
			end
			lines[#lines + 1] = string.format("%.1f s", warning.time_left or 0)

			-- the icon and the text under it, centred together; a molotov shows the fire it will start
			local icon_height = floor(CS2.ICON * scale + 0.5)
			local line = CS2.LINE * scale
			local top = y - (icon_height + line * #lines) / 2
			local kind = warning.kind == "molotov" and "fire" or warning.icon or warning.kind
			local texture, icon_width = icon(kind, icon_height)
			if texture then
				renderer.texture(texture, floor(x - icon_width / 2 + 0.5), floor(top + 0.5), icon_width, icon_height,
					255, 255, 255, 255, "f")
			end
			for n, text in ipairs(lines) do
				renderer_text(x, top + icon_height + line * (n - 0.5), 255, 255, 255, 255, "cb", 0, text)
			end

			-- off screen, an arrow just outside the glow points the way, over a soft shadow
			if not on_screen then
				local dx, dy = x - width / 2, y - height / 2
				local angle = math.deg(math.atan2(dy, dx))
				local length = max(sqrt(dx * dx + dy * dy), 1e-3)
				local ax, ay = floor(px + dx / length * arrow_gap - arrow_size / 2 + 0.5), floor(py + dy / length * arrow_gap - arrow_size / 2 + 0.5)
				local shape = CS2.arrow(arrow_size, (floor(angle / PIN.TURN_STEP + 0.5) * PIN.TURN_STEP) % 360)
				if shape then
					renderer.texture(shape, ax + 1, ay + 1, arrow_size, arrow_size, 0, 0, 0, 120, "f")
					renderer.texture(shape, ax, ay, arrow_size, arrow_size, 255, 255, 255, 240, "f")
				end
			end
		end
	end

	----------------------------------------------------------------------------
	-- Lunar: the grenade warning of the Lunar.Tools pen (codepen.io/Onii-the-flexboxer/pen/ExBOevM), in its
	-- units: a 72 px outer circle scaled 1.67, a dark track inside its edge with the time left as a light arc
	-- clockwise from the top, a 72 px inner circle with a blue gradient, a faint rim and a red glow that grows
	-- from nothing with the danger, and the icon in the middle.
	----------------------------------------------------------------------------

	local LUNAR = {
		SIZE   = 0.69,                   -- of the pen's size: 42 px out to the track's edge at 1080p
		OUTER  = 60.1,                   -- 36 * 1.67
		CORE   = 49.3,                   -- radial-gradient(#5064881A 58%, #101010 57%): farthest corner 50.9 * 0.58 * 1.67
		ARC    = 54.1,                   -- the timer's middle: r 45 of 100, on a 72 px box scaled 1.67
		ARC_WIDTH = 8.4,                 -- stroke-width 7 of 100, scaled the same
		INNER  = 36,                     -- inner circle, inside its 1 px border
		ICON   = 43,                     -- the 29 px icon scaled 1.5
		ICON_SMALL = 25,                 -- under a damage number, as the Neverlose pin's 26 -> 15
		-- how far each icon's artwork sits below the middle of its box, as a share of its height (the HE's box
		-- has room above the lever); measured from the paths in ICON_SVG
		ICON_DROP = {he = 0.093, molotov = 0.054, incgrenade = 0.092},
		CORE_COLOR  = {35, 37, 41},      -- #1E1E1E under #506488 at 10%
		TRACK_COLOR = {0, 0, 0, 0.2},    -- see-through, as in the Figma design (the pen's is a solid #101010)
		TOP    = {80, 100, 136},         -- the inner gradient, top to bottom, at 10% and 25%
		BOTTOM = {140, 175, 238},
		ARC_COLOR = {255, 255, 255, 191},   -- white at 75%
		RED    = 0.2,                    -- the glow's opacity at full danger; none at all without danger
		GLOW_CORE = 0.495,               -- solid out to this share of the inner radius, then fading out
		textures = {},
	}

	-- The still layers in one texture, every layer composited here pixel by pixel, edges anti-aliased. kind "red"
	-- is the glow on its own, white, tinted and faded when drawn: it fades in opacity only, its colour staying, as
	-- CSS gradients do.
	function LUNAR.texture(kind, scale)
		local key = kind .. scale
		if LUNAR.textures[key] ~= nil then return LUNAR.textures[key] or nil end
		local outer, core, inner = LUNAR.OUTER * scale, LUNAR.CORE * scale, LUNAR.INNER * scale
		local rim = max(1, scale)
		local size = 2 * ceil(kind == "red" and inner or outer) + 2
		local half = size / 2
		local function cover(r, d) return max(0, min(1, r - d + 0.5)) end
		local function mix(a, b, t) return a + (b - a) * t end

		-- the pixel being built, in straight alpha; each layer is laid over what is below
		local r, g, b, a
		local function over(lr, lg, lb, la)
			if la <= 0 then return end
			local out = la + a * (1 - la)
			r = (lr * la + r * a * (1 - la)) / out
			g = (lg * la + g * a * (1 - la)) / out
			b = (lb * la + b * a * (1 - la)) / out
			a = out
		end

		local pixels = {}
		for py = 0, size - 1 do
			for px = 0, size - 1 do
				local dx, dy = px + 0.5 - half, py + 0.5 - half
				local d = sqrt(dx * dx + dy * dy)
				r, g, b, a = 0, 0, 0, 0
				if kind == "red" then
					local u = max(0, min(1, (d / inner - LUNAR.GLOW_CORE) / (1 - LUNAR.GLOW_CORE)))
					r, g, b, a = 255, 255, 255, cover(inner, d) * (1 - u)
				else
					local track, centre = LUNAR.TRACK_COLOR, LUNAR.CORE_COLOR
					over(track[1], track[2], track[3], track[4] * (cover(outer, d) - cover(core, d)))
					over(centre[1], centre[2], centre[3], cover(core, d))
					local t = max(0, min(1, (dy + inner) / (2 * inner)))
					over(mix(LUNAR.TOP[1], LUNAR.BOTTOM[1], t), mix(LUNAR.TOP[2], LUNAR.BOTTOM[2], t), mix(LUNAR.TOP[3], LUNAR.BOTTOM[3], t), mix(0.1, 0.25, t) * cover(inner, d))
					over(255, 255, 255, 0.1 * (cover(inner + rim, d) - cover(inner, d)))   -- the border, 1 px at least
				end
				pixels[#pixels + 1] = string.char(floor(r + 0.5), floor(g + 0.5), floor(b + 0.5), floor(255 * a + 0.5))
			end
		end
		local ok, texture = pcall(renderer.load_rgba, table.concat(pixels), size, size)
		LUNAR.textures[key] = ok and texture and {texture, size} or false
		return LUNAR.textures[key] or nil
	end

	-- One Lunar marker, centred on x, y, faded and animated through the pin's state (see draw_pins): it fades in
	-- and out, and its icon shrinks and drops to make room for an HE grenade's damage as the pin's does.
	function LUNAR.marker(x, y, state)
		local warning, fade, scale = state.warning, state.fade, LUNAR.scale
		local px, py = floor(x + 0.5), floor(y + 0.5)

		local base, red = LUNAR.texture("base", scale), LUNAR.texture("red", scale)
		if base then
			renderer.texture(base[1], px - base[2] / 2, py - base[2] / 2, base[2], base[2], 255, 255, 255, floor(255 * fade), "f")
		end
		-- the red glow, from nothing to RED with the danger, as the pen's proximity does
		local glow = floor(255 * LUNAR.RED * state.danger * fade + 0.5)
		if red and glow > 0 then
			renderer.texture(red[1], px - red[2] / 2, py - red[2] / 2, red[2], red[2], 255, 0, 0, glow, "f")
		end

		-- the time left along the track, clockwise from the top
		local span = floor(360 * time_left(warning) + 0.5)
		local band = max(2, floor(LUNAR.ARC_WIDTH * scale + 0.5))
		local face = floor(LUNAR.ARC * scale + band / 2 + 0.5)
		local shape = span >= 1 and timer_shape(face, band, 270, span)
		if shape then
			local c = LUNAR.ARC_COLOR
			renderer.texture(shape, px - face, py - face, 2 * face, 2 * face, c[1], c[2], c[3], floor(c[4] * fade), "f")
		end

		-- the icon, its artwork centred, shrinking and dropping below an HE grenade's damage as the pin's does
		local t = shrink_of(state)
		local inner = LUNAR.INNER * scale
		if t > 0 and state.label then
			renderer_text(x, y - inner * 0.38, 255, 255, 255, floor(255 * t * fade), "cb", 0, state.label)
		end
		local kind = warning.kind == "molotov" and "fire" or warning.icon or warning.kind
		local icon_height = floor((LUNAR.ICON + (LUNAR.ICON_SMALL - LUNAR.ICON) * t) * scale + 0.5)
		local texture, icon_width = icon(kind, icon_height)
		if texture then
			local top = y - icon_height * (0.5 + (LUNAR.ICON_DROP[kind] or 0)) + inner * 0.28 * t
			renderer.texture(texture, floor(x - icon_width / 2 + 0.5), floor(top + 0.5), icon_width, icon_height,
				255, 255, 255, floor(255 * fade), "f")
		end
	end

	local function draw_lunar(warnings, ex, ey, ez)
		LUNAR.scale = floor(select(2, client.screen_size()) / 1080 * LUNAR.SIZE * 100 + 0.5) / 100   -- in steps, so that the textures are reused
		draw_pins(warnings, ex, ey, ez, LUNAR.marker, LUNAR.OUTER * LUNAR.scale + 6)
	end

	local function on_paint()
		local style = ui_get(mode)
		-- only the pin styles keep marker states up to date; left behind, they would fade out at stale spots when
		-- one of those styles comes back
		if style ~= "Neverlose" and style ~= "Lunar" and next(pin_states) then
			for key in pairs(pin_states) do pin_states[key] = nil end
		end
		if style ~= "Legacy" and style ~= "Neverlose" and style ~= "CS2" and style ~= "Lunar" then
			if next(tracked) then reset() end
			return
		end

		local player = entity.get_local_player()
		local ex, ey, ez = client_eye_position()
		if not player or not entity.is_alive(player) or not ex then return end

		check_trace(player)
		trace_ignore = player

		local warnings = {}
		add_projectiles(warnings, player)
		add_fires(warnings, player)
		if style == "Neverlose" then
			draw_pins(warnings, ex, ey, ez)
		elseif style == "CS2" then
			draw_cs2(warnings, ex, ey, ez)
		elseif style == "Lunar" then
			draw_lunar(warnings, ex, ey, ez)
		else
			draw_warnings(warnings, ex, ey, ez)
		end
	end

	client.set_event_callback("paint", guarded(on_paint))
	client.set_event_callback("level_init", reset)
	client.set_event_callback("cs_game_disconnected", reset)

	client.set_event_callback("shutdown", function()
		if builtin then
			ui_set_visible(builtin, true)
			ui_set(builtin, builtin_initial)
		end
	end)
end
proximity_warning()

--------------------------------------------------------------------------------
-- Molotov area
--------------------------------------------------------------------------------

do
	local FLAME_HALF_WIDTH = 64   -- InfernoFire_HalfWidth: how far a flame reaches from its centre
	local SAMPLES          = 24   -- points around each flame; more round the corners off
	local FILL_ALPHA       = 0.50 -- of the colour's alpha, at the edge
	local FILL_BANDS       = 12    -- steps of the gradient, spaced for equal alpha steps
	local FILL_CURVE       = 3     -- how sharply it rises towards the edge; 1 is linear
	local LEVEL_HEIGHT     = 24    -- units of height that split a fire into separate shapes

	local enabled = ui.new_checkbox("VISUALS", "Other ESP", "Molotov area")
	local harmful = ui.new_color_picker("VISUALS", "Other ESP", "Molotov area harmful", 235, 30, 30, 255)
	local safe_label = ui.new_label("VISUALS", "Other ESP", "Molotov area safe")
	local safe = ui.new_color_picker("VISUALS", "Other ESP", "Molotov area safe colour", 140, 200, 255, 255)

	local function update_menu()
		ui_set_visible(safe_label, ui_get(enabled))
		ui_set_visible(safe, ui_get(enabled))
	end
	update_menu()
	ui.set_callback(enabled, update_menu)

	-- Convex hull of 2D points {x, y, z}, counter-clockwise (Andrew's monotone chain).
	local function hull(points)
		table.sort(points, function(a, b) return a[1] < b[1] or (a[1] == b[1] and a[2] < b[2]) end)
		local function cross(o, a, b) return (a[1] - o[1]) * (b[2] - o[2]) - (a[2] - o[2]) * (b[1] - o[1]) end
		local result = {}
		for pass = 1, 2 do
			local start = #result
			for i = (pass == 1 and 1 or #points), (pass == 1 and #points or 1), (pass == 1 and 1 or -1) do
				local p = points[i]
				while #result >= start + 2 and cross(result[#result - 1], result[#result], p) <= 0 do
					result[#result] = nil
				end
				result[#result + 1] = p
			end
			result[#result] = nil   -- the last point starts the other half
		end
		return result
	end

	-- A fire as flat shapes, one per height level, so that flames on a box and on the floor beside it do not
	-- merge into one shape with walls. Rebuilt only when flames start or go out.
	local outlines = {}   -- by entindex: {key, shapes = {{outline, cx, cy, cz}, ...}}

	local function shape_of(flames)
		local points, z = {}, 0
		for _, flame in ipairs(flames) do z = z + flame[3] / #flames end
		for _, flame in ipairs(flames) do
			for s = 0, SAMPLES - 1 do
				local angle = s / SAMPLES * 2 * math.pi
				points[#points + 1] = {flame[1] + cos(angle) * FLAME_HALF_WIDTH, flame[2] + sin(angle) * FLAME_HALF_WIDTH, z}
			end
		end

		local outline = hull(points)
		local n = #outline
		local cx, cy = 0, 0
		for _, p in ipairs(outline) do cx, cy = cx + p[1] / n, cy + p[2] / n end
		return {outline = outline, cx = cx, cy = cy, cz = z}
	end

	local function fire_outline(entindex)
		local ox, oy, oz = entity_get_prop(entindex, "m_vecOrigin")
		local count = entity_get_prop(entindex, "m_fireCount") or 0
		if not ox or count <= 0 then return end

		local burning = {}
		for i = 0, count - 1 do burning[i + 1] = entity_get_prop(entindex, "m_bFireIsBurning", i) == 1 and "1" or "0" end
		local key = string.format("%.1f %.1f %.1f %s", ox, oy, oz, table.concat(burning))

		local cached = outlines[entindex]
		if cached and cached.key == key then return cached end

		local flames = {}
		for i = 0, count - 1 do
			if burning[i + 1] == "1" then
				flames[#flames + 1] = {
					ox + (entity_get_prop(entindex, "m_fireXDelta", i) or 0),
					oy + (entity_get_prop(entindex, "m_fireYDelta", i) or 0),
					oz + (entity_get_prop(entindex, "m_fireZDelta", i) or 0),
				}
			end
		end
		if #flames == 0 then
			outlines[entindex] = nil
			return
		end

		-- flames within LEVEL_HEIGHT of the lowest one in a group share its level
		table.sort(flames, function(a, b) return a[3] < b[3] end)
		local shapes, group = {}, {flames[1]}
		for i = 2, #flames do
			if flames[i][3] - group[1][3] > LEVEL_HEIGHT then
				shapes[#shapes + 1] = shape_of(group)
				group = {}
			end
			group[#group + 1] = flames[i]
		end
		shapes[#shapes + 1] = shape_of(group)

		cached = {key = key, shapes = shapes}
		outlines[entindex] = cached
		return cached
	end

	-- Band edges and alphas of the fill. The alpha rises as the cube of the distance from the centre, so the
	-- bands are spaced for equal alpha steps: thin at the edge where it climbs fast, wide in the clear middle.
	local band_scale, band_alpha = {}, {}
	for step = 1, FILL_BANDS do
		band_scale[step] = (step / FILL_BANDS) ^ (1 / FILL_CURVE)
		band_alpha[step] = (step - 0.5) / FILL_BANDS
	end

	local ring_x, ring_y = {}, {}   -- the fill's rings on screen, by band, reused from frame to frame

	-- The fill is FILL_BANDS copies of the outline, scaled about the middle in the world and each projected on its
	-- own, so a piece is drawn wherever its corners are in front of the camera: the middle of the fire may be off
	-- screen, or behind the camera, with the rest of it in view. The last ring is the outline itself, the rim.
	local function draw_shape(shape, r, g, b, a, width, height)
		local outline, n = shape.outline, #shape.outline
		local cx, cy, cz = shape.cx, shape.cy, shape.cz
		local project = screen_projector(width, height, cx, cy, cz)
		local mx, my = project(cx, cy, cz)

		for step = 1, FILL_BANDS do
			local xs, ys, scale = ring_x[step] or {}, ring_y[step] or {}, band_scale[step]
			ring_x[step], ring_y[step] = xs, ys
			for i = 1, n do
				local p = outline[i]
				xs[i], ys[i] = project(cx + (p[1] - cx) * scale, cy + (p[2] - cy) * scale, cz)
			end
		end

		for step = 1, FILL_BANDS do
			local alpha = floor(a * FILL_ALPHA * band_alpha[step])
			if alpha > 0 then
				local xs, ys, ixs, iys = ring_x[step], ring_y[step], ring_x[step - 1], ring_y[step - 1]
				for i = 1, n do
					local j = i % n + 1
					if xs[i] and xs[j] then
						if step == 1 then
							-- the innermost band fans out from the middle
							if mx then renderer.triangle(mx, my, xs[i], ys[i], xs[j], ys[j], r, g, b, alpha) end
						elseif ixs[i] and ixs[j] then
							renderer.triangle(ixs[i], iys[i], xs[i], ys[i], xs[j], ys[j], r, g, b, alpha)
							renderer.triangle(ixs[i], iys[i], xs[j], ys[j], ixs[j], iys[j], r, g, b, alpha)
						end
					end
				end
			end
		end

		local xs, ys = ring_x[FILL_BANDS], ring_y[FILL_BANDS]
		for i = 1, n do
			local j = i % n + 1
			if xs[i] and xs[j] then renderer.line(xs[i], ys[i], xs[j], ys[j], r, g, b, a) end
		end
	end

	local function draw_area(entindex, player)
		local fire = fire_outline(entindex)
		if not fire then return end

		local owner = player_from_handle(entity_get_prop(entindex, "m_hOwnerEntity"))
		local r, g, b, a = ui_get(damage_scale("fire", owner, player) > 0 and harmful or safe)
		local width, height = client.screen_size()
		for _, shape in ipairs(fire.shapes) do draw_shape(shape, r, g, b, a, width, height) end
	end

	local function forget_outlines() outlines = {} end
	client.set_event_callback("level_init", forget_outlines)
	client.set_event_callback("cs_game_disconnected", forget_outlines)

	paint_molotov_area = guarded(function()
		if not ui_get(enabled) then return end
		local player = entity.get_local_player()
		if not player then return end
		for _, entindex in ipairs(entity_get_all("CInferno")) do draw_area(entindex, player) end
	end)
end

--------------------------------------------------------------------------------
-- Icons
--------------------------------------------------------------------------------

-- The game's own equipment icons (materials/panorama/images/icons/equipment): width / height, then the SVG.
ICON_SVG = {
	he = {0.6074, [[<svg version="1.1" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" x="0px" y="0px" width="19.438px" height="32px" viewBox="0 0 19.438 32" enable-background="new 0 0 19.438 32" xml:space="preserve"><g id="Selected_Items"><g><g id="Selected_Items_57_"><path fill-rule="evenodd" clip-rule="evenodd" fill="#FFFFFF" d="M14.917,11.512c0,0.024,0,0.049,0,0.072 c0.022,0.023,0.034,0.048,0.034,0.072c0,0.023,0.012,0.047,0.036,0.072v0.035c0.025,0.023,0.037,0.048,0.037,0.072 c0.024,0.023,0.047,0.047,0.071,0.071v0.036c0,0.024,0.013,0.035,0.036,0.035c0,0.024,0,0.048,0,0.072 c0.025,0.023,0.036,0.048,0.036,0.072s0.012,0.048,0.037,0.073l0.035,0.035c0,0.024,0,0.047,0,0.071 c0.025,0.023,0.037,0.048,0.037,0.071l0.036,0.037v0.035c0,0.023,0,0.049,0,0.073c0.024,0.024,0.048,0.046,0.072,0.071 c0,0.024,0.011,0.047,0.034,0.073l0.036,0.036c0,0.023,0,0.047,0,0.071c0.024,0.023,0.037,0.048,0.037,0.072v0.036 c0.023,0,0.035,0.011,0.035,0.035c0,0.022,0,0.048,0,0.071c0.025,0.023,0.036,0.048,0.036,0.072 c0.024,0.023,0.036,0.047,0.036,0.072c0.023,0,0.036,0.013,0.036,0.036c0.023,0.023,0.037,0.047,0.037,0.072 c0.023,0.024,0.034,0.047,0.034,0.071c0.025,0,0.048,0,0.071,0c0,0.024,0.024,0.037,0.073,0.037 c0.024,0.022,0.047,0.046,0.071,0.07c0.023,0.024,0.037,0.047,0.037,0.072c0.022,0.024,0.035,0.048,0.035,0.071v0.073 c0.023,0.022,0.036,0.046,0.036,0.069c0,0.026,0.013,0.048,0.036,0.071v0.038c0,0.023,0.011,0.046,0.036,0.07 c0,0.024,0,0.047,0,0.073c0.024,0.024,0.036,0.047,0.036,0.071c0.024,0,0.037,0.012,0.037,0.035v0.037 c0.024,0.023,0.036,0.046,0.036,0.071c0,0.024,0,0.048,0,0.073l0.035,0.036c0,0.023,0.011,0.045,0.037,0.07 c0,0.024,0.012,0.049,0.036,0.072c0,0.023,0,0.047,0,0.072c0.025,0,0.036,0.011,0.036,0.036l0.035,0.037 c0.025,0.023,0.036,0.046,0.036,0.071c0.024,0.026,0.036,0.048,0.036,0.071v0.037c0.023,0.023,0.036,0.047,0.036,0.071 c0.023,0.025,0.037,0.049,0.037,0.072l0.034,0.035c0.025,0.024,0.036,0.047,0.036,0.07l0.037,0.038c0,0.023,0,0.046,0,0.072 c0.023,0.023,0.034,0.047,0.034,0.071l0.037,0.036c0,0.022,0.012,0.047,0.036,0.072c0,0.023,0.011,0.049,0.034,0.071v0.036 c0.026,0.024,0.038,0.047,0.038,0.072l0.034,0.036c0.025,0.023,0.037,0.047,0.037,0.071c0,0.024,0.012,0.036,0.035,0.036 c0,0.023,0,0.048,0,0.072c0.024,0.024,0.036,0.047,0.036,0.072c0.024,0.023,0.036,0.048,0.036,0.072l0.037,0.035 c0.023,0.024,0.036,0.049,0.036,0.072l0.035,0.036c0,0.024,0,0.047,0,0.072l0.035,0.035c0,0.026,0,0.048,0,0.071 c0,0.024,0.013,0.049,0.037,0.072c0,0.024,0,0.049,0,0.073c0.023,0,0.036,0.011,0.036,0.034c0.024,0.023,0.035,0.048,0.035,0.073 v0.035c0.025,0.024,0.049,0.047,0.072,0.072l0.037,0.037c0,0.023,0,0.047,0,0.071c0,0.024,0.012,0.048,0.035,0.071 c0,0.024,0,0.049,0,0.072c0.024,0,0.037,0.012,0.037,0.035c0,0.025,0,0.049,0,0.073l0.035,0.037c0,0.023,0,0.046,0,0.069 l0.036,0.039c0,0.022,0.012,0.045,0.035,0.069c0,0.023,0.013,0.047,0.037,0.071v0.037c0,0.025,0,0.047,0,0.072 c0.023,0.023,0.036,0.048,0.036,0.071l0.037,0.037c0.023,0.022,0.046,0.047,0.072,0.07v0.037 c0.024,0.024,0.035,0.048,0.035,0.072c0,0.023,0,0.047,0,0.071l0.036,0.036c0.024,0.023,0.036,0.049,0.036,0.071 c0,0.024,0,0.049,0,0.074l0.037,0.035v0.035c0.024,0.024,0.047,0.048,0.071,0.073c0,0.023,0.012,0.047,0.036,0.07 c0,0.024,0.012,0.049,0.035,0.074c0,0.022,0.013,0.036,0.036,0.036c0,0.023,0,0.047,0,0.07c0,0.025,0,0.048,0,0.073l0.071,0.072 c0.025,0.023,0.048,0.047,0.072,0.07c0,0.024,0,0.049,0,0.072c0.024,0.023,0.048,0.049,0.072,0.074v0.034 c0.023,0.024,0.035,0.048,0.035,0.073c0.025,0.023,0.037,0.048,0.037,0.071v0.036c0.023,0.024,0.047,0.037,0.072,0.037 c0,0.024,0.012,0.048,0.036,0.072c0,0.023,0.012,0.047,0.036,0.071c0,0.024,0.011,0.048,0.036,0.071v0.035 c0,0.025,0,0.049,0,0.073c0.023,0.023,0.034,0.047,0.034,0.071l0.037,0.037c0.024,0,0.047,0.011,0.072,0.036 c0,0.024,0,0.049,0,0.071c0.023,0.025,0.035,0.048,0.035,0.072l0.036,0.035v0.037c0,0.024,0,0.047,0,0.071v0.037 c0.025,0,0.049,0.011,0.073,0.034c0,0.025,0.011,0.05,0.034,0.072c0.024,0,0.036,0.011,0.036,0.035 c0,0.023,0.013,0.048,0.036,0.071v0.037c0,0.024,0,0.047,0,0.071v0.037c0,0.023,0,0.047,0,0.07s0,0.049,0,0.072 c0,0.025,0,0.049,0,0.073c-0.023,0-0.036,0.011-0.036,0.034c0,0.025,0,0.048,0,0.072l-0.036,0.036c0,0.143,0,0.286,0,0.43 c0,0.167,0,0.333,0,0.5c0,0.167,0,0.334,0,0.501c0,0.192,0,0.37,0,0.538c0,0.189,0,0.37,0,0.537c0,0.192,0,0.357,0,0.501 c0,0.12,0,0.227,0,0.324c0,0.023,0,0.046,0,0.07v0.144c0,0.022,0,0.047,0,0.071v0.106c0,0.024,0,0.049,0,0.074 c0,0.023,0,0.047,0,0.071v0.036c0,0.023,0,0.046,0,0.071v0.036c0,0.024,0,0.048,0,0.071c0,0.024,0,0.048,0,0.071l0.036,0.037 c0,0.025,0,0.048,0,0.071c0,0.025,0.013,0.048,0.036,0.071v0.037c0,0.024,0,0.048,0,0.072v0.035c0,0.023,0,0.047,0,0.071v0.037 c0,0.023,0,0.047,0,0.07c0,0.025,0,0.048,0,0.073c0,0.024,0,0.047,0,0.07v0.037c0,0.023,0,0.047,0,0.072v0.036 c0,0.024,0,0.048,0,0.072v0.037c0,0.022,0,0.045,0,0.07s0,0.048,0,0.072s0,0.047,0,0.071v0.037c0,0.023,0,0.048,0,0.071 l0.036,0.036c0,0.024,0,0.046,0,0.07l0.036,0.038c0,0.023,0,0.047,0,0.071c0,0.023,0,0.046,0,0.071s0,0.048,0,0.07v0.038 c0,0.024,0,0.047,0,0.071v0.037c0,0.023,0,0.047,0,0.071v0.037c0,0.024,0,0.046,0,0.071s0,0.049,0,0.071v0.036 c0,0.024,0,0.049,0,0.072c0,0.023,0,0.048,0,0.071v0.036c0,0.024,0,0.048,0,0.073v0.037c0,0.023,0,0.047,0,0.07 c0,0.024,0,0.049,0,0.073v0.036c0,0.024,0,0.047,0,0.071c0,0.023,0,0.048,0,0.072v0.071c0,0.023,0,0.048,0,0.071 c0,0.024,0,0.047,0,0.072c0,0.025,0,0.049,0,0.072l0.035,0.036c0,0.023,0.012,0.048,0.036,0.072c0,0.023,0,0.048,0,0.072v0.07 c0,0.024,0,0.049,0,0.072c0,0.024,0,0.047,0,0.072c0,0.023,0,0.047,0,0.071v0.035c0,0.024,0,0.049,0,0.072 c0,0.024,0,0.047,0,0.072v0.071c0,0.024,0,0.047,0,0.072s0,0.049,0,0.071c0,0.025,0,0.05,0,0.074v0.035c0,0.023,0,0.048,0,0.071 c0,0.024,0,0.048,0,0.073v0.071c0,0.024,0,0.047,0,0.07c0,0.024,0,0.048,0,0.071c0,0.025,0,0.049,0,0.074 c-0.024,0-0.036,0.011-0.036,0.035c0,0.024,0,0.049,0,0.072c-0.024,0.022-0.035,0.046-0.035,0.072v0.071c0,0.024,0,0.048,0,0.072 c0,0.024,0,0.047,0,0.071v0.037c0,0.022,0,0.046,0,0.07c0,0.023,0,0.048,0,0.072c0,0.023,0,0.048,0,0.071v0.073 c0,0.022,0,0.046,0,0.071c0,0.023,0,0.048,0,0.072v0.034c0,0.026,0,0.05,0,0.073c0,0.023,0,0.046,0,0.073c0,0.022,0,0.046,0,0.07 c0,0.024,0,0.049,0,0.072c0,0.024,0,0.049,0,0.073c0,0.022,0,0.047,0,0.071v0.035c0,0.025,0,0.048,0,0.071 c0,0.024,0,0.049,0,0.073c0,0.024,0,0.048,0,0.072c0,0.025-0.013,0.048-0.036,0.071c0,0.024,0,0.049,0,0.072 s-0.013,0.047-0.036,0.072l-0.036,0.036c0,0.024-0.012,0.047-0.036,0.07c-0.023,0.024-0.034,0.048-0.034,0.072 c-0.024,0.025-0.037,0.048-0.037,0.072h-0.037c0,0.024-0.011,0.048-0.036,0.072H18.64c-0.072,0.025-0.12,0.048-0.143,0.071 c-0.048,0-0.095,0.012-0.144,0.035c-0.072,0-0.155,0-0.251,0c-0.048-0.023-0.094-0.035-0.142-0.035 c-0.024-0.023-0.06-0.046-0.107-0.071c0-0.024-0.013-0.048-0.037-0.072v-0.035c0-0.024,0-0.048,0-0.072c0-0.025,0-0.048,0-0.071 c0-0.023,0-0.048,0-0.072v-0.037l0.037-0.035c0.024-0.023,0.047-0.048,0.071-0.072c0.024-0.023,0.048-0.046,0.071-0.071 c0.048,0,0.096,0,0.143,0c0.024-0.023,0.048-0.047,0.071-0.072c0-0.073,0-0.131,0-0.178c0-0.167,0-0.348,0-0.537 c0-0.167,0-0.347,0-0.538c0-0.166,0-0.335,0-0.501c0-0.168,0-0.334,0-0.501c0-0.192,0-0.37,0-0.537c0-0.192,0-0.371,0-0.537 s0-0.335,0-0.502c0-0.167,0-0.346,0-0.536c0-0.168,0-0.347,0-0.536c0-0.145,0-0.312,0-0.503c0-0.048,0-0.106,0-0.179v-0.144 c0-0.023,0-0.048,0-0.072v-0.036c0-0.024,0-0.046,0-0.071v-0.072c0-0.025,0-0.048,0-0.071v-0.145c0-0.023,0-0.047,0-0.07v-0.109 c-0.022-0.023-0.035-0.046-0.035-0.071c-0.024-0.024-0.047-0.037-0.072-0.037c0-0.023,0-0.047,0-0.071v-0.035 c0-0.023,0-0.047,0-0.073c0-0.024,0-0.046,0-0.071v-0.036c0-0.023,0-0.048,0-0.073c0-0.023,0-0.046,0-0.07v-0.037 c0-0.023,0-0.047,0-0.071v-0.037c0-0.023,0-0.048,0-0.072v-0.036c0-0.023,0-0.047,0-0.07c0-0.023,0-0.047,0-0.072 c0-0.023,0-0.048,0-0.071v-0.037c-0.023-0.024-0.035-0.046-0.035-0.071v-0.037c-0.024-0.023-0.036-0.047-0.036-0.072v-0.036 c0-0.023,0-0.047,0-0.07c0-0.024,0-0.049,0-0.074c0-0.024,0-0.046,0-0.07v-0.035c0-0.024,0-0.048,0-0.072v-0.036 c0-0.024,0-0.048,0-0.071v-0.037c0-0.022,0-0.047,0-0.07c0-0.026,0-0.05,0-0.073c0-0.023,0-0.047,0-0.071v-0.037 c0-0.023,0-0.047,0-0.071V21.79c0-0.024,0-0.048,0-0.072v-0.036c0-0.023,0-0.045,0-0.07s0-0.048,0-0.072 c-0.023-0.024-0.048-0.047-0.071-0.071v-0.037c0-0.023,0-0.047,0-0.071v-0.037c0-0.023,0-0.046,0-0.071v-0.036 c0-0.023,0-0.049,0-0.073c0-0.023,0-0.047,0-0.071v-0.036c-0.024-0.022-0.036-0.047-0.036-0.07 c-0.024-0.024-0.035-0.049-0.035-0.072v-0.072c0-0.024,0-0.048,0-0.073c0-0.023,0-0.046,0-0.071s0-0.048,0-0.072l-0.036-0.035 c-0.024-0.024-0.037-0.049-0.037-0.072c0-0.023,0-0.048,0-0.071v-0.072c0-0.022,0-0.049,0-0.071 c-0.024-0.024-0.036-0.047-0.036-0.072s0-0.049,0-0.071c-0.024,0-0.036-0.013-0.036-0.036c0-0.025-0.011-0.048-0.035-0.073 c0-0.023,0-0.048,0-0.071c-0.025,0-0.048-0.012-0.072-0.036v-0.037c0-0.023,0-0.047,0-0.071c0-0.024,0-0.048,0-0.072 c0-0.023,0-0.047,0-0.071v-0.035c0-0.026,0-0.049,0-0.071c0-0.026,0-0.049,0-0.073c-0.025,0-0.037-0.011-0.037-0.036 c-0.024,0-0.036-0.012-0.036-0.037c0-0.023-0.012-0.047-0.037-0.07c0-0.024-0.011-0.047-0.035-0.072c0-0.023,0-0.048,0-0.071 c-0.023,0-0.036-0.012-0.036-0.037c0-0.023-0.011-0.048-0.035-0.071c-0.024-0.024-0.037-0.048-0.037-0.073 c-0.023,0-0.035-0.011-0.035-0.035v-0.035c-0.024-0.024-0.037-0.047-0.037-0.071c-0.023-0.024-0.047-0.049-0.072-0.072 c0-0.024-0.011-0.036-0.035-0.036c0-0.025-0.013-0.048-0.036-0.072c0-0.023,0-0.047,0-0.071 c-0.024-0.025-0.037-0.049-0.037-0.073v-0.034L17.1,18.495c0-0.024-0.011-0.047-0.035-0.071c0-0.024-0.013-0.049-0.036-0.07 c0-0.027-0.013-0.037-0.037-0.037c0-0.025,0-0.049,0-0.072c-0.023-0.024-0.036-0.048-0.036-0.073 c-0.024-0.023-0.036-0.047-0.036-0.07h-0.035c0-0.025,0-0.048,0-0.073s-0.011-0.047-0.037-0.071c0-0.024,0-0.047,0-0.073 c-0.023-0.023-0.045-0.035-0.072-0.035c0-0.023-0.011-0.048-0.034-0.07c-0.024,0-0.048,0-0.073,0 c-0.023-0.025-0.058-0.049-0.107-0.074c-0.024-0.022-0.034-0.047-0.034-0.071c-0.025,0-0.049-0.011-0.073-0.036 c0-0.023,0-0.048,0-0.071c-0.024-0.024-0.036-0.047-0.036-0.072l-0.036-0.037c0-0.023,0-0.048,0-0.07 c-0.023,0-0.046-0.014-0.071-0.037c-0.024-0.023-0.048-0.047-0.073-0.071c0-0.025-0.012-0.047-0.035-0.072v-0.037 c0-0.023-0.012-0.048-0.036-0.071c0-0.024-0.012-0.047-0.037-0.069c0-0.027-0.012-0.039-0.036-0.039c0-0.023,0-0.046,0-0.069 c-0.025,0-0.036-0.012-0.036-0.037c-0.023-0.023-0.048-0.048-0.071-0.073V16.74c0-0.023-0.013-0.048-0.035-0.072 c0-0.023,0-0.046,0-0.071c-0.024-0.024-0.048-0.047-0.072-0.071l-0.036-0.037c-0.024-0.025-0.049-0.048-0.073-0.072v-0.035 c0-0.024,0-0.049,0-0.073v-0.034c-0.023-0.024-0.046-0.048-0.071-0.073c-0.023-0.023-0.048-0.048-0.071-0.072 c0-0.023-0.013-0.045-0.036-0.071l-0.036-0.035c-0.023-0.024-0.036-0.048-0.036-0.072l-0.035-0.036c0-0.023,0-0.047,0-0.072 c-0.024,0-0.037-0.013-0.037-0.035c-0.024-0.024-0.047-0.049-0.07-0.072c0-0.024-0.013-0.047-0.036-0.072 c-0.025-0.023-0.036-0.048-0.036-0.072l-0.036-0.036c-0.024-0.023-0.037-0.047-0.037-0.071c-0.023,0-0.047-0.011-0.072-0.036 c0-0.024,0-0.048,0-0.072c-0.024,0-0.036-0.012-0.036-0.036c-0.023-0.022-0.036-0.048-0.036-0.071 c-0.023-0.025-0.047-0.049-0.071-0.072c0-0.024-0.012-0.036-0.037-0.036c0-0.023-0.012-0.048-0.036-0.071 c-0.023-0.026-0.047-0.049-0.071-0.072l-0.036-0.038c-0.023-0.023-0.047-0.046-0.072-0.07l-0.033-0.035 c0-0.022-0.013-0.046-0.038-0.072c0-0.024,0-0.048,0-0.071c-0.023-0.023-0.047-0.037-0.071-0.037 c0-0.023-0.012-0.045-0.036-0.071c-0.024-0.024-0.037-0.048-0.037-0.071c-0.023,0-0.036-0.012-0.036-0.037 c-0.023-0.025-0.048-0.036-0.07-0.036c-0.025-0.024-0.036-0.048-0.036-0.072c0-0.023-0.013-0.048-0.036-0.072 c-0.908,0-1.827,0.013-2.758,0.036c0,0.024-0.012,0.036-0.036,0.036c0,0.023-0.011,0.047-0.035,0.072v0.036 c0,0.024,0,0.049,0,0.072s0,0.047,0,0.072v0.035c0,0.024,0,0.048,0,0.073c0,0.023,0,0.048,0,0.071v0.071c0,0.024,0,0.047,0,0.07 c0,0.026,0,0.049,0,0.073c0,0.023,0,0.048,0,0.071v0.037c0,0.023,0,0.047,0,0.072c0,0.023,0,0.048,0,0.071v0.072 c0,0.024,0,0.047,0,0.072c0,0.025,0,0.048,0,0.071c0,0.024,0,0.049,0,0.072v0.036c0,0.023,0,0.047,0,0.072 c0,0.023,0,0.048,0,0.073v0.07c0,0.024,0,0.049,0,0.072c0,0.023,0,0.048,0,0.072c0,0.023,0,0.046,0,0.071v0.036 c0,0.024,0,0.049,0,0.072c0,0.025,0,0.048,0,0.072v0.07c0,0.023,0,0.048,0,0.073c0,0.023,0,0.046,0,0.071v0.036 c0,0.023,0,0.047,0,0.072c0.024,0,0.047,0,0.071,0c0.024,0,0.047,0,0.072,0c0.071,0,0.144,0.011,0.214,0.035 c0,0.024,0.012,0.037,0.037,0.037c0.094,0.022,0.166,0.034,0.215,0.034c0.047,0,0.095,0.013,0.143,0.037c0.025,0,0.048,0,0.072,0 c0.143,0.023,0.299,0.035,0.465,0.035c0.072,0,0.131,0,0.18,0c0.022,0.024,0.047,0.035,0.071,0.035 c0.024,0.025,0.047,0.038,0.071,0.038c0.048,0,0.085,0.012,0.108,0.035h0.037c0.095,0.024,0.178,0.037,0.25,0.037 c0.07,0,0.131,0.011,0.179,0.036c0.023,0.023,0.048,0.034,0.071,0.034c0.024,0,0.048,0,0.072,0c0.048,0,0.083,0.012,0.107,0.039 c0.025,0,0.049,0,0.072,0c0.047,0.022,0.097,0.032,0.143,0.032c0.048,0,0.109,0.013,0.18,0.037 c0.023,0.023,0.047,0.047,0.072,0.071c0.047,0.024,0.083,0.037,0.106,0.037h0.073c0.071,0,0.141,0.012,0.213,0.037 c0.025,0.022,0.049,0.047,0.073,0.071c0.022,0.024,0.058,0.048,0.107,0.072c0.047,0.022,0.095,0.047,0.143,0.07 c0.024,0,0.061,0.012,0.108,0.037c0.023,0.024,0.06,0.036,0.108,0.036c0.046,0.024,0.094,0.048,0.142,0.07 c0,0.025,0.025,0.049,0.072,0.072l0.036,0.037c0.023,0.023,0.048,0.047,0.071,0.071c0.025,0.023,0.048,0.047,0.071,0.072v0.035 c0.049,0.024,0.084,0.048,0.109,0.073c0.024,0.023,0.058,0.035,0.107,0.035c0,0.024,0.023,0.047,0.071,0.071 c0,0.024,0.011,0.048,0.036,0.073c0.024,0,0.048,0.013,0.072,0.036s0.036,0.045,0.036,0.07c0.023,0.026,0.046,0.049,0.071,0.073 c0.024,0.023,0.048,0.036,0.072,0.036c0.023,0.023,0.047,0.047,0.071,0.07l0.036,0.037c0.023,0.024,0.047,0.047,0.073,0.071 c0,0.023,0.01,0.038,0.034,0.038c0,0.023,0.013,0.047,0.036,0.07c0.024,0.024,0.037,0.047,0.037,0.072 c0.023,0.023,0.034,0.048,0.034,0.071l0.037,0.037c0.024,0.024,0.036,0.048,0.036,0.072l0.034,0.036c0,0.024,0,0.047,0,0.071 l0.038,0.035c0,0.024,0,0.047,0,0.071c0.022,0.023,0.034,0.048,0.034,0.071c0.025,0.024,0.037,0.049,0.037,0.073v0.036 c0.023,0.024,0.035,0.049,0.035,0.071v0.037c0.024,0.023,0.049,0.047,0.072,0.07v0.037c0,0.024,0.012,0.047,0.037,0.071 c0,0.023,0,0.048,0,0.071c0,0.025,0,0.05,0,0.072c0.023,0,0.036,0.011,0.036,0.035c0.024,0.023,0.035,0.048,0.035,0.071v0.037 c0.024,0.024,0.035,0.047,0.035,0.071v0.037c0,0.023,0,0.047,0,0.07s0.013,0.049,0.037,0.072c0,0.025,0,0.049,0,0.073v0.034 c0.023,0.025,0.036,0.048,0.036,0.072l0.035,0.036c0,0.022,0,0.048,0,0.071c0.025,0,0.036,0.012,0.036,0.036s0,0.047,0,0.07 c0,0.025,0.013,0.049,0.036,0.073v0.036c0,0.023,0.012,0.048,0.037,0.072c0,0.023,0,0.047,0,0.072v0.034 c0,0.025,0.012,0.048,0.035,0.073v0.036c0.024,0.023,0.037,0.048,0.037,0.071c0.024,0.024,0.035,0.048,0.035,0.072l0.036,0.035 c0.024,0.024,0.035,0.048,0.035,0.072c0.025,0.024,0.037,0.049,0.037,0.072v0.072c0,0.024,0.012,0.048,0.036,0.071 c0,0.025,0,0.049,0,0.072s0.011,0.049,0.037,0.073v0.035c0.023,0.024,0.035,0.048,0.035,0.072 c0.024,0.024,0.037,0.049,0.037,0.073v0.034l0.035,0.036c0,0.024,0,0.048,0,0.072s0,0.047,0,0.071c0,0.023,0,0.049,0,0.072v0.035 c0,0.025,0,0.049,0,0.072c0.023,0.025,0.036,0.048,0.036,0.072v0.035l0.036,0.037c0,0.023,0,0.046,0,0.071 c0,0.023,0,0.049,0,0.072c0,0.022,0,0.049,0,0.071v0.036c0,0.024,0,0.047,0,0.071c0,0.024,0,0.048,0,0.071v0.072 c0,0.025,0,0.049,0,0.072c0,0.025,0,0.047,0,0.072v0.036c0,0.024,0,0.049,0,0.072c0,0.024,0,0.047,0,0.072 c0,0.023,0,0.045,0,0.07v0.072c0,0.024,0,0.049,0,0.072c0,0.024,0,0.048,0,0.072v0.036c0,0.024,0,0.047,0,0.072 c0,0.023,0,0.048,0,0.072c0,0.024,0,0.047,0,0.071s0,0.047,0,0.071c0,0.025,0,0.049,0,0.073s0,0.047,0,0.07v0.036 c0,0.024,0,0.048,0,0.073c0,0.022,0,0.047,0,0.071c-0.024,0.025-0.036,0.047-0.036,0.072c-0.024,0.023-0.036,0.046-0.036,0.07 c0,0.025,0,0.048,0,0.073c0,0.023,0,0.047,0,0.072v0.035c0,0.023,0,0.048,0,0.072s0,0.048,0,0.071c0,0.024,0,0.048,0,0.071 c0,0.024-0.011,0.049-0.035,0.072c0,0.023,0,0.047,0,0.071c0,0.024-0.013,0.046-0.037,0.072l-0.035,0.035 c0,0.023,0,0.049,0,0.072s0,0.047,0,0.071l-0.037,0.037v0.035c-0.024,0.023-0.036,0.048-0.036,0.072c0,0.022,0,0.047,0,0.072 c0,0.023,0,0.046,0,0.07v0.036c-0.024,0.023-0.037,0.049-0.037,0.072c-0.023,0.024-0.035,0.048-0.035,0.072l-0.036,0.037v0.034 c-0.023,0.024-0.035,0.048-0.035,0.072s0,0.048,0,0.072c0,0.023-0.012,0.035-0.037,0.035c0,0.023,0,0.049,0,0.073 c-0.023,0.023-0.035,0.047-0.035,0.071c0,0.024,0,0.046,0,0.07l-0.072,0.073c0,0.023-0.011,0.048-0.036,0.072 c0,0.023,0,0.048,0,0.071c0,0.024-0.011,0.035-0.035,0.035c0,0.025-0.013,0.049-0.036,0.074c0,0.023-0.012,0.048-0.037,0.072 c0,0.023,0,0.047,0,0.071L17.1,26.267v0.035c-0.022,0.024-0.048,0.048-0.071,0.073c0,0.023,0,0.046,0,0.07 c-0.024,0-0.037,0.013-0.037,0.036c0,0.025,0,0.047,0,0.072c-0.023,0.023-0.036,0.046-0.036,0.07 c0,0.024-0.012,0.048-0.036,0.073v0.037l-0.035,0.035c0,0.023-0.011,0.049-0.037,0.072c0,0.023,0,0.046,0,0.071l-0.034,0.036 c0,0.022-0.011,0.048-0.038,0.072c0,0.023,0,0.046,0,0.07l-0.034,0.036c-0.024,0.023-0.036,0.048-0.036,0.071l-0.037,0.037 c0,0.023-0.011,0.047-0.034,0.072c0,0.023,0,0.047,0,0.072l-0.037,0.036c0,0.022-0.011,0.048-0.036,0.072 c0,0.023-0.012,0.046-0.034,0.07c0,0.023-0.014,0.036-0.037,0.036c-0.024,0.024-0.036,0.049-0.036,0.072 c0,0.024-0.012,0.037-0.036,0.037c0,0.022-0.011,0.046-0.036,0.07c0,0.024-0.011,0.037-0.035,0.037 c0,0.023-0.011,0.047-0.036,0.07c0,0.024-0.012,0.049-0.036,0.072c-0.025,0.024-0.048,0.048-0.071,0.072v0.036 c-0.024,0.024-0.036,0.047-0.036,0.072c-0.024,0.024-0.048,0.036-0.072,0.036c0,0.023-0.012,0.048-0.036,0.071 c0,0.025-0.012,0.038-0.036,0.038c0,0.022-0.012,0.046-0.036,0.07c0,0.024,0,0.048,0,0.071c-0.024,0.025-0.049,0.049-0.072,0.073 c-0.023,0-0.047,0.011-0.071,0.036c-0.024,0.024-0.049,0.047-0.073,0.07v0.035c-0.023,0.024-0.046,0.048-0.071,0.073 c0,0.025-0.011,0.037-0.034,0.037c0,0.023-0.014,0.047-0.037,0.07c0,0.024-0.013,0.047-0.036,0.072 c-0.023,0.024-0.047,0.048-0.072,0.071l-0.035,0.037c-0.024,0.024-0.037,0.048-0.037,0.072c-0.047,0.024-0.083,0.035-0.106,0.035 c-0.025,0.025-0.036,0.048-0.036,0.073l-0.036,0.034c-0.048,0.024-0.084,0.047-0.108,0.071c-0.048,0.025-0.083,0.048-0.107,0.073 c-0.024,0.024-0.047,0.048-0.072,0.071c-0.024,0-0.048,0.011-0.07,0.037c-0.024,0.022-0.037,0.046-0.037,0.071 c-0.024,0.023-0.047,0.035-0.071,0.035c-0.047,0.024-0.096,0.048-0.144,0.071c-0.024,0-0.059,0.013-0.108,0.037 c-0.048,0.024-0.084,0.047-0.106,0.072c-0.025,0.024-0.049,0.047-0.072,0.072s-0.047,0.035-0.071,0.035 c-0.049,0.024-0.085,0.049-0.109,0.072c-0.046,0.024-0.096,0.049-0.143,0.073c-0.047,0.022-0.083,0.035-0.107,0.035 c0,0.024-0.012,0.047-0.035,0.071l-0.037,0.036c-0.047,0.024-0.083,0.048-0.106,0.072c-0.024,0.025-0.049,0.036-0.073,0.036 c-0.048,0.024-0.095,0.037-0.143,0.037c-0.024,0.023-0.048,0.035-0.072,0.035c-0.05,0-0.083,0.012-0.107,0.036 c-0.071,0.024-0.119,0.049-0.144,0.072c-0.048,0.024-0.084,0.036-0.108,0.036c-0.047,0.023-0.084,0.035-0.106,0.035 c-0.073,0-0.132,0.012-0.18,0.037c-0.024,0-0.047,0-0.072,0c-0.046,0.023-0.094,0.036-0.141,0.036 c-0.051,0.024-0.098,0.036-0.146,0.036c-0.046,0.023-0.083,0.046-0.106,0.071c-0.047,0.024-0.095,0.047-0.144,0.072l-0.071,0.035 c-0.073,0-0.143,0-0.215,0c-0.025,0.024-0.037,0.048-0.037,0.072c-0.024,0-0.046,0-0.07,0c-0.047,0.025-0.096,0.037-0.144,0.037 c-0.025,0-0.06,0.011-0.108,0.034c-0.024,0-0.047,0-0.071,0c-0.024,0.024-0.06,0.035-0.107,0.035 c-0.095,0-0.179,0.013-0.25,0.037c-0.048,0-0.084,0-0.107,0c-0.024,0.025-0.061,0.036-0.107,0.036 c-0.048,0-0.096,0.012-0.144,0.037h-0.037c-0.046,0.023-0.093,0.035-0.141,0.035c-0.048,0-0.109,0.012-0.18,0.035 c-0.048,0.023-0.083,0.037-0.107,0.037c-0.024,0.023-0.06,0.034-0.108,0.034c-0.119,0-0.225,0-0.322,0 c-0.048,0.024-0.095,0.047-0.143,0.072c-0.048,0.024-0.084,0.037-0.108,0.037c-0.023,0.024-0.06,0.037-0.107,0.037 c-0.072,0-0.155,0.012-0.25,0.036H9.258c-0.023,0.023-0.06,0.036-0.108,0.036c-0.548,0-1.087,0-1.611,0 c-0.049,0-0.096-0.013-0.144-0.036H7.361c-0.024-0.024-0.049-0.036-0.073-0.036c-0.072-0.024-0.131-0.037-0.18-0.037 c-0.046-0.024-0.095-0.047-0.143-0.072c0-0.024-0.012-0.037-0.036-0.037H6.895c-0.049-0.022-0.107-0.034-0.18-0.034 c-0.024-0.024-0.06-0.048-0.106-0.072l-0.037-0.035c-0.047-0.024-0.084-0.037-0.107-0.037c-0.072-0.023-0.144-0.036-0.216-0.036 l-0.071-0.037H6.142c-0.073-0.023-0.13-0.035-0.178-0.035c-0.049-0.023-0.097-0.046-0.144-0.071 c-0.048-0.024-0.095-0.048-0.143-0.072H5.569c-0.072-0.024-0.143-0.047-0.214-0.072c-0.023-0.024-0.049-0.048-0.071-0.072H5.248 C5.176,30.611,5.127,30.6,5.104,30.6c-0.072-0.023-0.144-0.048-0.214-0.072c-0.025,0-0.051-0.012-0.073-0.037H4.781 c-0.072-0.023-0.119-0.035-0.143-0.035c-0.048-0.023-0.107-0.048-0.179-0.072c-0.023-0.023-0.06-0.046-0.108-0.071 c-0.047,0-0.094-0.012-0.142-0.035c-0.048-0.024-0.108-0.048-0.181-0.073c-0.023-0.024-0.033-0.048-0.033-0.072 c-0.049-0.023-0.097-0.036-0.145-0.036c-0.024-0.023-0.048-0.046-0.072-0.071c-0.024,0-0.048-0.013-0.072-0.035 c-0.022-0.024-0.046-0.048-0.07-0.073c-0.048-0.023-0.072-0.047-0.072-0.072c-0.024,0-0.048-0.011-0.072-0.035 c-0.023-0.024-0.048-0.047-0.072-0.072c-0.022-0.025-0.045-0.048-0.071-0.072c-0.025-0.023-0.059-0.037-0.107-0.037 c-0.024-0.023-0.06-0.046-0.106-0.071c-0.024,0-0.037-0.012-0.037-0.035c-0.024-0.024-0.048-0.049-0.071-0.071 c-0.024-0.026-0.049-0.037-0.073-0.037c0-0.023-0.012-0.047-0.035-0.071c-0.024-0.025-0.037-0.048-0.037-0.073 c-0.024-0.024-0.047-0.047-0.071-0.071c-0.023-0.023-0.047-0.034-0.071-0.034c-0.024-0.025-0.048-0.048-0.071-0.073 c-0.023,0-0.037-0.011-0.037-0.035c-0.047-0.023-0.095-0.048-0.143-0.072c0-0.024-0.012-0.037-0.034-0.037 c-0.024-0.023-0.038-0.047-0.038-0.071c-0.024-0.025-0.047-0.048-0.072-0.072c-0.023-0.023-0.047-0.047-0.072-0.07 c-0.022,0-0.046-0.012-0.07-0.037c0-0.025-0.013-0.048-0.036-0.073c-0.023,0-0.048-0.011-0.072-0.035 c0-0.023-0.012-0.046-0.036-0.07c0-0.025-0.012-0.036-0.036-0.036c0-0.023-0.012-0.048-0.036-0.073 c-0.023-0.023-0.035-0.046-0.035-0.071c-0.024-0.024-0.048-0.048-0.073-0.07c-0.023,0-0.047-0.012-0.071-0.038 c-0.024-0.023-0.037-0.047-0.037-0.071c-0.024,0-0.036-0.012-0.036-0.036s0-0.048,0-0.072l-0.035-0.036c0-0.023,0-0.047,0-0.072 c-0.023-0.023-0.049-0.048-0.072-0.072c0-0.023-0.012-0.047-0.036-0.07c-0.023,0-0.036-0.013-0.036-0.037 c-0.024-0.023-0.035-0.048-0.035-0.07l-0.036-0.037c0-0.023-0.013-0.047-0.037-0.072l-0.071-0.07 c-0.024-0.024-0.047-0.049-0.071-0.072c0-0.024-0.013-0.048-0.036-0.072V27.34c0-0.023,0-0.047,0-0.072 c0-0.024-0.013-0.047-0.036-0.072c0-0.023-0.012-0.048-0.036-0.071c-0.024,0-0.036-0.011-0.036-0.036 c-0.024,0-0.049-0.012-0.071-0.035c0-0.023,0-0.048,0-0.072c0-0.024-0.012-0.047-0.037-0.071v-0.036 c0-0.024-0.011-0.048-0.035-0.073c0-0.023-0.012-0.047-0.037-0.07c0-0.024-0.012-0.037-0.035-0.037c0-0.025,0-0.048,0-0.073 v-0.036c0-0.023-0.012-0.048-0.036-0.071c0-0.023,0-0.048,0-0.072c-0.025,0-0.037-0.011-0.037-0.036 c0-0.022-0.011-0.046-0.035-0.071c0-0.024,0-0.047,0-0.071c0-0.025-0.013-0.037-0.037-0.037c0-0.023,0-0.048,0-0.071 c-0.023,0-0.035-0.013-0.035-0.037c0-0.024,0-0.047,0-0.071l-0.036-0.038c0-0.022,0-0.046,0-0.07s-0.012-0.047-0.036-0.071 c0-0.024-0.012-0.048-0.035-0.071l-0.036-0.038c0-0.023,0-0.045,0-0.07v-0.036c0-0.023-0.013-0.048-0.037-0.071v-0.037 c0-0.023,0-0.046,0-0.071s0-0.048,0-0.072s0-0.047,0-0.07c-0.022,0-0.034-0.014-0.034-0.037 c-0.024-0.024-0.048-0.048-0.072-0.072v-0.036c0-0.025,0-0.048,0-0.072v-0.037c0-0.023,0-0.046,0-0.07c0-0.024,0-0.048,0-0.073 c0-0.023,0-0.047,0-0.07v-0.037c-0.023-0.024-0.036-0.048-0.036-0.071v-0.035c-0.024-0.024-0.036-0.048-0.036-0.072v-0.037 c0-0.022,0-0.046,0-0.071c0-0.023-0.013-0.046-0.037-0.071v-0.037c0-0.023-0.012-0.046-0.036-0.071c0-0.023,0-0.047,0-0.071 v-0.036c0-0.025,0-0.048,0-0.071v-0.036c0-0.024,0-0.048,0-0.071c0-0.024,0-0.049,0-0.074v-0.034c0-0.025,0-0.047,0-0.072 c0-0.024,0-0.049,0-0.071v-0.073c0-0.023,0-0.047,0-0.071c0-0.024,0-0.047,0-0.07c0-0.025,0-0.05,0-0.073v-0.035 c0-0.024,0-0.047,0-0.071c0-0.024,0-0.049,0-0.072v-0.072c0-0.023,0-0.047,0-0.071s0-0.047,0-0.073c0-0.024,0-0.049,0-0.072 v-0.036c0-0.024,0-0.046,0-0.07c0-0.024,0-0.048,0-0.072v-0.072c0-0.024,0-0.046,0-0.071s0-0.049,0-0.073 c0-0.024,0-0.047,0-0.072v-0.037c0-0.023,0-0.047,0-0.071c0-0.024,0-0.047,0-0.071v-0.071c0-0.024,0-0.048,0-0.072 c0-0.024,0-0.048,0-0.072s0-0.048,0-0.072v-0.035c0.024-0.024,0.036-0.047,0.036-0.072c0-0.023,0.012-0.047,0.037-0.072v-0.071 c0-0.023,0-0.047,0-0.071c0-0.024,0-0.047,0-0.072v-0.036c0-0.024,0-0.047,0-0.07c0-0.023,0-0.048,0-0.073 c0-0.023,0.012-0.048,0.036-0.072v-0.035c0.024,0,0.036-0.013,0.036-0.037c0-0.023,0-0.047,0-0.071c0-0.024,0-0.048,0-0.071 v-0.037c0-0.024,0-0.047,0-0.072c0-0.023,0-0.048,0-0.072c0.024-0.023,0.048-0.047,0.072-0.072H0.59c0-0.022,0-0.047,0-0.07 c0-0.024,0-0.049,0-0.072s0-0.049,0-0.072v-0.036c0-0.024,0-0.048,0-0.071c0-0.024,0.013-0.049,0.037-0.072 c0-0.024,0-0.049,0-0.072c0-0.024,0-0.049,0-0.072c0-0.023,0-0.048,0-0.071c0.023-0.023,0.036-0.047,0.036-0.072 c0.023,0,0.035-0.013,0.035-0.035c0.024-0.025,0.047-0.049,0.072-0.072c0-0.024,0-0.047,0-0.072c0-0.023,0.012-0.034,0.035-0.034 v-0.036c0-0.025,0-0.048,0-0.073c0-0.023,0-0.048,0-0.071c0.023-0.023,0.037-0.049,0.037-0.073v-0.036 c0.023-0.023,0.046-0.047,0.071-0.072c0-0.023,0.013-0.048,0.036-0.071v-0.035c0.023,0,0.035-0.011,0.035-0.035 c0.024-0.026,0.037-0.049,0.037-0.071c0.024-0.026,0.035-0.049,0.035-0.073v-0.036c0-0.024,0-0.048,0-0.072 c0.025-0.024,0.037-0.047,0.037-0.072c0-0.022,0-0.046,0-0.071v-0.036c0.022,0,0.036-0.012,0.036-0.037 c0.023-0.023,0.035-0.048,0.035-0.071c0.024-0.024,0.049-0.048,0.072-0.073l0.036-0.035c0-0.023,0-0.047,0-0.071 c0.023-0.024,0.036-0.048,0.036-0.071c0.024-0.024,0.047-0.048,0.071-0.072L1.45,18.71c0.024-0.023,0.037-0.047,0.037-0.071 c0.023-0.025,0.036-0.049,0.036-0.073l0.035-0.034c0.024-0.025,0.036-0.051,0.036-0.074c0.024-0.023,0.036-0.047,0.036-0.072 c0.023-0.023,0.049-0.046,0.072-0.07c0.023,0,0.035-0.013,0.035-0.036c0.024,0,0.049-0.013,0.073-0.036 c0.023-0.024,0.047-0.048,0.071-0.073c0.024-0.023,0.049-0.047,0.073-0.07l0.035-0.036c0.024-0.025,0.036-0.049,0.036-0.074 c0.024-0.022,0.049-0.046,0.072-0.07c0.024-0.024,0.049-0.048,0.072-0.073c0.023,0,0.036-0.012,0.036-0.035 c0.024,0,0.048-0.013,0.07-0.035c0.024-0.025,0.048-0.049,0.072-0.074c0.024-0.022,0.048-0.047,0.072-0.071L2.455,17.6 c0.022-0.023,0.034-0.048,0.034-0.071c0.025-0.024,0.048-0.047,0.071-0.072c0.024-0.024,0.049-0.037,0.072-0.037 c0-0.023,0.014-0.048,0.037-0.07c0.047,0,0.095-0.014,0.142-0.037c0.024-0.023,0.047-0.047,0.071-0.071 c0.025-0.025,0.061-0.047,0.109-0.072c0.046,0,0.083,0,0.107,0c0.024,0,0.059,0,0.108,0c0.023-0.023,0.046-0.047,0.07-0.072 c0.047-0.023,0.098-0.048,0.143-0.073c0.072-0.023,0.121-0.046,0.145-0.071c0.047,0,0.096-0.01,0.142-0.034 c0.048-0.025,0.108-0.036,0.18-0.036c0.024,0,0.048-0.012,0.072-0.037h0.037c0.047-0.023,0.093-0.035,0.141-0.035 c0.047-0.025,0.084-0.038,0.108-0.038c0.047-0.023,0.094-0.035,0.142-0.035c0.049,0,0.084,0,0.108,0 c0.048,0,0.095-0.012,0.143-0.035h0.036c0.048-0.024,0.095-0.037,0.143-0.037c0.096-0.023,0.203-0.034,0.322-0.034 c0.025-0.025,0.061-0.049,0.109-0.072c0.046,0,0.095-0.012,0.143-0.036c0.023-0.025,0.047-0.037,0.071-0.037 c0.048,0,0.084-0.013,0.108-0.036l0.036-0.036c0.049,0,0.095,0,0.143,0c0.216,0,0.442-0.011,0.68-0.035c0.024,0,0.049,0,0.072,0 c0.024,0,0.059,0,0.108,0v-0.035c0.118,0,0.236,0,0.356,0V16.24c0-0.024,0-0.047,0-0.072c0-0.023,0-0.048,0-0.072V16.06 c0-0.024,0-0.048,0-0.071c0-0.024,0-0.048,0-0.072c0-0.023,0-0.047,0-0.072v-0.07c0-0.025,0-0.049,0-0.073 c0-0.025,0-0.049,0-0.072v-0.036c0-0.023,0-0.047,0-0.072c0-0.023,0-0.046,0-0.071c0-0.024,0-0.048,0-0.072v-0.072 c0-0.023,0-0.048,0-0.071c0-0.025,0-0.048,0-0.072v-0.037c0-0.023,0-0.047,0-0.071c0-0.024,0-0.047,0-0.073 c-0.023-0.023-0.036-0.046-0.036-0.07v-0.071c0-0.023,0-0.048,0-0.071c0-0.024,0-0.048,0-0.073v-0.035c0-0.024,0-0.049,0-0.072 s0-0.048,0-0.072v-0.036c0-0.024,0-0.048,0-0.072v-0.036c-0.096,0-0.191,0-0.286,0c0-0.023-0.013-0.047-0.035-0.071 c0-0.022,0-0.047,0-0.07c0-0.025,0-0.048,0-0.073c-0.024,0-0.061,0-0.108,0c0-0.025,0-0.047,0-0.071v-0.072 c0-0.024,0-0.047,0-0.071c0-0.025,0-0.049,0-0.073v-0.108c0-0.023,0-0.045,0-0.071c0-0.023,0-0.047,0-0.069v-0.323 c0-0.024,0-0.047,0-0.071c0-0.024,0-0.048,0-0.072v-0.036c0-0.024,0-0.049,0-0.072c0-0.024,0-0.049,0-0.072s0-0.049,0-0.071 v-0.071c0-0.023,0-0.048,0-0.072c0-0.024,0-0.048,0-0.071v-0.036c0-0.026,0-0.049,0-0.073c0-0.025,0-0.047,0-0.071 c0-0.024,0-0.049,0-0.073v-0.071c0-0.023,0-0.048,0-0.071c0-0.024,0-0.047,0-0.071v-0.035c0-0.024,0-0.049,0-0.073 s0-0.048,0-0.072c0-0.024,0-0.048,0-0.072v-0.071c0-0.024,0-0.048,0-0.071c0-0.024,0-0.049,0-0.072v-0.035 c0-0.025,0-0.048,0-0.072c0-0.024,0-0.048,0-0.072c0-0.023,0-0.047,0-0.072v-0.071c0-0.026,0-0.049,0-0.072 c0-0.024,0-0.048,0-0.071v-0.036c0-0.024,0-0.049,0-0.072c0-0.024,0-0.048,0-0.072v-0.034c0-0.025,0-0.051,0-0.074v-0.035 c0-0.025,0-0.049,0-0.072v-0.036c0-0.024,0-0.048,0-0.071c0-0.025,0-0.049,0-0.072s0-0.047,0-0.072v-0.036 c0-0.025,0-0.048,0-0.072v-0.036c0-0.024,0-0.048,0-0.072v-0.035c0-0.025,0-0.049,0-0.072c0-0.024,0-0.048,0-0.072 c0-0.022,0-0.046,0-0.071v-0.036c0-0.023,0-0.047,0-0.071v-0.036c0-0.024,0-0.047,0-0.072V9.937c0-0.024,0-0.049,0-0.072 c0-0.024,0-0.047,0-0.071c0-0.023,0-0.048,0-0.072V9.685c0-0.023,0-0.047,0-0.071V9.578c0-0.022,0-0.046,0-0.072V9.471 c-0.239,0-0.466,0.013-0.681,0.035c-0.048,0-0.083,0-0.107,0C5.666,9.531,5.617,9.542,5.569,9.542H5.534 c-0.049,0-0.085,0-0.107,0C5.378,9.52,5.318,9.506,5.248,9.506c-0.048,0-0.083-0.011-0.109-0.035 C5.093,9.447,5.032,9.422,4.96,9.399c-0.024-0.023-0.036-0.047-0.036-0.07C4.901,9.305,4.876,9.282,4.853,9.256 c-0.025,0-0.036-0.012-0.036-0.036C4.792,9.197,4.781,9.173,4.781,9.148V9.113C4.757,9.089,4.746,9.064,4.746,9.042V9.006 c0-0.026,0-0.049,0-0.073c0-0.022,0-0.047,0-0.071c0-0.023,0.011-0.036,0.036-0.036c0-0.023,0-0.047,0-0.071 c0-0.023,0.011-0.046,0.036-0.072l0.036-0.036C4.876,8.625,4.901,8.6,4.924,8.576L4.96,8.54c0.048-0.024,0.096-0.037,0.144-0.037 h0.035V8.469c0-0.024,0-0.047,0-0.072V8.36c0-0.023,0-0.047,0-0.07c0-0.025,0-0.046,0-0.074V8.182l0.037-0.036 C5.2,8.123,5.223,8.099,5.248,8.075c0.023-0.023,0.036-0.048,0.036-0.072C5.305,7.978,5.331,7.955,5.354,7.93 c0-0.023,0.012-0.037,0.036-0.037c0.023-0.022,0.047-0.046,0.071-0.07c0.048,0,0.084-0.011,0.108-0.035l0.036-0.036 c0.049,0,0.095,0,0.143,0c0.167,0,0.334,0,0.501,0V7.716C6.273,7.692,6.297,7.68,6.322,7.68c0.022-0.023,0.046-0.047,0.071-0.071 c0.047,0,0.084-0.011,0.107-0.035V7.538c0-0.024,0-0.048,0-0.071c1.766,0,3.522,0,5.264,0V7.43c0-0.024,0-0.048,0-0.073 c0-0.022,0-0.047,0-0.07c0-0.025,0-0.048,0-0.071V7.18c0-0.024,0-0.048,0-0.072c0-0.025,0-0.048,0-0.072V6.964 c0-0.024,0-0.047,0-0.071c0-0.025,0-0.049,0-0.072V6.785c0-0.023,0-0.046,0-0.07c0-0.023,0-0.047,0-0.071V6.606 c0.025-0.024,0.061-0.036,0.108-0.036V6.535c0.31,0,0.621,0,0.931,0V6.57c0,0.024,0,0.048,0,0.073c0,0.023,0,0.048,0,0.071 c0,0.024,0,0.047,0,0.07l0.036,0.037c0,0.023,0,0.046,0,0.072c0.024,0.023,0.036,0.046,0.036,0.071L12.911,7v0.036 c0.024,0.024,0.035,0.047,0.035,0.072c0,0.024,0,0.048,0,0.072c0,0.022-0.011,0.036-0.035,0.036c0,0.023,0,0.046,0,0.071 c-0.024,0.023-0.036,0.047-0.036,0.07c-0.024,0.024-0.036,0.048-0.036,0.073v0.072c0,0.023,0,0.046,0,0.072 c0.048,0,0.096,0,0.143,0c0.025,0.023,0.049,0.047,0.071,0.07c0.025,0.025,0.048,0.036,0.073,0.036 c0.023,0.024,0.036,0.048,0.036,0.073c0.024,0,0.048,0.013,0.072,0.036c0.024,0.024,0.036,0.047,0.036,0.071l0.037,0.034 c0.023,0.024,0.035,0.05,0.035,0.073c0.024,0.024,0.037,0.048,0.037,0.073v0.036c0.024,0.024,0.035,0.048,0.035,0.071v0.036 c0,0.025,0,0.049,0,0.071c0,0.023-0.011,0.047-0.035,0.071V8.36h0.035c0,0.024,0.012,0.049,0.035,0.072c0,0.023,0,0.049,0,0.071 c0.024,0.024,0.037,0.048,0.037,0.073l0.035,0.036c0,0.024,0,0.049,0,0.072c0,0.026,0,0.048,0,0.072 c0.024,0.024,0.037,0.048,0.037,0.071h0.036c0.023,0.023,0.035,0.048,0.035,0.071c0.024,0,0.049,0.014,0.072,0.036 c0.024,0.024,0.036,0.047,0.036,0.073l0.037,0.035c0.023,0.023,0.035,0.048,0.035,0.071l0.037,0.036c0,0.024,0,0.048,0,0.071 l0.035,0.036c0,0.026,0,0.049,0,0.073c0,0.023,0.012,0.047,0.037,0.07c0,0.023,0,0.048,0,0.072l0.035,0.035 c0,0.025,0,0.05,0,0.072l0.035,0.036c0,0.024,0,0.048,0,0.071l0.037,0.037c0,0.024,0.012,0.048,0.035,0.072 c0,0.024,0,0.047,0,0.071c0.024,0.023,0.047,0.048,0.072,0.072c0.024,0,0.037,0.013,0.037,0.035 c0.022,0.025,0.034,0.048,0.034,0.072v0.036c0,0.024,0,0.048,0,0.071l0.037,0.036c0.023,0.024,0.047,0.049,0.072,0.071 c0,0.023,0.011,0.047,0.036,0.072c0,0.023,0.012,0.047,0.035,0.072v0.035c0.023,0.024,0.036,0.048,0.036,0.072v0.036 c0.024,0.024,0.036,0.047,0.036,0.072c0.022,0,0.047,0.012,0.07,0.036c0,0.024,0,0.049,0,0.072 c0.024,0.023,0.036,0.047,0.036,0.072c0,0.023,0,0.047,0,0.071c0.024,0,0.037,0.013,0.037,0.036 c0.024,0.022,0.036,0.047,0.036,0.072v0.035c0,0.023,0.011,0.049,0.035,0.074l0.036,0.034c0,0.024,0.012,0.048,0.038,0.072 c0,0.022,0.01,0.047,0.033,0.072v0.036c0.025,0.023,0.038,0.047,0.038,0.071c0.023,0.023,0.035,0.046,0.035,0.072l0.036,0.034 C14.879,11.5,14.892,11.512,14.917,11.512z M1.415,21.896c0-0.023,0-0.047,0-0.071c0-0.024,0-0.047,0-0.072v-0.036l-0.036,0.036 c0,0.024,0,0.048,0,0.072c-0.024,0.024-0.035,0.048-0.035,0.071v0.071c0,0.025,0,0.049,0,0.072c0,0.023,0,0.048,0,0.072 c0-0.024,0.012-0.049,0.035-0.072c0-0.023,0-0.047,0-0.072c0.025,0,0.036-0.011,0.036-0.035V21.896z"/></g></g></g><g id="guides"></g></svg>]]},
	flash = {0.7956, [[<svg version="1.1" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" x="0px" y="0px" width="25.458px" height="32px" viewBox="0 0 25.458 32" enable-background="new 0 0 25.458 32" xml:space="preserve"><g id="Selected_Items"><g><g id="Selected_Items_55_"><path fill-rule="evenodd" clip-rule="evenodd" fill="#FFFFFF" fill-opacity="0.302" d="M19.192,7.81l5.917,0.906l-6.023,4.121 l5.695,4.542l-5.319,0.705l1.238,6.83l-2.734-1.576l0.011-10.596l-3.441-5.741h-4.232c-0.385,0-0.641,0.038-0.77,0.116 c-0.231,0.102-0.347,0.32-0.347,0.654c0,0.513,0.308,0.808,0.924,0.885v1.539l-1.156,1H8.417c-0.359,0-0.54,0.179-0.54,0.539 l0.38,2.323l0.641,0.945l-0.136,1.851h0.423V18.7H8.763v0.731l0.045,0.533l0.067,0.449L2.88,23.023l2.458-6.787L0,13.574 l5.76-2.34L1.647,5.016L8.91,6.015l1.349-5.625l3.69,5.175l6.03-2.97L19.192,7.81z M17.345,13.274v9.736l-3.002-1.854l0.135-0.99 l0.268-0.741l-0.067-0.608l-0.304-0.169v-1.855l0.472-0.338l-0.27-1.519l1.161-0.754l-0.082-2.013L17.345,13.274z"/><path fill-rule="evenodd" clip-rule="evenodd" fill="#FFFFFF" d="M17.276,13.185l-1.54-0.886v1.81l-0.923,0.847v1.808h-0.384 v1.848h0.384v1.924h-0.384v1.848h0.384v1.924h-0.384v1.848h0.384v1.771l0.923,1.578v1.308c0,0.36-0.18,0.539-0.539,0.539h-6.85 c-0.359,0-0.54-0.179-0.54-0.539v-1.308l0.885-1.578v-1.771h0.424v-1.848H8.693v-1.924h0.424v-1.848H8.693V18.61h0.424v-1.848 H8.693v-1.808l-0.885-0.847v-2.464c0-0.359,0.181-0.538,0.54-0.538h0.538l1.155-1.001v-1.54C9.425,8.488,9.118,8.194,9.118,7.682 c0-0.333,0.115-0.553,0.346-0.655c0.128-0.077,0.385-0.115,0.77-0.115h4.232l3.58,5.734v12.122l-1.809,0.886 c-0.257,0.128-0.45,0.09-0.577-0.116c-0.129-0.23-0.078-0.448,0.153-0.653c0.206-0.18,0.514-0.386,0.923-0.616l0.54-0.27V13.185z "/></g></g></g><g id="guides"></g></svg>]]},
	molotov = {0.5885, [[<svg version="1.1" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" x="0px" y="0px" width="18.833px" height="32px" viewBox="0 0 18.833 32" enable-background="new 0 0 18.833 32" xml:space="preserve"><g id="Selected_Items"><g><g id="Selected_Items_58_"><path fill-rule="evenodd" clip-rule="evenodd" fill="#FFFFFF" fill-opacity="0.6" d="M15.826,7.595 c0.05,0.074,0.185,0.259,0.407,0.554c0.172,0.224,0.308,0.285,0.406,0.186c0.099-0.099,0-0.382-0.295-0.851 c-0.321-0.493-0.271-1.035,0.148-1.628c0.049,0.247,0.345,0.617,0.888,1.11c0.543,0.518,0.875,1.023,0.999,1.517 c0.049,0.246,0.061,0.493,0.037,0.739c-0.049,0.37-0.148,0.666-0.295,0.888c-0.247,0.321-0.382,0.53-0.407,0.63 c-0.075,0.173-0.013,0.382,0.185,0.629c0.172,0.245,0.123,0.578-0.148,0.998c-0.173,0.246-0.567,0.715-1.183,1.405 c-0.355,0.396-0.879,0.618-1.573,0.666c0.172-0.279,0.351-0.575,0.537-0.887c0.099,0,0.161-0.014,0.186-0.038l1.11-1.517 c0.049-0.099-0.014-0.259-0.186-0.48c-0.197-0.222-0.456-0.456-0.776-0.704c-0.32-0.22-0.617-0.381-0.888-0.48 c-0.295-0.122-0.469-0.136-0.518-0.036l-1.11,1.517h0.038c-0.024,0.05-0.024,0.123,0,0.221c-0.425,0.508-0.801,0.957-1.128,1.351 c-0.036-0.027-0.067-0.059-0.093-0.091c-0.198-0.271-0.382-0.642-0.555-1.11c-0.197-0.666-0.234-1.208-0.111-1.627 c0.198-0.371,0.345-0.642,0.444-0.815c0.173-0.345,0.173-0.813,0-1.405l0.407,0.148c0.32,0.173,0.555,0.444,0.702,0.813 c0.075,0.124,0.111,0.457,0.111,0.999c0,0.418,0.011,0.555,0.037,0.406c0.295-0.394,0.48-0.801,0.554-1.22 c0.025-0.246-0.135-0.629-0.48-1.147c-0.443-0.542-0.714-0.888-0.813-1.035c-0.148-0.223-0.21-0.739-0.186-1.553 c0.025-0.839,0.148-1.369,0.37-1.591c0.05,0.518,0.271,0.974,0.666,1.368c0.198,0.196,0.728,0.419,1.59,0.666 c0.346,0.099,0.555,0.309,0.629,0.629C15.653,7.262,15.752,7.52,15.826,7.595z"/><path fill-rule="evenodd" clip-rule="evenodd" fill="#FFFFFF" d="M14.975,10.331c0.271,0.1,0.568,0.261,0.888,0.48 c0.32,0.248,0.58,0.482,0.776,0.704c0.172,0.222,0.235,0.382,0.186,0.48l-1.11,1.517c-0.025,0.024-0.087,0.038-0.186,0.038 c-1.356,2.269-2.158,3.574-2.404,3.92c-0.099,0.122-0.21,0.803-0.334,2.034c-0.147,1.208-0.406,2.084-0.776,2.626L5.58,31.157 c-0.197,0.172-0.641,0.185-1.332,0.037c-0.739-0.197-1.455-0.543-2.144-1.037c-0.691-0.491-1.246-1.06-1.666-1.7 c-0.37-0.593-0.505-1.011-0.407-1.259l6.437-9.062c0.37-0.544,1.11-1.072,2.22-1.591c0.271-0.123,0.641-0.296,1.109-0.518 c0.346-0.173,0.567-0.32,0.667-0.443c0.148-0.224,1.122-1.406,2.922-3.553c-0.024-0.098-0.024-0.171,0-0.221h-0.038l1.11-1.517 C14.506,10.195,14.679,10.209,14.975,10.331z"/></g></g></g><g id="guides"></g></svg>]]},
	incgrenade = {0.3867, [[<svg version="1.1" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" x="0px" y="0px" width="12.375px" height="32px" viewBox="0 0 12.375 32" enable-background="new 0 0 12.375 32" xml:space="preserve"><g id="Selected_Items"><g><g id="Selected_Items_54_"><path fill-rule="evenodd" clip-rule="evenodd" fill="#FFFFFF" d="M8.277,7.221l0.037,0.037V7.33c0,0.022,0.012,0.035,0.036,0.035 v0.073h0.036v0.037c0.023,0,0.036,0.012,0.036,0.036V7.62h0.036v0.073h0.036v0.073c0.024,0,0.036,0.012,0.036,0.035v0.109 l0.036,0.036V8.02l0.036,0.036v0.038h0.037v0.071l0.035,0.036v0.037c0.024,0,0.037,0.012,0.037,0.036v0.037l0.036,0.035v0.109 c0,0.023,0.012,0.036,0.036,0.036v0.037h0.036v0.108h0.036v0.036l0.036,0.037v0.035l0.036,0.037v0.072l0.037,0.037V9l0.035,0.037 v0.037l0.036,0.035v0.037l0.072,0.072v0.037c0.023,0,0.036,0.012,0.036,0.036l0.035,0.036V9.51l0.036,0.036v0.072l0.036,0.037 v0.036l0.036,0.036v0.036L9.319,9.8v0.073h0.036v0.072c0,0.023,0.013,0.036,0.036,0.036v0.109c0.024,0,0.036,0.013,0.036,0.036 v0.072c0.023,0,0.036,0.014,0.036,0.037v0.072H9.5v0.036l0.035,0.036v0.037l0.036,0.037v0.035l0.036,0.037v0.146l0.036,0.036 v0.073h0.036v0.108h0.036v0.072l0.036,0.036v0.036l0.036,0.036v0.073c0.024,0,0.036,0.013,0.036,0.036v0.072 c0,0.024,0.012,0.037,0.036,0.037v0.071l0.037,0.038v0.036H9.93v0.035l0.036,0.037v0.035l0.037,0.038v0.072h0.035v0.108 l0.037,0.037v0.036c0,0.023,0.012,0.037,0.036,0.037v0.035l0.036,0.037v0.108l0.072,0.072v0.073l0.036,0.036v0.037h0.036v0.071 h0.036c0,0.024,0.012,0.036,0.036,0.036v0.038l0.144,0.145v0.473l0.037,0.036v0.073c0.022,0,0.035,0.012,0.035,0.036l0.036,0.035 v0.036l0.036,0.036v0.037h0.037v0.073c0.023,0,0.036,0.013,0.036,0.036l0.036,0.035v0.037h0.036v0.036l0.072,0.074 c0,0.022,0.012,0.035,0.035,0.035v0.037h0.037v0.036l0.108,0.145v0.255c0.023,0.024,0.047,0.036,0.071,0.036v0.473 c0,0.097,0,0.169,0,0.218c0,0.073,0,0.145,0,0.218v0.182c0,0.072,0,0.145,0,0.219v0.181c0,0.097,0,0.182,0,0.255 c0,0.048,0,0.12,0,0.218c0,0.048,0,0.121,0,0.218c0,0.049,0,0.108,0,0.182v0.036c0,0.049,0,0.11,0,0.182c0,0.049,0,0.122,0,0.218 c0,0.048,0,0.121,0,0.219c0,0.048,0,0.109,0,0.182s0,0.145,0,0.217c0,0.073,0,0.146,0,0.219s0,0.146,0,0.218 c0,0.073,0,0.134,0,0.182c0,0.098,0,0.17,0,0.219c0,0.072,0,0.145,0,0.217c0,0.073,0,0.146,0,0.219s0,0.134,0,0.183 c0,0.096,0,0.168,0,0.217c0,0.072,0,0.146,0,0.219c0,0.072,0,0.145,0,0.218s0,0.145,0,0.218s0,0.146,0,0.218 c0,0.048,0,0.121,0,0.219c0,0.047,0,0.12,0,0.217c0,0.05,0,0.109,0,0.183s0,0.145,0,0.219c0,0.047,0,0.119,0,0.217 c0,0.048,0,0.121,0,0.219c0,0.048,0,0.108,0,0.182c0,0.071,0,0.145,0,0.218c0,0.072,0,0.146,0,0.218c0,0.073,0,0.146,0,0.219 c0,0.072,0,0.132,0,0.181c0,0.097,0,0.169,0,0.218c0,0.073,0,0.146,0,0.218s0,0.146,0,0.218c0,0.074,0,0.133,0,0.183 c0,0.096,0,0.169,0,0.217c0,0.073,0,0.146,0,0.219c0,0.073,0,0.146,0,0.218s0,0.146,0,0.219c0,0.071,0,0.145,0,0.218 c0,0.049,0,0.121,0,0.218c0,0.049,0,0.121,0,0.218c0,0.049,0,0.109,0,0.182s0,0.146,0,0.218c0,0.05,0,0.121,0,0.219 c0,0.047,0,0.121,0,0.218c0,0.049,0,0.108,0,0.182s0,0.146,0,0.218s0,0.146,0,0.218c0,0.073,0,0.146,0,0.219s0,0.133,0,0.182 c0,0.096,0,0.169,0,0.218c0,0.072,0,0.146,0,0.218c0,0.073,0,0.145,0,0.218s0,0.134,0,0.181c0,0.098,0,0.17,0,0.219 c0,0.073,0,0.146,0,0.218c0,0.073,0,0.158,0,0.255v0.073c-0.024,0-0.06,0-0.108,0v0.035c-0.024,0-0.036,0.013-0.036,0.037 l-0.035,0.072h-0.072v0.037l-0.036,0.035v0.037h-0.036v0.073c-0.167,0-0.335,0-0.503,0v-0.582l0.072-0.073 c0.024,0,0.037-0.011,0.037-0.036h0.036V27.82h0.035v-0.036h0.037c0-0.073,0-0.133,0-0.182c0-0.072,0-0.121,0-0.146 c0-0.072,0-0.145,0-0.217c0-0.073,0-0.146,0-0.218c0-0.073,0-0.146,0-0.218c0-0.073,0-0.146,0-0.219c0-0.048,0-0.121,0-0.217 c0-0.05,0-0.11,0-0.182c0-0.072,0-0.146,0-0.22c0-0.072,0-0.145,0-0.217c0-0.05,0-0.121,0-0.219c0-0.048,0-0.108,0-0.181 c0-0.073,0-0.146,0-0.218c0-0.073,0-0.146,0-0.218c0-0.074,0-0.146,0-0.219s0-0.134,0-0.182c0-0.098,0-0.171,0-0.219 c0-0.097,0-0.169,0-0.218c0-0.072,0-0.144,0-0.218c0-0.072,0-0.134,0-0.182c0-0.097,0-0.17,0-0.219c0-0.097,0-0.169,0-0.217 c0-0.072,0-0.146,0-0.219c0-0.071,0-0.146,0-0.218s0-0.146,0-0.218s0-0.146,0-0.218c0-0.049,0-0.121,0-0.218 c0-0.049,0-0.109,0-0.182c0-0.073,0-0.146,0-0.219s0-0.145,0-0.218c0-0.048,0-0.122,0-0.218c0-0.048,0-0.108,0-0.182 c0-0.074,0-0.146,0-0.219c0-0.097,0-0.169,0-0.218c0-0.072,0-0.146,0-0.218s0-0.133,0-0.181c0-0.098,0-0.171,0-0.22 c0-0.096,0-0.169,0-0.218c0-0.072,0-0.145,0-0.216c0-0.074,0-0.135,0-0.183c0-0.098,0-0.182,0-0.254c0-0.074,0-0.146,0-0.219 c0-0.048,0-0.121,0-0.218c0-0.049,0-0.109,0-0.181c0-0.074,0-0.147,0-0.22s0-0.145,0-0.218c0-0.049,0-0.121,0-0.218 c0-0.049,0-0.108,0-0.182s0-0.145,0-0.218s0-0.146,0-0.218c0-0.073,0-0.146,0-0.218s0-0.134,0-0.183c0-0.098,0-0.169,0-0.219 c0-0.096,0-0.169,0-0.217c0-0.072,0-0.146,0-0.219c0-0.072,0-0.132,0-0.181c0-0.097,0-0.171,0-0.218c0-0.098,0-0.169,0-0.219 c0-0.072,0-0.146,0-0.218s0-0.146,0-0.218c0-0.073,0-0.146,0-0.219c0-0.071,0-0.145,0-0.218c0-0.048,0-0.121,0-0.219 c0-0.048,0-0.108,0-0.182v-0.035c-0.024,0-0.048,0-0.072,0l-0.036-0.036V14.27l-0.037-0.036v-0.037h-0.036V14.16l-0.036-0.036 v-0.037l-0.036-0.035v-0.073l-0.036-0.036v-0.037h-0.037v-0.072h-0.036v-0.036l-0.036-0.037h-0.036 c0-0.024-0.012-0.035-0.037-0.035v-0.073c0-0.024-0.011-0.035-0.035-0.035v-0.038H9.966v-0.072c-0.023,0-0.036-0.012-0.036-0.037 c0-0.023-0.012-0.035-0.035-0.035v-0.036H9.858V13.36l-0.036-0.073c0-0.022-0.012-0.036-0.036-0.036v-0.036l-0.108-0.071v-0.037 H9.643v-0.072c-0.024,0-0.036-0.014-0.036-0.037l-0.036-0.035v-0.037H9.535v-0.036c-0.024-0.025-0.048-0.036-0.071-0.036v-0.037 H9.427v-0.036c-0.024,0-0.036-0.013-0.036-0.036v-0.072c-0.023,0-0.036-0.013-0.036-0.037l-0.036-0.036v-0.036H9.284 l-0.108-0.145v-0.038c-0.023,0-0.035-0.012-0.035-0.036H9.104v17.729l-0.143,0.037c-0.048,0.024-0.097,0.06-0.145,0.108H8.781 c-0.575,0.315-1.113,0.534-1.617,0.654c-0.599,0.194-1.173,0.315-1.725,0.363c-0.503,0.097-0.994,0.121-1.473,0.073 c-0.454-0.049-0.861-0.109-1.22-0.182c-0.79-0.17-1.401-0.375-1.832-0.618c-0.216-0.097-0.383-0.181-0.503-0.254 c-0.168-0.073-0.251-0.121-0.251-0.146l-0.108-0.037V12.308c-0.095-0.195-0.06-0.4,0.108-0.618c0-0.024,0.012-0.049,0.035-0.072 c0.072-0.024,0.144-0.036,0.216-0.036v-0.038c0.12-0.047,0.276-0.084,0.467-0.107c0.191-0.024,0.467-0.049,0.826-0.074 c0.191-0.048,0.395-0.084,0.61-0.108c0.167-0.023,0.287-0.035,0.359-0.035c-0.023-0.219-0.023-0.388,0-0.509 c0.048-0.072,0.084-0.134,0.109-0.182l0.107-0.146V8.783H2.853c-0.12,0-0.251-0.012-0.395-0.037c-0.168,0-0.335,0-0.502,0V8.238 h0.035V8.201c0.023,0,0.048,0,0.071,0c-0.048-0.072-0.048-0.182,0-0.327c0-0.072,0.048-0.193,0.144-0.362 c0.072-0.169,0.192-0.267,0.359-0.291h4.239l0.18,0.072v0.146c0.144-0.289,0.323-0.545,0.539-0.763V6.64c0-0.023,0-0.048,0-0.073 c0.023,0,0.06,0,0.107,0c0-0.023,0.012-0.035,0.037-0.035v0.035c0.12,0,0.239,0,0.359,0v0.4l0.036,0.036v0.036 c0.047,0,0.108,0,0.18,0v0.036l0.035,0.036V7.221z M3.573,14.559c-0.576,0.51-1.031,1.141-1.367,1.89 c-0.478,1.042-0.718,2.096-0.718,3.161c0,0.8,0.096,1.538,0.288,2.216c0.239,0.849,0.707,1.453,1.401,1.817 c0.335,0.193,0.707,0.303,1.113,0.326c-0.694-0.436-1.066-1.15-1.113-2.144c-0.048-0.751,0.216-1.441,0.79-2.069 c0.145-0.123,0.241-0.195,0.289-0.219c-0.263,0.751,0.023,1.524,0.862,2.325c0.407,0.411,0.707,0.884,0.897,1.416 c0.095,0.267,0.072,0.448-0.072,0.546c0.312,0.121,0.635-0.014,0.97-0.399c0.014-0.017,0.026-0.034,0.036-0.056 c0.489-0.306,0.8-0.856,0.934-1.652c0.144-0.944-0.011-1.864-0.467-2.761c-0.311-0.654-0.682-1.199-1.113-1.635 C5.775,16.716,5.417,16,5.225,15.177c-0.24-0.896-0.107-1.658,0.395-2.288C4.948,13.397,4.267,13.955,3.573,14.559z M8.745,25.241c0-0.023,0-0.062,0-0.108v-0.11c-1.94,0.292-3.292,0.436-4.059,0.436c-0.791,0-2.203-0.144-4.238-0.436 c0,0.025,0,0.049,0,0.073s0,0.048,0,0.072v0.51c0,0.022,0,0.048,0,0.072c0,0.023,0,0.048,0,0.072v0.072c0,0.024,0,0.049,0,0.072 c0,0.024,0,0.05,0,0.073c0,0.024,0,0.048,0,0.074v0.072c0,0.023,0,0.047,0,0.071s0,0.049,0,0.072v0.038 c2.06,0.362,3.497,0.544,4.31,0.544c0.862,0,2.191-0.17,3.987-0.508v-0.4c0-0.024,0-0.048,0-0.072v-0.037 c0-0.048,0-0.085,0-0.108c0-0.024,0-0.048,0-0.073v-0.035c0-0.024,0-0.049,0-0.073s0-0.062,0-0.108v-0.038 c0-0.024,0-0.048,0-0.071c0-0.025,0-0.049,0-0.072C8.745,25.289,8.745,25.265,8.745,25.241z M6.338,23.969h-0.18v0.037 L6.338,23.969z M5.439,24.042v-0.036c-0.023,0-0.036,0.012-0.036,0.036H5.439z M3.859,24.151c-0.07-0.008-0.136-0.02-0.198-0.036 l-0.018,0.036C3.716,24.151,3.788,24.151,3.859,24.151z"/></g></g></g><g id="guides"></g></svg>]]},
	fire = {1.0397, [[<svg version="1.1" id="Layer_1" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" x="0px" y="0px" width="30.093px" height="28.944px" viewBox="0 0 30.093 28.944" enable-background="new 0 0 30.093 28.944" xml:space="preserve"><g id="guides"></g><path fill-rule="evenodd" clip-rule="evenodd" fill="#FFFFFF" d="M11.443,8.821c0.219,1.083,0.241,2.045,0.064,2.887 c-0.177,0.843-0.336,1.433-0.479,1.771c0.133-0.249,0.324-0.531,0.572-0.848c0.708-0.906,1.706-1.641,2.993-2.201 c0.661-0.29,1.171-0.501,1.527-0.635c1.144-0.434,1.763-0.995,1.859-1.687c0.082-0.563,0.043-1.144-0.118-1.74 c-0.155-0.586-0.193-1.108-0.117-1.567c0.099-0.591,0.483-1.083,1.153-1.478c0.258-0.152,0.51-0.269,0.757-0.35 c-0.037,0.022-0.114,0.263-0.229,0.722c-0.115,0.458-0.038,1.018,0.234,1.676c0.271,0.658,0.472,1.133,0.604,1.42 c0.132,0.29,0.241,0.853,0.324,1.689c0.084,0.838-0.127,1.822-0.629,2.952c-0.502,1.132-1.12,1.89-1.854,2.275 c-0.732,0.386-1.145,0.786-1.237,1.203c-0.092,0.419,0.087,0.755,0.535,1.013s0.927,0.282,1.436,0.074 c0.577-0.238,0.921-0.741,1.031-1.508c0.108-0.751,0.423-1.421,0.945-2.009c0.393-0.438,0.943-0.873,1.654-1.3 c0.24-0.143,0.532-0.285,0.879-0.43c0.192-0.078,0.47-0.191,0.835-0.338c0.622-0.276,1.075-0.65,1.358-1.123 c0.298-0.491,0.465-1.19,0.505-2.096c0.011-0.284,0.009-0.571-0.004-0.862c0.446,0.265,0.796,0.788,1.048,1.568 c0.251,0.782,0.32,1.457,0.206,2.025c-0.113,0.568-0.318,1.059-0.611,1.472c-0.295,0.412-0.695,0.901-1.201,1.469 c-0.519,0.578-0.864,0.985-1.04,1.222c-0.318,0.425-0.503,0.795-0.557,1.109c-0.044,0.269-0.05,0.763-0.016,1.481 c0.05,1.016,0.075,1.695,0.075,2.037c0,1.836-0.334,3.184-1.004,4.045c-0.874,1.123-1.731,1.902-2.568,2.336 c-0.955,0.49-2.228,0.736-3.813,0.736c-1.717,0-3.154-0.246-4.313-0.736c-1.237-0.525-2.083-1.303-2.541-2.336 c-0.394-0.885-0.668-1.938-0.827-3.158c-0.05-0.385-0.083-0.76-0.103-1.127c-0.49-0.092-0.916,0.209-1.278,0.904 c-0.36,0.693-0.522,1.348-0.484,1.957c0.039,0.611,0.246,1.471,0.625,2.578c0.131,0.449,0.185,0.801,0.161,1.051 c-0.031,0.311-0.184,0.521-0.456,0.631c-0.321,0.129-0.688,0.178-1.1,0.146c-0.463-0.037-0.902-0.174-1.319-0.41 c-1.062-0.604-1.706-1.781-1.937-3.531c-0.229-1.75-0.301-3.033-0.214-3.85c0.086-0.814,0.342-1.613,0.77-2.398 c0.428-0.783,0.832-1.344,1.213-1.681c0.382-0.338,0.893-0.712,1.532-1.122c0.64-0.408,1.108-0.745,1.405-1.008 c0.438-0.383,0.715-0.807,0.83-1.271C8.824,9.292,8.52,7.952,7.613,6.456C7.33,5.988,7.005,5.532,6.637,5.087 c0.837,0.111,1.791,0.49,2.865,1.138C10.576,6.872,11.223,7.737,11.443,8.821z"/></svg>]]},
}

return({M=function(X,X,J)X=(J[0X4D8F]);return X;end,qU=function(X,X,J,e)(J)[0X5]=(X);e=(0X6e);return e;end,Fs=function(X,X,J,e,C)C=nil;J=nil;e=(nil);X=nil;return X,e,C,J;end,xf=function(X,X,J,e,C)if not(C)then(X[0X22])[e]=J;else X[0X22][e]={J,(X[0XC](J))};end;end,Ws=function(X,X)X=nil;return X;end,ws=function(X)end,JU=function(X,X)X=nil;return X;end,wf=function(X,J,e,C)local W;for y=0x51,0x115,0X47 do W,C=X:Mf(C,e,J,y);if W==0x93EC then break;else if W~=-0X1 then else return-1,C;end;end;end;return nil,C;end,Ls=function(X,X)(X)[27],X[0X1c]=X[0X17],(X[0X17]);end,s=function(X,J,e,C)if C==44 then X:I(J);return 0x10ea,C;elseif C==0x6A then(J)[11]=(function(W,y,I)I=I or 1;y=y or#W;if not((y-I+0X1)>0X1f3D)then return J[0X3](W,I,y);else return J[0X9](I,W,y);end;end);if not e[0X718A]then C=-3209987969+(e[0X4099]+e[0X78Dd]+X.S[0X9]+X.S[0x6]+e[2550]);e[0x718A]=(C);else C=X:_(C,e);end;else if C~=65 then else(J)[12]=X.g;if not e[13153]then C=-137+(((X.S[0X1]<X.S[0x4]and e[2550]or e[0x78dD])>=X.S[5]and X.S[5]or e[29066])+e[0X3124]+e[0X66F0]);(e)[0X3361]=C;else C=e[13153];end;end;end;return nil,C;end,Ts=function(X)end,ms=function(X,X)X[0X18]=X[0X18]+4;end,Es=function(X,X)X=(0X0);return X;end,tU=function(X,X,J,e,C)(e)[C+0X1]=X;(e)[C+0X2]=J;end,qs=function(X,J,e,C,W)W=0X72;repeat if W==41 then J[29]=(function()local y,I,T,f;f,T,y,I=X:Fs(f,I,T,y);goto W;::X::;X:cs();goto C;::J::;X:ms(J);goto y;::e::;I,y,T,f=X:xs(f,I,T,J,y);goto X;::C::;X:fs();goto J;::W::;X:is();goto e;::y::;do return(X:os(T,y,I,f));end;end);break;else if W==114 then C=pcall;if not e[0X5ee8]then W=(-0X4dc34E9A+((X.S[1]>X.S[0x1]and e[24343]or e[13153])-X.S[5]+X.S[0X2]+e[0X4d8F]));(e)[24296]=W;else W=X:Gs(W,e);end;end;end;until false;(J)[0X1E]=X.n;J[31]=function()local e,y,I;I=X:rs(I);goto f;::I::;e,y=X:Zs(I);if e~=-2 then else return y;end;::T::;I=X:ks(I,J);goto U;::f::;I=X:Ms(I,J);goto T;::U::;X:ws();goto I;end;(J)[32]=function()local e,y,I,T,f=(104);while true do if e>39 then e=(0X27);T,f=J[0x001D](),J[0x1d]();else if not(e<104)then else y,f,I=X:Rs(f,J,T);if y==0x67cc then break;else if y~=-2 then else return I;end;end;end;end;end;return f*J[23]+T;end;return C,W;end,js=function(X)end,Uf=function(X,J,e,C,W,y,I,T,f,U)local m,E,r,d,V,g=C[14](J),40;repeat if E<26 then E=X:qU(y,f,E);elseif E>0X6E then f[0X3]=W;break;elseif E>0X5c and E<110 then V,E=X:BU(V,C,E,J);elseif E>0X67 and E<0X0075 then E=(117);(f)[0X4]=(V);else if E>0X1a and E<49 then E=(103);d=C[0xE](J);elseif E<103 and E>0X31 then f[9]=(T);E=(0XB);elseif E>0X28 and E<92 then E=(0x5C);f[1]=d;else if not(E<0X28 and E>11)then else E=(49);g=57;end;end;end;until false;e=(nil);I=nil;f[0xA]=U;(f)[7]=m;for D=0X1,J,0x1 do local J,K,z;z,K,J=X:vU(K,z,J);goto E;::m::;z,K,J=X:LU(J,C,K,z);X:OU();goto r;::E::;X:bU();goto m;::r::;local N,S,u;N,S,u=X:dU(C,N,u,S);local A,R,c,n;c,R,A,n=X:IU(n,A,R,c);E=0X2A;repeat if E>0X3f then if not(E<=0X5B)then E,n,R=X:_U(D,U,z,A,J,n,R,y,E,u);else if not(E<91)then c=((N-S)/8);E=(126);else d[D]=K;E=(96);end;end;else if not(E>0x12)then if not(E<=1)then X:zU(U,g,D,u,m,C,f,R);break;else u,A,E=X:lU(u,E,A,z,J);end;else if E==42 then E,S=X:sU(N,E,S);else E=0x12;W[D]=c;end;end;end;until false;if S==0X0 then W[D]=c;elseif S==1 then W[D]=(D-c);elseif S==0x5 then if not(C[0Xa])then(V)[D]=(C[34][c]);else local J,U;J,U=X:DU(J,U);goto V;::d::;U=X:eU(f,U,J);goto g;::V::;J=X:HU(c,C,J);goto d;::g::;(J)[U+0X2]=D;J[U+3]=(0x4);end;else if S==2 then(W)[D]=D+c;else if S==7 then local J;J=X:JU(J);goto K;::D::;X:WU(V,J,C,c,D);goto z;::K::;r,J=X:KU(g,C,J);if r==0x71A7 then do goto D;end;else if r==-0X1 then return e,-1,I;end;end;::z::;end;end;end;if A==7 then u=#C[39];(C[39])[u+1]=(T);(C[39])[u+0x2]=(D);(C[0X27])[u+3]=n;elseif A==1 then y[D]=D-n;elseif A==0x2 then X:uU(n,D,y);elseif A==5 then if not(C[10])then T[D]=(C[0X22][n]);else local J,W;J,W=X:Tf(W,J);goto S;::N::;X:Xf(D,f,g,J,W);goto u;::S::;W,J=X:Cf(n,C,W,J);goto N;::u::;J[W+0X3]=(0X9);end;else if A==0 then if g==0X39 then else while C[0X2C]~=C[44]do(C)[26]=C[23];end;end;(y)[D]=(n);end;end;end;e=C[0X021]();return e,nil,I;end,MU=function(X,X,J,e,C,W)W=(X[0x0021]()-0X125D2);C=X[14](W);e=nil;J=nil;return C,W,J,e;end,jf=function(X,X,J,e)e[J]=X[0X2e]();end,If=function(X,X,J)X=J[0X1F]();return X;end,sU=function(X,X,J,e)e=X%0x8;J=0x1;return J,e;end,Lf=function(X,X,J)J=X[28]();return J;end,Tf=function(X,X,J)J=nil;X=nil;return J,X;end,ts=function(X)end,KU=function(X,X,J,e)e=#J[0X27];if X~=0XC5 then else return-1,e;end;return 29095,e;end,Zs=function(X,J)do return-2,(X:Vs(J));end;return nil;end,o=true,XZ=function(X,X,J)J=(0x4+((X[0X5fC4]+X[0X5f17]>X[19855]and X[17393]or X[0X718A])-X[626]==X[8633]and X[26707]or X[0X1740]));(X)[14316]=(J);return J;end,yU=function(X,X,J,e,C)C=1;J=X[0X1B](e,0,0X17);return C,J;end,zf=function(X,J,e,C)local W,y,I,T;for f=0x2,0XDF,70 do T,y,I=X:mf(f,I,T,J);if y==38878 then break;end;end;local f;for U=0x29,0XB9,51 do y,f,W=X:lf(T,J,f,I,U);if y==45593 then break;else if y==-0X2 then return-2,C,e,W;else if y~=-0X1 then else return-1,C,e;end;end;end;end;local W=J[0Xe](f);C=(nil);for y=66,0Xf0,87 do if y==66 then(J)[39]=J[0Xe](f*3);for I=1,f do X:jf(J,I,W);end;for I=1,#J[39],0X3 do(J[0X027][I])[J[0X27][I+1]]=(W[J[0x27][I+2]]);end;if not(T)then else X:Nf();goto R;::A::;X:pf(W,J);goto c;::R::;X:Pf(J);goto A;::c::;end;else if y==240 then X:tf(J);else if y==153 then C=X:Ef(J,C,W);end;end;end;end;e=55;return nil,C,e;end,rU=function(X,J,e,C)e=(function(...)return(...)[...];end);if not J[8633]then C=(-58+((J[13607]>J[5952]and X.S[8]or J[0X1b90])-X.S[4]-C<J[0X586D]and J[17845]or J[0X7425]));(J)[0X21b9]=(C);else C=J[0X21b9];end;return C,e;end,QU=function(X)end,Qf=function(X)end,l=function(X,J,e,C,W)local y;W=123;while true do y,J,W=X:d(C,W,J,e);if y==0X1e4e then break;end;end;e[0XB]=nil;e[12]=(nil);(e)[13]=(nil);W=(106);while true do y,W=X:s(e,C,W);if y~=0X10Ea then else break;end;end;(e)[14]=(function(C)if C<=0x186a0 then return{e[0XB](e[0X7],C,0x1)};else return{};end;end);(e)[0xF]=X.h;e[0X10]=X.Q;(e)[17]=(nil);e[18]=nil;return J,W;end,ZU=function(X,J,e,C,W)W=e.typeof(e[X.x]("i\110t\x364\z  \x5Ft"));C=e.typeof(e.new("uin\11664_\116"));J=e[X.f](e[X.x]("c\111mple\120"));return J,C,W;end,Bf=function(X)end,X=function(...)(...)[...]=nil;end,fs=function(X)end,sf=function(X,J,e,C)if not(J>0Xb5)then C=X:_f(J,e,C);else if not(J>0Xc6)then C=X.o;else local W=0X57;while true do if not(W<=74)then if J==0Xe8 then C=X:vf(C,e);else C=X:Lf(e,C);end;W=(0X4A);else break;end;end;end;end;return C;end,Ds=function(X,J,e,C,W)if W==0X0 then return-2,C,J,(X:Ps(e));else X:ts();goto w;::n::;J=X:Es(J);goto F;::w::;C=X:zs(C);goto n;::F::;end;return nil,C,J;end,bU=function(X)end,Cf=function(X,X,J,e,C)C=(J[34][X]);e=#C;return e,C;end,Bs=function(X,X)X[33]=(nil);(X)[0X22]=nil;X[0X023]=(nil);end,af=function(X,X,J)J[0Xb]=(X);end,Ss=function(X,X,J,e,C,W)X=(W/C[0X16][e])%C[22][J];return X;end,i=false,pU=function(X,X,J,e)e=(X[0x22][J]);return e;end,Rf=function(X,J,e,C,W,y)local I,T;if C<=0x1C then X:ff();return W,0Xcee2,C;else if not(y>46)then I,W=X:wf(y,J,W);if I~=-1 then else return W,-1,C;end;else if y>0X4B then I,W,T=X:rf(y,e,J,W);if I==-0X2 then return W,-2,C,T;end;else W=J[0x1d]();end;end;C=(28);end;return W,nil,C;end,b=function(X,J,e,C)if e<0x7B then C[0XA]=X.y;return 24517,e;else e=X:O(e,J,C);end;return nil,e;end,c="Your environment does not support LuaJIT's FFI library, therefore you cannot use LL/ULL/i suffixes.",rf=function(X,J,e,C,W)if C[0X2c]~=0XEC then C[0X16]=C[0X2C];while C[44]do return-0X2,W,e;end;elseif C[0X1C]==C[0X7]then for e=0X26,143,0X69 do X:Gf(e,C);end;else if not(J<=90)then W=X.i;else W=C[26]();end;end;return nil,W;end,n=bit.bxor,bf=function(X)end,PU=function(X,X,J)(X)[J+3]=(7);end,Ns=function(X)end,of=function(X,X)(X)[19],X[0X24]=-25,(-X[0X28]);end,Mf=function(X,J,e,C,W)local y;if W==0XDf then return 0X93eC,J;else if W==0X51 then y=X:Vf(e);if y~=-0x1 then else return-0X1,J;end;else if W~=152 then else J=X:kf(J,C,e);end;end;end;return nil,J;end,hf=function(X,J,e,C,W,y,I,T)if not(J>113)then y=W/4;else C=X:gf(W,T,C,e,y,I);end;return C,y;end,vs=function(X,X)X[0X24]={};end,AU=function(X,X)X=1;return X;end,ns=function(X,J,e,C,W)J[0X19]=(nil);J[0X1A]=(nil);J[27]=(nil);C=0X6;while true do if C==45 then(J)[23]=(4294967296);if not(not e[626])then C=(e[0X00272]);else e[0X7F29]=2717430522+((e[0x3124]-X.S[9]<X.S[0X08]and e[0X4d8F]or X.S[8])-X.S[0X9]-e[0X9f6]);C=(0X651D16DF+((e[0X9f6]~=X.S[7]and e[12580]or e[29733])+e[0X3124]+X.S[0X2]-X.S[8]));e[626]=(C);end;else if C==0X31 then X:Xs(J);break;elseif C==0X6 then J[22]={[0]=1,2,4,8,16,0X20,64,0X80,0X100,0X200,0X400,2048,0x01000,0X002000,0x4000,32768,65536,0X20000,0x40000,0x80000,0X00100000,2097152,0X400000,8388608,16777216,0X2000000,67108864,134217728,268435456,0X20000000,0X40000000,2147483648,4294967296};if not e[8607]then C=(-348071988+((X.S[0X7]+e[30941]+X.S[0X2]>=X.S[0X1]and X.S[0x4]or e[0x5da2])-X.S[0x5]));e[8607]=C;else C=(e[0X219F]);end;elseif C==26 then(J)[26]=function()local y,I=J[0X10](J[21],J[24],J[24]+1);(J)[0x18]=J[0X18]+0X2;return I*0X100+y;end;if not(not e[0X586d])then C=e[22637];else C=X:Cs(e,C);end;elseif C==40 then C=X:Ys(e,C,J);else if C~=103 then else C=X:hs(C,J,e);end;end;end;end;J[28]=function()local e,y;e,y=X:As(J);if e~=-0X2 then else return y;end;end;W=(nil);J[29]=(nil);return C,W;end,v=function(X,J,e,C,W)(e)[0X5]=(nil);e[6]=nil;J=(103);while true do if J>0X67 then e[0X6]=X.U;break;elseif J>92 and J<110 then(e)[0X1]=(nil);if not(not C[0X4695])then J=X:Z(J,C);else J=X:V(C,J);end;elseif J<92 and J>0X01a then(e)[0X3]=X.C;if not(not C[0X5da2])then J=(C[23970]);else J=X:k(J,C);end;elseif J>0xb and J<49 then(e)[2]=X.YZ;if not C[19855]then J=(-1282691466+((X.S[4]==X.S[0X2]and X.S[5]or X.S[0X4])-X.S[6]-X.S[0X6]+X.S[0X5]));(C)[19855]=(J);else J=X:M(J,C);end;else if J<103 and J>0X31 then e[4]=(next);if not(not C[0X3124])then J=X:R(C,J);else J=X:w(J,C);end;else if J<0X001A then J=X:B(C,J,e);end;end;end;end;(e)[0X7]=({});e[8]=X.Y;e[0X9]=nil;W=(nil);(e)[0xa]=nil;return W,J;end,uf=function(X,J)J[36][16]=X.X;(J[36])[0X2]=(J[28]);(J[0X24])[0xB]=(J[25]);end,Pf=function(X,X)X[0X24][7]=(X[34]);end,k=function(X,J,e)e[0X4099]=(-2250170788+(X.S[0X6]+X.S[0X1]-X.S[5]+X.S[9]+e[0X4D8F]));J=(4453475056+((e[26352]<=e[0X66f0]and X.S[0X9]or X.S[4])-X.S[0X9]-X.S[0X6]-X.S[0x8]));(e)[23970]=J;return J;end,TZ=function(X)end,tf=function(X,X)(X)[0X22]=(nil);end,IU=function(X,X,J,e,C)J=(nil);e=(nil);C=nil;X=(nil);return C,e,J,X;end,V=function(X,J,e)(J)[0X66F0]=(-3960917663+((X.S[7]-X.S[0X5]>X.S[0X4]and e or X.S[5])+X.S[0X8]+e));e=6229590189+((X.S[0X1]<=X.S[6]and X.S[0X4]or X.S[0x5])-X.S[1]-X.S[0x3]-X.S[8]);J[18069]=(e);return e;end,K=function(X)end,lf=function(X,J,e,C,W,y)local I,T;if y~=92 then(e)[0XA]=J;for y=0X1,W,1 do local W,f;for U=0x78,0X264,0X7b do if U==489 then elseif U==120 then W=nil;else if U==612 then X:xf(e,W,y,J);elseif U==243 then f=e[0X19]();else if U==366 then if not(f<=109)then for y=85,190,0X068 do if y>85 then X:Bf();break;else if not(y<0XBD)then else W=X:sf(f,e,W);end;end;end;else I,W,T=X:qf(J,f,W,e);if I==-2 then return-0x2,C,T;else if I~=-1 then else return-0x1,C;end;end;end;end;end;end;end;end;else C=(e[0x21]()-53210);return 45593,C;end;return nil,C;end,DU=function(X,X,J)X=nil;J=nil;return X,J;end,UZ=function(X,J,e,C,W,y,I,T,f,U)while true do if y>0X40 and y<0X72 then y,J=X:Jf(W,y,J);elseif y<0X5E and y>37 then f[36][0X6]=f[0X25];if not W[20279]then y=2264512484+((W[0X4099]<=W[29733]and W[20886]or W[17845])-W[0X272]-W[24516]-X.S[0X2]);W[20279]=(y);else y=X:Wf(y,W);end;elseif y>0X5E then f[0X024][0x3]=(I);break;elseif y<64 and y>31 then if J~=0Xa6 then else X:Kf(f);goto k;::o::;X:uf(f);goto q;::k::;X:TZ();goto o;::q::;f[36][1]=f[0x28];end;if not W[0X6d38]then y=(-178+(W[2550]+W[0x586D]+W[0X7425]-W[16537]+W[0X5fC4]));(W)[27960]=(y);else y=(W[0X6D38]);end;else if y<37 then(f[0X0024])[0X12]=X;if not(not W[0X632A])then y=X:SZ(W,y);else W[14465]=0X3d+((W[0X7F29]-W[18995]==X.S[0X6]and W[19855]or W[22637])+W[0X43f1]-W[0X43F1]);y=(0X2F+((X.S[5]-W[29733]>=W[0X9f6]and W[30941]or W[6978])+W[0X13C6]+W[12580]));(W)[25386]=y;end;end;end;end;y=110;while true do if y==80 then return J,{f[45](U,C)},y;else if y==117 then f[36][12]=C;(f[36])[0X0F]=T;if not W[0x525B]then y=(-30+((W[24516]+W[0X5DA2]~=W[23549]and W[0X7f29]or W[19855])+W[22637]+W[0X5196]));W[21083]=(y);else y=(W[21083]);end;else if y~=110 then else(f[0X24])[0X5]=(e);f[0X24][17]=(f[45]);if not W[0X37ec]then y=X:XZ(W,y);else y=X:CZ(y,W);end;end;end;end;end;return J,nil,y;end,is=function(X)end,mU=function(X)end,x="ne\119",R=function(X,X,J)J=X[0X03124];return J;end,Xf=function(X,J,e,C,W,y)if C~=0X39 then else X:Sf(J,e,y,W);end;end,qf=function(X,J,e,C,W)local y,I,T=113;while true do C,I,y,T=X:Rf(W,J,y,C,e);if I==52962 then break;elseif I==-2 then return-0x2,C,T;else if I~=-0X1 then else return-0x1,C;end;end;end;return nil,C;end,hs=function(X,J,e,C)(e)[25]=(function()local W,y,I;I=X:as(I);goto i;::x::;W,I,y=X:ys(I,e);if W~=-2 then else return y;end;::i::;goto x;end);if not C[0x13c6]then J=X:gs(C,J);else J=(C[0X13c6]);end;return J;end,gU=function(X)end,E=function(X,J,e,C,W)local y;J=(0X21);while true do y,J=X:P(W,J,e);if y~=62189 then else break;end;end;for e=0,0XfF,1 do X:t(C,e,W);end;return J;end,Ef=function(X,X,J,e)J=e[X[0X21]()];return J;end,Vf=function(X,X)if X[0X2c]~=236 then return-0x1;end;return nil;end,e=function(X,J,e,C,W)if W<0X5d then J={};return 0XBa50,J,W;else if not(W>0X18)then else W=X:D(W,C,e);end;end;return nil,J,W;end,ys=function(X,X,J)X=J[0X10](J[0x15],J[0X18],J[0x18]);J[0X18]=(J[24]+0X001);do return-0X2,X,X;end;return nil,X;end,Yf=function(X)end,t=function(X,X,J,e)e[0X5][J]=X(J);end,as=function(X,X)X=(nil);return X;end,Gs=function(X,X,J)X=J[24296];return X;end,LU=function(X,X,J,e,C)X=J[35]();e=J[0X023]();C=J[0X23]();return C,e,X;end,ls=function(X,X,J,e)J=(-1)^X[0X1B](e,0X1F,0X1);return J;end,ss=function(X,X,J,e,C,W,y)W=(X[27](y,0,20)*X[0X017]+e);J=(nil);C=(nil);return C,W,J;end,kU=function(X,J,e,C,W,y,I,T)local f,U,m;(e)[0X2b]=nil;(e)[44]=nil;T=(0X3b);repeat f,I,U,C,m,T=X:VU(T,W,U,m,e,C,y,I);if f==17494 then break;end;until false;do f,e=J(X.m,'\102fi');if not(f and e)then else m,U,I=X:ZU(m,e,U,I);end;end;return C,I,T;end,jU=function(X,X,J)X=(nil);J=nil;return X,J;end,w=function(X,J,e)e[0X4a33]=-2717430381+(J+X.S[0X5]-e[0X5Da2]+e[26352]<X.S[8]and X.S[0X9]or e[18069]);J=-3960917551+(J-e[0x4695]-X.S[0X5]+e[26352]>J and X.S[4]or X.S[0X8]);(e)[12580]=J;return J;end,C=unpack,Sf=function(X,X,J,e,C)local W=34;while true do if W>0X19 then W=(25);(C)[e+1]=(J);else if W<0X0022 then C[e+0X2]=(X);break;end;end;end;end,y=nil,zU=function(X,J,e,C,W,y,I,T,f)if W==0 then J[C]=f;elseif W==0X1 then(J)[C]=(C-f);elseif W==5 then if not(I[0Xa])then X:EU(f,C,I,y,e);else local e,U;e,U=X:jU(e,U);goto Q;::B::;U=X:NU(U,e);goto G;::Q::;e=X:pU(I,f,e);goto B;::b::;X:PU(e,U);goto Y;::G::;X:tU(T,C,e,U);goto b;::Y::;end;else if W==0X2 then J[C]=C+f;else if W==0X7 then local X=(#I[39]);I[39][X+1]=y;I[39][X+0x2]=C;(I[39])[X+0X3]=(f);end;end;end;end,Us=function(X,X,J)X=J[0x3e74];return X;end,fU=function(X,J,e,C,W)local y,I,T,f;for U=38,264,107 do y,C,f,T,I=X:xU(C,e,J,f,T,U,W);if y==0XF048 then break;else if y==-0x2 then return-2,C,I;else if y~=-1 then else return-1,C;end;end;end;end;return-2,C,f*(0X2^(T-127))*(J/(0X2^0X17)+C);end,ds=function(X,J,e,C)(J)[0X24]=(nil);C=(0X64);repeat if C==54 then X:vs(J);break;elseif C==0X73 then J[0X23]=function()local W=J[33]();if J[27]~=J[0X12]then else for y=0X33,0X3A,0X7 do if y>51 then X:Ls(J);else if not(y<58)then else J[0X1d]=0X9e and J[0XB];end;end;end;end;for y=107,0Xcf,64 do if y~=171 then if W>=J[13]then return W-J[18];end;else return(X:Os(W));end;end;end;if not(not e[17393])then C=(e[17393]);else C=-7133903034+(X.S[0X5]-e[15988]+e[13644]+X.S[7]+X.S[0X9]);e[0x43F1]=C;end;else if C~=0X64 then else(J)[0X21]=function()local W,y;for I=83,0X9C,33 do if not(I<116)then if J[0X7]==J[11]then else y,W=X:bs(W,y,J);end;break;else W=0X00;y=1;end;end;return W;end;J[34]=nil;if not(not e[0X3527])then C=(e[0X003527]);else(e)[12378]=-3572451460+(e[0X718A]+X.S[4]+X.S[2]-e[0X586d]-e[22637]);C=(-3960917447+((e[0X4099]-e[29733]<X.S[5]and e[0X4d8F]or e[28731])+e[0X00718A]>e[0X354C]and X.S[8]or X.S[0x7]));(e)[13607]=C;end;end;end;until false;(J)[0X25]=nil;J[0X26]=(nil);return C;end,Hf=function(X,X,J)J=(X[23549]);return J;end,vU=function(X,X,J,e)e=(nil);X=(nil);J=(nil);return J,X,e;end,ps=function(X,X,J,e)X=e[0X1b](J,20,0XB);return X;end,VU=function(X,J,e,C,W,y,I,T,f)if J==0X3B then f=e;if not T[6978]then J=-3456605734+((T[0X703B]==T[14767]and J or X.S[7])+T[0X4d8F]+T[24343]+T[0x5ee8]);(T)[0X1B42]=(J);else J=X:oU(T,J);end;elseif J==31 then(y)[43]=(setfenv);y[0X2C]=0XEC;return 0X4456,f,C,I,W,J;else if J==37 then C=e;if not(not T[24516])then J=T[24516];else J=-51+((T[10334]-T[7056]+T[0X718A]>T[15988]and T[15988]or T[0x4d8f])>T[0X219f]and T[0X3527]or T[0X586d]);T[0X5fC4]=J;end;else if J==0X40 then W=e;if not(not T[0X6853])then J=(T[0x6853]);else J=X:GU(J,T);end;else if J==0X5E then J,I=X:rU(T,I,J);end;end;end;end;return nil,f,C,I,W,J;end,f="\x74ypeo\102",U=setmetatable,u=function(X)end,SZ=function(X,X,J)J=X[25386];return J;end,OU=function(X)end,zs=function(X,X)X=0x1;return X;end,mf=function(X,J,e,C,W)if J<0X08E and J>2 then e=X:cf(W,e);else if J>0X48 then C=W[25]()~=0;return C,38878,e;else if not(J<0X48)then else(W)[1]=({});end;end;end;return C,nil,e;end,Ff=function(X,J)local e,C;if J[44]~=193 then else e,C=X:nf(J);if e==-0X2 then return-2,C;end;end;return nil;end,UU=function(X,X,J,e,C)C=e[29]();X=(nil);J=(nil);return C,J,X;end,T=function(X)local J,e,C=({});C=X:r(C,J);local W,y;y,W=X:v(W,J,C,y);y,W=X:l(y,J,C,W);W=X:E(W,C,y,J);y=(nil);y=X:z(y,J);W,y=X:H(y,J,C,W);local I;W,I=X:ns(J,C,W,I);I,W=X:qs(J,C,I,W);X:Bs(J);W=X:ds(J,C,W);local T,f,U;f,T,W,U=X:iU(T,W,J,C,U,f);U,f,W=X:kU(I,J,U,T,C,f,W);local I,T,f,m;m,f,W,T,I=X:ef(W,I,m,T,f,C,J);m,e,W=X:UZ(m,T,y,C,W,U,I,J,f);if e then return X.G(e);end;end,hU=function(X)end,Hs=function(X,X)for J=0X59,180,0X47 do if J<0xA0 then X[13]=(X[0X1f]);else return-0x1;end;end;return nil;end,XU=function(X)end,Ys=function(X,J,e,C)C[24]=0x1;if not J[15988]then J[0X5743]=-88+((X.S[4]-J[0X7425]-J[0x2498]==X.S[1]and X.S[0X2]or J[0X4695])+J[0X9f6]);e=(-0x1d5BD3F3+(J[19855]-J[19855]-J[32553]+J[16537]>=X.S[1]and J[18995]or X.S[0X6]));(J)[0x03E74]=e;else e=X:Us(e,J);end;return e;end,_=function(X,X,J)X=(J[0X718A]);return X;end,kf=function(X,J,e,C)if not(e<=39)then J=C[0X20]();else J=X:Zf(J,C);end;return J;end,Is=function(X,J,e)e[27694]=-0X2C+((X.S[1]<e[0X305A]and e[12580]or e[0X285e])+e[28731]+e[5062]-e[17393]);J=20+((X.S[7]+e[0X5EE8]-e[0X00354C]>X.S[6]and e[0x66F0]or e[0X13c6])-e[0x78dd]);e[0x1f4b]=J;return J;end,Gf=function(X,J,e)if not(J>38)then while true do X:of(e);end;else if not(e[44])then else e[0X001d]=e[44];end;end;end,Wf=function(X,X,J)X=J[0X4F37];return X;end,yf=function(X,X,J,e)e=X[14](J);return e;end,_s=function(X,X,J,e,C)X=nil;J=(nil);C=nil;local W=46;while true do if not(W>0X2E)then X,J=e[0X1D](),e[29]();W=53;else if not(X==0X00 and J==0)then else return J,X,-2,C,0;end;C=1;break;end;end;return J,X,nil,C;end,Vs=function(X,X)return X;end,Cs=function(X,J,e)e=-80+(((X.S[8]<J[0X4695]and J[0X4099]or J[0X4A33])-J[8607]~=J[0X7f29]and J[15988]or J[13644])+J[18069]);J[0X586d]=e;return e;end,Js=function(X,J,e,C,W,y)local I,T;if J==0X0 then I,J,C,T=X:Ds(C,W,J,y);if I~=-0X2 then else return J,-0X2,C,T;end;else if J~=0X7FF then else if y==0 then if e[31]~=W then else while e[0X19]^174 do return J,-2,C,(X:es(e));end;while e[0X25]do I=X:Hs(e);if I~=-1 then else return J,-0X1,C;end;end;end;return J,-0x2,C,W*(0X40fAb4/0x0);else return J,-0X2,C,W*(0/0);end;end;end;return J,0XA077,C;end,ks=function(X,X,J)if X>=J[17]then X=(X-J[0X17]);end;return X;end,h=error,nf=function(X,J)local e,C;for W=0X59,306,95 do if W~=0XB8 then J[0x17]=84;else if not(91)then else X:Qf();goto a;::v::;e,C=X:Af(J);if e==-2 then return-0X2,C;end;::a::;J[0x2d],J[0Xb]=J[13],(J[0X2c]);goto v;end;break;end;end;return nil;end,rs=function(X,X)X=(nil);return X;end,Ps=function(X,X)return X*0X0;end,uU=function(X,X,J,e)e[J]=J+X;end,CU=function(X,X,J)J=X[0X21]();return J;end,G=unpack,ff=function(X)end,cU=function(X,J,e,C,W)if e==0x0 then return-2,J,W,C*0X0;else X:QU();goto M;::L::;W=X:AU(W);goto H;::s::;X:nU();goto L;::M::;X:FU();goto s;::H::;J=(0);end;return nil,J,W;end,Rs=function(X,X,J,e)if X==0X0 then return-2,X,e;else if X>=J[17]then X=X-J[0X17];end;end;return 0x67Cc,X;end,Ms=function(X,X,J)X=J[0X01D]();return X;end,J=function(X,X)X=(nil);return X;end,us=function(X)end,YZ=string.sub,I=function(X,X)X[13]=(4503599627370496);end,eU=function(X,X,J,e)J=#e;(e)[J+1]=X;return J;end,O=function(X,J,e,C)C[9]=function(W,y,I)if not(W>I)then else return;end;local T=(I-W+1);if T>=8 then return y[W],y[W+1],y[W+2],y[W+0x3],y[W+4],y[W+5],y[W+6],y[W+7],C[0X9](W+8,y,I);elseif T>=0X07 then return y[W],y[W+1],y[W+2],y[W+3],y[W+0x4],y[W+0x5],y[W+6],C[0X9](W+7,y,I);elseif T>=0X6 then return y[W],y[W+0X1],y[W+0x2],y[W+0x3],y[W+4],y[W+0X5],C[9](W+6,y,I);elseif T>=5 then return y[W],y[W+0X1],y[W+0X2],y[W+0x3],y[W+0x4],C[0X9](W+5,y,I);elseif T>=4 then return y[W],y[W+0x1],y[W+2],y[W+3],C[9](W+0X4,y,I);else if T>=0X003 then return y[W],y[W+0X1],y[W+2],C[9](W+3,y,I);else if T>=2 then return y[W],y[W+0X1],C[0X9](W+2,y,I);else return y[W],C[9](W+1,y,I);end;end;end;end;if not e[30941]then J=X:L(J,e);else J=(e[30941]);end;return J;end,Zf=function(X,X,J)X=-J[25]();return X;end,ef=function(X,J,e,C,W,y,I,T)T[0X2d]=nil;(T)[0X2e]=(nil);e=(nil);W=nil;J=(53);while true do if J<0X35 then W=function(...)return(...)();end;break;else if J>0X10 then T[0X2D]=function(f,U,m)local E,r,d,V,g=f[2],f[0X5],f[0x4],f[0xA],(f[0X9]);local D,K,z=f[0X1],f[3],f[0x7];m=(nil);m=(function(...)local N,S,u,A=T[14](E),(T[0x14]());local E,R,c,n,w,F,o,k,q,x,i,B,Q,b,G,Y=0X0,0X1,0X1,(0x1);while true do local v=D[n];if not(v<83)then if not(v<0X7D)then if v>=146 then if v<0X9c then if v<0X97 then if not(v<0X94)then if not(v>=0X95)then w=(z[n]);B=(N);else if v==0X96 then(N)[r[n]]=(D);else G=(r[n]);k=(k[G]);end;end;else if v==0x93 then w=(c);else N[K[n]]=(N[V[n]]>N[r[n]]);end;end;else if v<153 then if v==152 then N[K[n]]=g[n]+N[V[n]];else(N)[V[n]]=N[r[n]]-d[n];end;else if v<0X9A then if N[V[n]]~=d[n]then n=r[n];end;else if v==155 then q=(V[n]);else w=K[n];end;end;end;end;else if v<161 then if v<0X9e then if v~=0X09D then B=B^k;else for a=q,w do B=N;k=a;a=nil;(B)[k]=(a);end;end;else if not(v>=159)then q=r[n];c=q+V[n]-0X1;(N[q])(T[11](N,c,q+1));c=(q-0x1);else if v==160 then k=r[n];B=B[k];k=N;else Q={[5]=Q,[0X4]=o,[1]=b,[3]=Y};c=(K[n]);o=(N[c]);b=N[c+0X1];Y=(N[c+2]);n=(V[n]);end;end;end;else if v<164 then if not(v<162)then if v~=0Xa3 then q=N;else q=({...});for a=0X1,V[n]do(N)[a]=q[a];end;end;else N[K[n]]=N[V[n]]~=N[r[n]];end;else if not(v<0XA5)then if v~=166 then B=(V[n]);w=w[B];B=(N);else q=U[V[n]];q[0x2][q[1]]=N[K[n]];end;else c=(q);q=(N);end;end;end;end;else if v>=135 then if not(v<0X8C)then if v<143 then if v>=0X8d then if v~=0X8e then(N)[r[n]]=N[V[n]]+d[n];else(N)[r[n]]=(N[V[n]]>=N[K[n]]);end;else q=r[n];w=u-E-1;if w<0 then w=(-1);end;B=(0x0);for a=q,q+w do N[a]=(i[R+B]);B=(B+0X1);end;c=q+w;end;else if not(v>=144)then w=(V[n]);else if v==0x91 then q=r[n];(N[q])(N[q+1]);c=q-1;else N[V[n]]=(nil);end;end;end;else if v<0X89 then if v==0X88 then(N)[V[n]]=r;else w=N;end;else if v>=0X8A then if v~=139 then if N[K[n]]~=N[r[n]]then else n=V[n];end;else N[V[n]]=U[K[n]][g[n]];end;else(N)[r[n]]=U[V[n]];end;end;end;else if not(v<130)then if not(v>=132)then if v~=0X83 then T[36][K[n]]=(N[r[n]]);else q=(U[r[n]]);q[0X2][q[1]][N[V[n]]]=(N[K[n]]);end;else if v<133 then k=K[n];B=B[k];else if v~=134 then w=(r[n]);B=d[n];else q=N;end;end;end;else if v<127 then if v==126 then if not(not(N[r[n]]<d[n]))then else n=V[n];end;else q=(r[n]);w=(V[n]);c=(q+w-1);if not(F)then else for a,L in T[0x4],F do if not(a>=0X1)then else(L)[0X2]=L;(L)[0X3]=N[a];(L)[1]=(0x3);(F)[a]=nil;end;end;end;return N[q](T[0XB](N,c,q+1));end;else if not(v<0X80)then if v==0X81 then k=q;G=(2);k=(k[G]);else(N)[V[n]]=({});end;else q=N;w=K[n];B=(N);end;end;end;end;end;else if not(v>=104)then if v<0x05D then if v>=88 then if v<0X5a then if v~=0x59 then B=q;q=(1);else N[K[n]]=N[V[n]]/g[n];end;else if v<0X5B then q=K[n];(N)[q]=N[q](T[11](N,c,q+0x1));c=q;else if v==0X5c then q=U[K[n]];(q[0X2])[q[1]]=g[n];else G=q;q=1;end;end;end;else if v>=85 then if not(v<0X56)then if v~=0X57 then B=T[36];k=(V[n]);else w[B]=(q);end;else if N[V[n]]==d[n]then n=(r[n]);end;end;else if v==0X54 then q=(false);o=o+Y;if Y<=0x0 then q=o>=b;else q=(o<=b);end;if q then(N)[r[n]+3]=o;n=K[n];end;else B=(B[k]);q[w]=B;end;end;end;else if v<0x62 then if not(v>=0X5F)then if v~=0X5e then c=(V[n]);N[c]();c=c-0x1;else o=(Q[0X4]);b=Q[0x1];Y=Q[3];Q=(Q[0X5]);end;else if v>=0X60 then if v~=0x61 then k=d[n];B=(B-k);else(N)[V[n]]=N[r[n]]+N[K[n]];end;else q=(r[n]);w=V[n];B=K[n];if w==0 then else c=q+w-1;end;k,G=(nil);if w==0x1 then k,G=T[42](N[q]());else k,G=T[42](N[q](T[11](N,c,q+1)));end;if B==0X1 then c=q-1;else if B~=0X0 then k=(q+B-2);c=k+1;else k=(k+q-0X1);c=(k);end;w=(0);for a=q,k do w=(w+1);(N)[a]=G[w];end;end;end;end;else if v<0X65 then if not(v<99)then if v~=100 then N[K[n]]=not N[r[n]];else(N)[K[n]]=(N[r[n]]);end;else q[w]=B;end;else if v>=102 then if v~=0X67 then if not(not(N[K[n]]<=g[n]))then else n=(V[n]);end;else N[K[n]]=T[0X24][V[n]];end;else q=U[r[n]];(N)[K[n]]=q[2][q[1]];end;end;end;end;else if v>=0X072 then if not(v<0X77)then if not(v>=0X7a)then if v>=120 then if v==0x79 then N[K[n]]=N[r[n]]<=N[V[n]];else B=(B[k]);k=g[n];end;else k=(g[n]);B=B/k;end;else if not(v<0X7B)then if v==124 then q=U[V[n]];(N)[K[n]]=(q[2][q[0X1]][g[n]]);else(N[r[n]])[N[V[n]]]=N[K[n]];end;else N[r[n]]=T[14](K[n]);end;end;else if v<0X74 then if v~=0x73 then q=(K[n]);(N)[q]=N[q](N[q+1],N[q+0x2]);c=(q);else B=(B[q]);q=(g[n]);end;else if v>=0X75 then if v~=118 then k=(g[n]);B=(B[k]);else if not N[r[n]]then n=V[n];end;end;else w=r[n];B=U;end;end;end;else if not(v<109)then if not(v<0X6F)then if not(v>=112)then if F then for a,L in T[0X4],F do if not(a>=1)then else(L)[0X2]=L;L[0X3]=(N[a]);L[0X1]=(3);(F)[a]=(nil);end;end;end;return;else if v==0x71 then q=(nil);w=(nil);B=(nil);k=(nil);G=(0X3);while true do if G<=6 then if G~=0X3 then w=0x000;B=(4503599627370495);G=-68+((G~=v and G or G)-v-G==G and G or v);else q=(-190);G=-0x6E+((v-G-v>=G and v or G)+v);end;else if not(G>0X28)then B=(v);G=63+(G-G+G+v<=G and G or G);else if G~=0X67 then w=w*B;G=(40+((v~=v and G or G)+v-G-v));else k=D[n];break;end;end;end;end;G=0X5A;repeat if G>90 then k=(v);break;else if G<113 then B=(B-k);G=(v+G-v+v-G);end;end;until false;B=B==k;G=0X44;while true do if G>68 then if not(not B)then else B=(v);end;break;else if not(B)then else B=D[n];end;G=0X3c+(((v==G and G or v)<v and v or G)-v+G);end;end;k=(D[n]);G=(105);repeat if G==0X34 then k=D[n];G=(-49+((G+v<=G and v or G)-v==G and G or G));elseif G==105 then B=(B+k);G=-0x002d+((v+v>=v and G or G)-v+G);else if G==3 then B=(B-k);G=0X3+((G-G-v~=G and G or G)~=v and G or v);else if G~=6 then else k=(v);break;end;end;end;until false;B=B+k;k=(D[n]);B=(B-k);k=D[n];G=45;repeat if G<0X2d then if not(B)then else B=(v);end;break;else if G>0X28 then B=(B<k);G=-0XA3+(v+G-v+G+v);end;end;until false;if not(not B)then else B=(v);end;k=(D[n]);B=B+k;w=(w+B);q=q+w;(D)[n]=q;q=r[n];n=(q);else if not(not(z[n]<=N[K[n]]))then else n=(r[n]);end;end;end;else if v~=110 then B=(B[k]);else if N[r[n]]~=N[K[n]]then n=(V[n]);end;end;end;else if not(v<0X6A)then if not(v>=0x6B)then B=(0X2);w=(w[B]);else if v==108 then(N)[V[n]]=(N[r[n]]-N[K[n]]);else if not(not(z[n]<N[K[n]]))then else n=r[n];end;end;end;else if v~=0X69 then(N)[r[n]]=N[V[n]]~=d[n];else(N)[V[n]]=(N[K[n]]*N[r[n]]);end;end;end;end;end;end;else if not(v<41)then if v<0X3E then if not(v<0x33)then if not(v<0X38)then if v<0X3b then if not(v<57)then if v==0X3a then(N[r[n]])[N[V[n]]]=d[n];else q[w]=(B);end;else N[r[n]][z[n]]=N[K[n]];end;else if not(v<60)then if v~=0X3D then k=(g[n]);else N[V[n]]=(N[K[n]]%N[r[n]]);end;else q=r[n];(N[q])(T[0Xb](N,c,q+0X1));c=(q-0x1);end;end;else if v<53 then if v~=52 then if not(not(N[r[n]]<=N[K[n]]))then else n=(V[n]);end;else q=(V[n]);w=(N[r[n]]);(N)[q+0X1]=w;(N)[q]=w[d[n]];end;else if v<54 then for a=r[n],K[n]do N[a]=(nil);end;else if v~=0x37 then(N)[r[n]]=(N[V[n]]*d[n]);else N[V[n]]=f;end;end;end;end;else if v>=46 then if not(v>=0x30)then if v~=47 then u,i=T[0X2a](...);else w=(r[n]);B=d[n];end;else if not(v<49)then if v~=50 then(N)[V[n]]=-N[K[n]];else B=(nil);q[w]=(B);end;else N[K[n]]=N[V[n]][N[r[n]]];end;end;else if not(v>=0X2B)then if v~=0X2A then N[r[n]]=#N[K[n]];else B=(N);k=V[n];end;else if v<0X2C then B=B+k;(q)[w]=(B);else if v==0X2D then N[V[n]]=g[n]<=N[K[n]];else if F then for f,a in T[0X4],F do if not(f>=0x1)then else a[0X2]=a;a[0X3]=N[f];(a)[0X1]=(3);(F)[f]=nil;end;end;end;return N[K[n]];end;end;end;end;end;else if v<0x48 then if not(v<0X43)then if not(v<69)then if v<70 then E=V[n];u,i=T[42](...);for f=1,E do(N)[f]=(i[f]);end;R=(E+0X1);else if v~=71 then N[K[n]]=(N[r[n]]..N[V[n]]);else if not(N[V[n]]<N[K[n]])then n=(r[n]);end;end;end;else if v~=68 then N[r[n]]=(N);else q=U;end;end;else if v>=64 then if v<0X41 then G=K[n];k=(k[G]);else if v~=0x42 then q=(nil);w=(nil);B=(nil);k=25;repeat if k>0X24 then if k~=51 then w=(w*B);break;else B=4503599627370495;k=0X43+(v-K[n]+k-v+K[n]);end;else if k<=25 then q=0X124;k=(-0x4+((k+k-v<=k and v or k)-k));else w=0;k=(-0XE+((k+K[n]>K[n]and k or k)-k<k and v or v));end;end;until false;B=K[n];G=(D[n]);B=(B<G);k=(0X49);while true do if k==73 then if not(B)then else B=K[n];end;if not B then B=D[n];end;G=(D[n]);k=89+((v+k-k<=K[n]and K[n]or K[n])-k);else if k==0X14 then B=B-G;k=0x27+((k-k>=k and k or k)+k+k);else if k==99 then G=(K[n]);break;end;end;end;end;B=(B-G);k=89;while true do if k==89 then G=(v);k=(0X7+((k+K[n]-v<k and k or K[n])+K[n]));else if k==0x73 then G=(K[n]);break;else if k==0x64 then B=(B+G);k=-0X4d+((K[n]>k and k or k)+k-K[n]-K[n]);end;end;end;end;B=B+G;G=(v);k=0X66;repeat if k>13 then B=B-G;k=-52+(v+K[n]-k+K[n]~=k and v or k);else if k<102 then G=v;break;end;end;until false;B=B-G;k=(35);repeat if k==0X23 then G=D[n];k=(0X86+(K[n]-v-k+K[n]-K[n]));else if k~=0X26 then else B=(B-G);break;end;end;until false;w=w+B;k=0X1B;while true do if k<0X001b then q=U;break;elseif k>0X5 and k<0x3E then q=(q+w);k=4+(K[n]+k+k-k+k);else if not(k>0x1b)then else(D)[n]=q;k=(0X80+(K[n]+k-v-k-k));end;end;end;w=r[n];q=(q[w]);w=N;x=nil;k=64;repeat if not(k>=0X40)then G=q;x=0x2;break;else B=(K[n]);k=(-37+(((k==v and k or k)-k<k and K[n]or K[n])+k));end;until false;G=(G[x]);A=nil;k=13;while true do if k>0X00d then if not(k>=0X7A)then x=(x[A]);k=45+((v-k<=k and k or v)-v+k);else G=G[x];w[B]=G;break;end;else if k==0XD then x=q;k=(-57+(k+v-k+k==k and k or v));else A=(1);k=(63+(((k>k and k or k)-k==v and k or K[n])>=k and v or k));end;end;end;else q=N;w=(V[n]);end;end;else if v~=0X3f then w=(d[n]);B=(g[n]);else if not(N[V[n]])then else n=r[n];end;end;end;end;else if not(v>=77)then if not(v>=0X4A)then if v==0x49 then q=(q[w]);w=N;B=(K[n]);else q=K[n];c=q+r[n]-0X1;(N)[q]=N[q](T[0Xb](N,c,q+1));c=(q);end;else if not(v<0x4b)then if v==76 then q=K[n];w=V[n];B=N[q];for f=1,c-q do(B)[w+f]=(N[q+f]);end;else N[V[n]][d[n]]=(g[n]);end;end;end;else if v>=80 then if not(v<81)then if v==82 then q=(U[V[n]]);N[r[n]]=q[2][q[1]][N[K[n]]];else N[V[n]]=(T[0X1E](N[r[n]],d[n]));end;else q=(r[n]);w=(K[n]);end;else if not(v>=0x4E)then q=(N);w=r[n];q=(q[w]);else if v==0X4F then k=r[n];B=B[k];else B=(z[n]);k=(N);end;end;end;end;end;end;else if not(v>=0x14)then if v>=0xa then if not(v<0xf)then if v<17 then if v==0X10 then k=(K[n]);else q=(V[n]);N[q]=N[q](N[q+0X1]);c=(q);end;else if v>=0X12 then if v==0X13 then q=(16);w=nil;B=(nil);k=(0X59);while true do if k>89 then B=4503599627370495;break;else if k<100 then w=(0X0);k=-27+((k-k==v and v or v)+k+v);end;end;end;w=w*B;B=D[n];G=(D[n]);k=(48);repeat if k>48 then G=(D[n]);break;else if not(k<0X4f)then else B=B-G;k=(0X4F+(((v-k>k and v or k)<=k and v or k)-v));end;end;until false;B=(B-G);k=91;repeat if k==0X5b then G=D[n];k=0x23+(v-v-v+v+k);else if k==96 then B=B+G;break;else if k==0x7E then B=B-G;k=-0xCa+(v+k+v-v+k);else if k~=69 then else G=(v);k=84+((v==v and k or k)-v-v-v);end;end;end;end;until false;k=0X64;while true do if k==0X64 then G=v;k=(0xf+(k-v+v+v>=v and k or v));else if k==0X73 then B=(B~=G);k=-42+((k+v+k==k and k or k)-v);else if k~=0X36 then else if B then B=(v);end;break;end;end;end;end;if not(not B)then else B=v;end;G=(D[n]);k=124;repeat if k<124 then G=(D[n]);break;else if not(k>43)then else B=B+G;k=-100+((k~=v and k or v)+v+v-v);end;end;until false;B=(B+G);k=(0x41);repeat if k>0X2c then if k~=62 then G=D[n];B=(B+G);k=(-86+((v-k+k>v and v or k)+k));else w=(K[n]);break;end;else if k<0X2C then q=(U);k=(35+((k==k and v or v)+k-v~=v and k or v));else w=(w+B);q=q+w;D[n]=(q);k=-0x11+(v+k-k+k<=v and k or k);end;end;until false;q=(q[w]);w=(q);k=(0x41);while true do if k==0X5 then G=(g[n]);break;elseif k==0X41 then B=(0X2);k=25+(k+v-k+k-k);elseif k==0X2C then w=w[B];k=(0X1B+(((v+k<=k and k or v)<k and k or v)-k));else if k==0X3e then G=1;B=(B[G]);k=43+((v<v and k or v)-v-v-v);else if k==0X1B then B=q;k=(0x23+(v+k-k+v>=v and k or v));end;end;end;end;(w)[B]=G;else(N)[V[n]]=N[r[n]]==d[n];end;else q=V[n];w,B=o(b,Y);if w then N[q+1]=(w);(N)[q+2]=(B);n=K[n];Y=w;end;end;end;else if v<12 then if v==11 then G=G[q];k=(k[G]);(w)[B]=(k);else w=K[n];q=q[w];w=q;end;else if v<13 then k=c;B=B[k];B=B();else if v~=0XE then(N)[K[n]]=V;else w=(V[n]);B=(S);end;end;end;end;else if v<5 then if not(v<0X2)then if not(v>=3)then N[V[n]]=(K);else if v~=0x4 then q=(q[w]);else w=r[n];end;end;else if v==1 then B=(N);else k=(V[n]);end;end;else if v<0x7 then if v~=6 then(N)[r[n]]=(d[n]);else N[V[n]]=(N[r[n]]<d[n]);end;else if v>=0X8 then if v==0X9 then q=(nil);w=(nil);B=(nil);k=51;while true do if k==0X5d then B=(4503599627370495);k=-69+(((v+v<v and v or k)<=k and k or v)<=v and k or k);else if k==118 then w=(0);k=(-0X98+((v+v>=v and k or v)+v+k));elseif k==24 then w=(w*B);k=5+((k<=v and k or v)+v-v+v);else if k==51 then q=0X91;k=0X97+(v+v+v-k-v);else if k~=0x17 then else B=(v);break;end;end;end;end;end;G=nil;k=0X072;repeat if k<0X72 then B=(B+G);G=D[n];break;else if k>0X29 then G=(D[n]);k=0X89+((k+v==k and v or v)+v-k);end;end;until false;B=B-G;k=94;repeat if k>37 then G=D[n];B=B==G;k=(-0X4B+(k-k+v+k+v));else if k<0x5e then if B then B=(D[n]);end;break;end;end;until false;k=(4);while true do if k>0X4 then B=(B>=G);break;else if k<0x13 then if not B then B=(v);end;G=(v);k=7+(v+k+k-v+k);end;end;end;k=0X66;while true do if k==0x66 then if not(B)then else B=(v);end;k=0X4+((k-v~=k and v or v)+v<=k and v or v);else if k==13 then if not B then B=D[n];end;break;end;end;end;G=v;B=(B-G);G=(D[n]);k=0x31;repeat if k<49 then B=B-G;k=(0x65+(v+k-k+k-k));elseif k>110 then q=q+w;D[n]=q;break;elseif k<0X75 and k>0x5c then w=w+B;k=99+((v+k+v>k and v or v)+v);elseif k<92 and k>11 then B=(B+G);G=D[n];B=B-G;k=(34+((k<v and k or v)-k+k+k));else if not(k<0x6E and k>0x31)then else G=(D[n]);k=(2+(v-v+v+v-v));end;end;until false;k=35;repeat if k>0X0023 then w=V[n];B=(r);break;else q=N;k=(0X1D+((k+v<k and v or v)-v~=k and v or k));end;until false;q[w]=(B);else q=(V[n]);N[q](N[q+0X1],N[q+0x2]);c=(q-0x1);end;else B=B[k];end;end;end;end;else if not(v>=30)then if not(v<25)then if not(v>=27)then if v~=26 then(N)[K[n]]=N[V[n]][g[n]];else c=V[n];(N)[c]=N[c]();end;else if not(v<28)then if v~=29 then if F then for f,E in T[0X4],F do if f>=0X1 then E[2]=(E);E[0X3]=(N[f]);E[0X1]=(0X3);F[f]=nil;end;end;end;q=(K[n]);return N[q](T[11](N,c,q+1));else N[K[n]]=z[n]^N[r[n]];end;else(N)[V[n]]=N[r[n]]%d[n];end;end;else if v<22 then if v==21 then N[V[n]]=(T[30](N[r[n]],N[K[n]]));else(N)[V[n]]=N[K[n]]==N[r[n]];end;else if v>=23 then if v~=0X18 then(N)[V[n]]=(S[g[n]]);else B=N;k=(r[n]);end;else q=(N);w=V[n];B=(N);end;end;end;else if v<0X23 then if not(v>=0x20)then if v~=31 then(N)[r[n]]=(z[n]>N[K[n]]);else q=(N);w=K[n];end;else if v>=0X21 then if v~=0x22 then q=N;w=K[n];else q=(d[n]);w=(q[0xB]);B=#w;k=(B>0 and{});G=T[45](q,k);(T[43])(G,S);(N)[r[n]]=(G);if k then for f=0X1,B do G=(w[f]);q=G[0X2];x=G[1];if q==0X2 then k[f-1]=(U[x]);else if q==3 then k[f-1]=N[x];else if q~=0 then else if not(not F)then else F={};end;A=(F[x]);if not A then A={[0X1]=x,[2]=N};(F)[x]=(A);end;k[f-0X1]=(A);end;end;end;end;end;end;else Q={[5]=Q,[0X4]=o,[1]=b,[0x3]=Y};q=(K[n]);Y=(N[q+0X2]+0);b=(N[q+1]+0);o=(N[q]-Y);n=(r[n]);end;end;else if not(v>=38)then if v<36 then k=V[n];B=B[k];else if v==37 then N[K[n]]=(z[n]~=N[r[n]]);else n=(r[n]);end;end;else if v>=0x27 then if v==0x28 then(N)[V[n]]=(N[K[n]]/N[r[n]]);else(N)[r[n]]=N[K[n]]>z[n];end;else w=r[n];end;end;end;end;end;end;end;n=n+1;end;end);return m;end;T[0x2e]=(function()local f,U,m,E,r,d;E,m,d,r=X:MU(T,d,r,E,m);local V,g;g,d,r,V=X:RU(r,g,m,T,V,d);local D,K;D,U,K=X:Uf(m,D,T,E,g,K,V,d,r);if U~=-1 then else return;end;X:Yf();goto h;::t::;X:af(K,d);goto P;::h::;K=X:yf(T,D,K);goto t;::P::;for V=0X51,0X9d,3 do if V==84 then for z=1,D do g=nil;E=0X0053;repeat if E~=0X0053 then if not(T[1][g])then m=nil;r=(nil);for D=0X71,0X81,0X10 do r,m=X:hf(D,z,r,g,m,T,K);end;else K[z]=T[0X1][g];end;break;else E=22;g=T[33]();end;until false;end;elseif V==81 then U,f=X:Ff(T);if U==-0x2 then return f;end;elseif V==0X57 then(d)[2]=T[33]();else if V==93 then return d;else if V==90 then(d)[0X6]=T[33]();end;end;end;end;end);e=(function()local f,U,m,E;f,m,E,U=X:zf(T,E,m);if f==-0X2 then return U;else if f==-1 then return;end;end;while true do f,E=X:Df(E,T);if f~=25229 then else break;end;end;return m;end);if not(not I[20886])then J=(I[20886]);else J=-0X67+(((I[30941]>I[0X0718A]and X.S[0X2]or I[0x305A])<=I[626]and I[0X5Da2]or I[0X45B5])+I[8011]-I[0X4695]);(I)[0X5196]=J;end;end;end;end;y=e();C=(nil);J=(94);return C,y,J,W,e;end,WU=function(X,X,J,e,C,W)if e[44]==115 then else for y=1,0X90,0X11 do if y==1 then(e[0X27])[J+0X1]=(X);e[0X27][J+2]=W;else if y==18 then(e[0X27])[J+3]=C;break;end;end;end;end;end,pf=function(X,X,J)J[0X24][0x4]=(X);end,z=function(X,X,J)J[0X13]=(function(e)e=J[0X008](J[2](e,0x5),"\122","!!!!\x21");return J[8](e,".....",J[0X6]({},{__index=function(e,C)local W,y,I,T,f=J[16](C,1,0x5);local U=(f-33)+(T-0X21)*85+(I-0X21)*7225+(y-33)*614125+(W-0X21)*0X31c84B1;y=(U%256);U=U/0x100;U=(U-U%1);I=U%0X0100;U=U/0x100;U=(U-U%0X1);W=(U%256);U=U/256;U=U-U%1;f=U%0X100;T=(J[0x005][y]..J[5][I]..J[5][W]..J[0X5][f]);U=(U/0X100);U=(U-U%1);(e)[C]=T;return T;end}));end);(J)[0x14]=(nil);J[21]=(nil);X=nil;return X;end,Kf=function(X,J)(J[0X24])[13]=J[31];J[0X24][0Xa]=X.S;end,SU=function(X,J,e)if J[23]==J[0X19]then else for C=0X27,0x080,0X29 do if C==39 then J[0x18]=(J[24]+e);else if C~=0x50 then else return-0X02,(X:TU(J,e));end;end;end;end;return 43097;end,Y=string.gsub,r=function(X,X,J)X=({});(J)[0X1]=(nil);J[0X2]=(nil);J[0X3]=nil;(J)[4]=(nil);return X;end,TU=function(X,X,J)return(X[0x2](X[21],X[0x18]-J,X[0X18]-0X1));end,CZ=function(X,X,J)X=J[0x37EC];return X;end,vf=function(X,X,J)X=J[25]();return X;end,Of=function(X)end,es=function(X,X)return X[33];end,nU=function(X)end,oU=function(X,X,J)J=(X[6978]);return J;end,g=type,BU=function(X,X,J,e,C)e=0X1A;X=J[0Xe](C);return X,e;end,YU=function(X,X)if X==0 then return-0X2,0X00;end;return 0xBb6;end,GU=function(X,J,e)J=(-3576561742+((e[0X3124]+e[13153]>e[24516]and e[18995]or X.S[0X3])+e[5062]+e[0X703b]));e[26707]=J;return J;end,Q=string.byte,cf=function(X,X,J)J=X[0X021]()-4711;(X)[0x22]=X[0XE](J);return J;end,B=function(X,J,e,C)(C)[0x5]={};if not(not J[0X9F6])then e=(J[2550]);else e=X:q(J,e);end;return e;end,D=function(X,J,e,C)(C)[20]=getfenv;(C)[0X15]=C[0X13](X.A);if not(not e[10334])then J=(e[10334]);else e[29733]=(0xc3Fb+((e[0X9f6]>e[0X66f0]and e[2550]or e[0x0078DD])-e[0X004099]-X.S[1]+e[30941]));e[5952]=(-0xC377+((e[12580]+e[0x9F6]+e[28731]>e[0x4D8f]and e[13644]or e[30941])+X.S[0X1]));J=(-0X64+((X.S[5]+e[13644]>X.S[1]and e[0x2498]or e[2550])-e[13644]+e[18995]));e[10334]=(J);end;return J;end,Os=function(X,X)return X;end,As=function(X,J)local e,C=J[26](),(0X51);repeat if C>81 then C=X:Qs(C);else if C>43 and C<124 then if e>=32768 then if J[0Xb]~=J[18]then e=(e-0x10000);end;end;C=124;else if C<81 then return-2,e;end;end;end;until false;return nil;end,a=string,FU=function(X)end,NU=function(X,X,J)X=#J;return X;end,lU=function(X,X,J,e,C,W)J=(0X6C);X=(W%8);e=(C%0X8);return X,e,J;end,dU=function(X,X,J,e,C)J=X[0x23]();C=nil;e=(nil);return J,C,e;end,H=function(X,J,e,C,W)local y;W=(0X5D);repeat y,J,W=X:e(J,e,C,W);if y==0XbA50 then break;end;until false;(e)[22]=(nil);(e)[23]=(nil);(e)[0X18]=nil;return W,J;end,gs=function(X,J,e)e=(-87+((((X.S[5]~=J[0X3124]and X.S[0x5]or J[0X219f])>=J[19855]and J[10334]or J[0X219f])>=J[0X5F17]and J[0x4695]or J[13644])<=X.S[0x5]and J[0X1740]or X.S[3]));(J)[5062]=e;return e;end,bs=function(X,X,J,e)repeat local C;C=e[25]();X=(X+((C>127 and C-128 or C)*J));J=(J*128);until C<0x80;return J,X;end,Df=function(X,J,e)if J==42 then e[0X1]=X.y;return 25229,J;else if J~=0X37 then else J=0X2a;(e)[0X27]=X.y;end;end;return nil,J;end,Z=function(X,X,J)X=J[0X4695];return X;end,Qs=function(X,X)X=(43);return X;end,xU=function(X,J,e,C,W,y,I,T)local f,U;if I==252 then X:hU();return 61512,J,W,y;elseif I==0X26 then y=T[0X1B](e,0X17,0X8);W=(-0X1)^T[27](e,0x1f,1);if y==0 then f,J,y,U=X:cU(J,C,W,y);if f~=-0x2 then else return-0X2,J,W,y,U;end;elseif y~=255 then else if T[0X11]~=T[0X16]then if T[26]==T[18]then return-0x1,J,W,y;elseif T[17]==T[0X23]then(T)[5]=T[0x5]+-0X7E;if T[9]^0XF6 then(T)[0x17]=(T[23]);end;elseif C==0 then return-2,J,W,y,W*(846297/0X0);else return-2,J,W,y,W*(0/0);end;end;end;else if I==145 then X:mU();end;end;return nil,J,W,y;end,P=function(X,J,e,C)if e==33 then e=X:p(C,J,e);else if e~=0XC then else J[0x012]=9007199254740992;return 0Xf2ED,e;end;end;return nil,e;end,_U=function(X,X,J,e,C,W,y,I,T,f,U)if f>96 then if not(f<=0x6c)then y=(e-C)/8;f=(69);else I=((W-U)/8);f=(91);end;else T[X]=(y);f=(0X3F);J[X]=(I);end;return f,y,I;end,p=function(X,J,e,C)(e)[0x11]=(2147483648);if not(not J[13644])then C=X:N(J,C);else C=X:j(J,C);end;return C;end,cs=function(X)end,d=function(X,J,e,C,W)local y;if e>0X1E then y,e=X:b(J,e,W);if y==24517 then return 7758,C,e;end;else C=(X.a.char);if not J[0x2498]then J[0X5F17]=0X699DE314+(J[12580]-X.S[2]+X.S[0x6]+J[0X4099]+e);e=-3960917461+((X.S[4]<=J[0x4695]and J[0x5Da2]or X.S[7])+X.S[1]-J[16537]~=X.S[0X2]and X.S[8]or X.S[1]);J[9368]=(e);else e=J[9368];end;end;return nil,C,e;end,A=[=[LPH?kQ!)fY9N1"@;TW]k\+IJkQC$KkUG^;kTK)#kSNGKY>OKqF)taok]CiVkU5RWkTf9qGQt>pCgggmkUl"7kXanWkSiY%Ba1b&Eb0<0D?R.bk]D&\kX+M5AHf7?Ec5u=39UG-:?WSZ"0X6=k\t*RkT8qWkS35SEt#6,ASlO#@<>q"k[eR>!KC8'!*U?*FU##M@ps=okVq^@kR?YikZ?sJkQU/]Y=.RbE-ZO.kV_R8kRH`BY=e"DDJ<\ckXF]Gk\THsk[3N\kR$GX!!'G(D<OcG0'FMC1[%?mATJ*[DImF%-U-a;kTK'ikX4P7kU>Y6B+1tJASlO#@<>q"Y@VNZ@Wc_o"g:/SCgggmEsAgaDJ=-5kY:8RkQC#ok]Z/@kW.j&kWn>l0BgD0%d3rW&EkV-#j<SuDfc8gBl\<:!1[\>kPtV`Y<(kK@UX?^0Bct`@<)RUFCfN;4QmFAF`%DeEb/]s(?dU=AS27ODImF%kQ%f0kV23_>H42b(?bhT\l5o)$::79#b2X=kVTZfY9)nN@;'-RAd5GZ@;U%'!!&O23*H=_AdGR@FCT32S8N7Z/,ucjpPD5,MB89FI@M/NkV4V&!Bs\9/<LR8&knfTI[iJbS.K8n.U0h$kQW)3>#a\hI-:W6S#n$-:99rnjld\i.1[8c=aR=[+EDUC+CTCdBm+B*D/O94+EVL4F(K9#FDl%>E-64EFE2;=@;fs)Ec3(2@;fs)EccA5/8&j7AS?0.De<R$Gp$[=@3B]2FWb79CgpgjDfd+5ATMu:Eck%u-YRI-9ICs(:1$Og:fUju<D5\Z+=C)VEarNpFQb>q?q`GJ<Yq8-($G_Y6_#^k=q7)D`1.'OkV8Z?342pI5qAt*!@M&,pGt%!&me3F$ndWdrbBRrSH&Wh!0/6O;!<,eF(ko`@<,e"S"+,3S37/=-h":4>,dkYOdpDeS.e0W"q-"];s8(gS9sH]>q*62lepusL6e>E#j;[NHhE/a/F]/m`Chq197\(RRP[J>0cth6"<q#0S=E.6S,tc6$u@e8I8Z9!]k<iI"Io`BS2@4a%`WFCS-RhQ@P2cJ=p'ghTUZ5Z=pqtR!1X,X=pkO,"0Vh,>$XL"ki`5S<LctN=o_<t!*TGsY@?\sDJ<npAn>LaA7]XmS-*\55$1(]=ok$pd[UKU=q#;!-X$a\@g9+<@:O1nfb@)$e4[b,>0;u7s&oFlSBU1%(OlZoNYmnKou<L/_ONO2Y7f&WF`2=*>%M2aUKVJW;'_Y7alkkf'mW`JS5nc:$A@b^$Enj@A79CmBl\<:#[1kY!!&Mj!Nub/$Of\$T3AM%S9jB((Nq=*#-ZkH@<-Kadcb@`-0jfd!VBPJS3m=\2OWCVBlF-WS,o3b'aju)h("#H>$gDA#-S.XB6Rd)n=4T3S/baF"#?+k%/K%U:5#,P?Wmd%D$7$F>%i"dba\d+S3C")"7W"a4'JuS"km,aS<#2j2!c8!R[8r&"c$84=pFCup0%N8UQV,P!j<q*':_t!kQF?OS'RU1>F[YrS0jnpG$H8J]na\!eHqafEp,S5)X,?-rE:Q3!q0:p6<&Q,!WlOJ!jA`i\"#)[kU;EH"A;o9S-9C+3c?%4A=O=sS2kIus8Jl7=p5aH2Wt4kCd338!DHZDS-P*Y"i;tPJXf[&S2#SF)9OS`ig$'-"3m@#XIM1EkS[[l`0HiSHg!OskQ3bJ:q`C*=m.%KS,kEL2u&[^)_/q!N!E!Ya.3&7Y:$I2FCB"i*1U\PkTJ<=D$7$K>%V;`$a0YlA0iWb3a4$`kZnN)=s)sS7Haj%^;uq:kS*/3h1D.k=QhmRT1%k4>&`[c.Hh#P9*/=$PSURs=p9pi>&=?k.\$]EkW5ak!R4g@S--N0'4StNgfk&S=s<)2Z^_)'=rGf,9]uS2=p=@u#Hn/jEa`p)-Za-5SAaW0&>"g`4lfX:9oY\986R+c@gfIAATDg6Bl%m/Dk(BVS/@:h2s58/4;%gf!chHkS-EJ,'Pd7[[q/O/D6]THBj)"df?'N`QKT';78Y8!!iK3SS-3Y3"++BC-1Y`>S-8%."q<?VkQGj.kV;;)d]]/kRnFm_YMs$.``aJcS@dtd(UGYX/t=]\kTT@"=sN6TpRD7g=pe77$8)C3rT&X;SH&Wh0EcT2><N+mF6Hms1/78.Q;F(f7Tolo!b>I;!O(+F#Kg3WkQe@rPN&)E7[sgLkVZ@GGHb7cm7gBH#.7VVGb"?gS2RHV'&=')$Rr3J70QjL0/hLR!o-r\kQ*PEC_*^YCij`,A7]XmDJ<]o:g.C@".NHpMSGUMr#2(UVjnC1S2.U;%*9PMGBqB%!`W>Hq&D,ZE;c:P,$H.)/b5D4M-<)%*T<Li#3\(0%.\b8>$^>,LZeddiR8:,FZ`hMY5okOF`V0u"R0*!pnfX5;D=Uf+.O.US./o)-Rj/6X:1Rk0J8mCDWhB>pO[d_&A4MMe!qR1>!3/L%pEW(Y@-PpATK4TDImF%@j!.BDf0H$BO`0,AS5mhG&Ck6DJsQ0FDbZ,+D,O7AThd#@W*B,FCSlsig"MRpfa1D2k3(<>&./<!j;eGopkaJS2(qs&]lB=]eu4JS<`:>"l>J7fUNZ>kS*R"kX"E#;OEuQKBOlBBBT?Z6:<c&K4pc<adb*(Y;JA=D..;f@;'jrS6kE$!)BB<S-J+Z-.`([q5HoT!_li#&%X]i"H$@eDfc8g@Vp7*GareckSLi]!L$\SH"$o[pf_6N(!<$I9q3!FkSFeRk[<T_S;ueo!<<.a.)aW*"?]j0YBIOJS0dA_3*n\Ig-p4U`+b`#oia=j'Kqrh>3ICY.<7G=N^fN?-;+pkYKqW9/M/)jF!Y$pEZeXnEclJ8ATJu3DImF%F_3%FBlmj&/gtUU+=SoqF`_28,p7)G/hSPnAKWES+=q&@D00?1FCB9&ASbga>9J8@,',%S+?^iQ+>,9!HQXkfBjY3iG\(E'B-9>[AS-$q-n'7V.5!5*.=E<f=m-tWJbCR`YGLFn#mq<#+:/>\-m_,2/hAJ*/hSb/+<W9h+<VdL/1rP--nd&$5UId%#mr:M-m/g&+<VdV,:G2r-71#b5X7S"-n$Ag0/"t3,:Frg,9nKZ0.84n/1`8#+<VdZ0.\4g/g_u"+:/>].PDne5UA$*5X7S",sX^\/g)8f5X6YE-71')5UIg)5X7S"-mL'(/hS\)/hSb/,q:N"-pU$_/0H&f#mr:00.J(N5UJ*+5X7S"+=]WA/0u\e.NfiV5UIg+5X7S".OZSi+<W9f+<VdL+<VdL/g)8Z,:G#m5Un0.+=\^'$7mYL/0bH-5X6tF5X7S"-7U,f-7g8f/1rS&-7'li5X7R]5UIm+5X7S"5X7S",="LZ/hS\)/1rP-5X6YL5X7S"+=KK?#mqt2,9S*O5U.Bo5X7S",sX+5,pP#b/1</(.PE>*.PEG+0.\_,5X6eI5X7S"5X7S"5UAZ\5X7S"5X7S"5U.m+5X7S"+>,!++:/B$+<W<[5X7R]5X7S"5U@gD-7U,_5UIs-,="LZ5X6YL,qhMK,qLYp5X7S",sX^\-9sgL+<W3`/hAP)+<W9h5X6VF.Nfs$+<q1"5UJ*+5X7S"5X7S"-nIVK5X6VD,="LZ+<VdZ5UJ$)5X6PZ/g)Vj5X7R]/1*VI5X6PH-8.VL-m_,D-9sg]-nI#"0-Dem-m/jJ+<Vd[-71u+5UId(0.&qL5V+$/5X6_B+=ocC+<VdL,9S*O/2'7R+=9?#5X7Rf,qM;H5X6V?-pU$_+<VdL5X6eA-9sg]/gVep0+'<S0-_S^5X6PF-mL'(5U.p8+>5uF5X7R\5X7R]+<W9b-7g8^/grtM+=]#o5X6V<+<VdV5X7R\,sX^\+<VdL-m0WT-mh2E/hSb+$8*YO0/"t$/1*VI-7BuZ/0dDF0-DA[/0HT25X6YG-6kc@,:,T?,q(/f+=\^'5X7Rf-9sg]5U.C(-6kc@-9sg]5X6VA5X7S"-nd+u,;2;3/gVeN+<s,u5X6YK0-DK)5X7S"5X6YB/h\P:/0H&X0.85%+<W3b5UJ-:5X7S"5X7S"-7UQ3,qgl!.O.2D5X7R]/gqqm/1rP-5Un!*,sVej+=8Ef5X6YK+=\^'0-DA[5X7R]5X7S"5X7S"5X7S"5X6Y],q(Dk5X7S"5X7S"/1*VI5U[`t5X7S"5X7S"00hcf5X7Rc/gEVH-424L,pb)h.OHbj+<VdL5X7Rf+<W9b5X6_K-9sg]0/"_,-mLAu5UJ-,5X7S"5X7S"5X7S"+=KK?5UJ$)5X7S"-9sg]5U[a+0.J;6$83em,:Xul0-DZ.5X7S"5Umm)5X7S"+<W=&.NfiV5X7R\5X7S"5V+N7,:jra-8$Dl5X7S"+<s-:,q:,l,:kJp5X7S"+<VdV-9sgG.OHVu-8$DJ5U.m75X7S"+=ocC-7U,\/gWbJ5X6YI+=nof/0H&X,sWb)5X7R]5X7S"/grtM+<W3b5X7Rf5X7S"/1*VI+<VdL5U@Nq/jMZe.R4=n5UA$*5X7S"-pU$_+<VdL/g)8Z-m0WT5X7S"5X7S"+<VdT+<VdL,q(5q5X7R]5X7S"5X7S"/1;i10.n@i,sWq40-DA[+=09<+:9bS+=]W!5X6YK5X7S"+<W9`+<VdL+<VdL/g)8Z5X6YL-8$c55X7S"-9sg].P)\n+>,2p/g)8Z-7C>p5UA$2,p4Et+<W.!,sX^H$6q)E+=niG5X7Rf.R66a+<VdL+<VdL+<VdL+<VdL+<VdL5X6PH5X7S"/h0+O5X7R\/1<bK5X7S"+=9?=/gr"s+<VdL-8$D`/0H6(+:/B".NgAe5X7S"+<Vmo+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL-9rk"$6q)S-m0W=-8$Dh5X7S"+<VdV+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL,9S*O/1r87+:/B"-8$D`-7g8l/3lHc+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL-9rt%$6q)S+<Uss/g)bk.R66F5X6tF+<Vd[+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL/g`h:#mr(30.mP;5U.Bo,p4fd/1`,5+<W3[+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL/3lHR#mr=<+<Uss+<W'^/gWb--m1,b5X7S"/2&;7+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL/3lHR#mr.70*qpk,p4<`+=nj)5X7Ra5X7S"-m0p'/h\M9+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL+<VdL5X6P:$7I#J#mgnE.O-&Y5UJ*++>+m(5X7R_.P<&55X7S"5X7S".Ng>i+<W9i+<VdL+<VdL+<VdL+<VdL,q^f"5UJ*50-Df2#mgnF.R4:H5UA$*/1!PH+>58g5X7Re5X7S"/3lHc5X6VJ,sX^\5UJ*++=]WA/g`h..R5=5,p4Zg/g`hK#mr.)#mgnE0-DA[-7U,\5X7S"+<W3]-7C>d5X7S",9STc5X7S",9SI$,sX^\5X6_?/hA+p0-DA[5X7S"$7I#H#mgnE5Tg@>/g)bk5X7RZ-9sg]+>,2n+<VdL+<VdL+<VdL+<VdL+<VdL.P*1p5UId*5X7S"-n[,!#mgnF#mgnE/gr%h/g)8Z5X6VK5X7S"5X7S"5X7S"5X7S"5X7S"5X7S"5X7S"5X7S"-m1,j#mgqe#mgnE+<Uss.PE"t+<VdZ+<VdL/1`D+/1`>'/hS\++<W9h+<VdL+<VdL$6q2TS4VoO!rjFVBQjT_8=Xd1S2^>(Ih+$YATJ'ZFCSluUmL(6=>cVeVjopOS/I`o#Xc)4=pEed2d-9;S29@2)i%1RI,_K&k\95qC]gjrJ`pu_\=?XakSuhIokg3rmH>"GBNd7V!!'D/D!5#Pb?Us8"<Ue[='A8\Ad,@=Ec5l4Cn+,]Y;R/ODeO1uF'j$0c(&9akUqeA"E[fTD4mGikTo@5nK=>3"0X4DSG0Gu#IOWc#HrU?DJ<HbS9SUU+]CjPS,tBJ.*aJSSA@>X/n"C1kQcQ?WL'-mWLPm:S3>U_)-uY2D4G8&=s4J!Mjso3S/;Vg&O>6q=p<kg/a*6/H@q!CkYgUVQ'\"O0q8R%kT4**"'o)NF6Hil9R$cF8RZ[`#b6/'S1Z,R2j-@L>,kZo&*O*IQh=RQ3H1Jt%9ab/LC]mZC7GkVS9EaR"[mZuE@'Y8@QU(eqOB:3=s=Y/&6aC9?1XQeMs)=9&i+0k!YSZC1,]u>SNWJY;s4?o1Kn>Q"9hs3>#ku6lWON[(/p_+fUO$o=r*u`86Q%Dg%_'BV-5^VS/CGn/Y3OgkQ(ZeS58@l#>1;%\uu:@kU+j5:V*&E#Hp1[Ci=6$S8MbH%@thLNru&*,R+Cq"pP1`%Om*:`ARl((1d[sb#LVh"t6ST"p15=4s"8>7NQ+F&&oAK%@.(@c9;9%*^F$?.gPK3%dj@p7OD[N7QtWP!Q$)dRf_FNSe1Sr2?a:("U66C"q/s65!D_Q`BF<^!N&To(VW';c2k@.?j#Nn&WR7ESie"$"rXib4q7d8[6=VN!N&$rAHk=&69[RC%K6LuL&hILjpB$:Sj#pY?jc<,-aaBFk$ekhV@IJR#!N4l5!B`nh*(k+!N$VJ>R!@r#"B`+"p'6"blO1q/-\9a,R-F:UF$1!"pP84$C*4I(A1o:#UBTY/nG4kL-e'fp'QNT",6g`"/ZTp$J%C:SmV`AL'%#M!N'<2VCi+N#!N4l5!B0^]flI`![Isd!N$&*>R!@r"u[($#!P4^"p)dj!LY2;5WfAX&elGo(`#(bbi&O/;BuGL!JqQr)L_m>#dOX="pTO""p(_LWX_u4dNf#"?3UT:,R+GW"pP=<"f4Zl#3$a0l6HQl?3UT<"U5/8"pLh]#!N'E#Dsc2/phaG!Jq#0)Kl=.,R+GWM$=.i%L-MG"pS[H%TWd;#$q=eAc[_mng"DDM&$9tIKk6%%B]`$#EAia%))b<"5X'=!WE+`$cWU`[LK)7$k(&Y%E8F_*b#8L"5X?med'Jm\g.Ic?3UT<,R+GW('XiG#(?T0"pPupIXWt'"QC[2L'HK=[L&f3F9K;g%Ki#G?3^B/"jU%!M&$5s"p'GZ"r[Ms8gFTF!Jq!b%L)rg"p'HcFp;+S%Xr^.!JV$^%E8dQL3*X.('0^%NWk^K;['T2"L\T5"0*UN!JqQr%L)ro"p'`kIKis[!IuF"!Jq9j"I96m$(_0P%CQ>($3"7Q#1`bY#d+632@BF^"pPuP!MC\B<Wr[3"U5(2"p+*t"f5f7<]V?-FodF("r_XhW$DQOJ1_$-"pP84"0FHbm61[X80eB?<`B8-,R+@V?3UTnM%0^o('Y6L"r75r"pPt]*W_*%*a]B]"rmgO"r_(`L'P^&Sdq%eF9o;c"pjX7^&dX%;[/g$!O`?l#&[cX?3XR;?7pS*<WS$e!Jq!b)I<VSM$=.g%L-MG"pS[H%TWd;#$q=eAc[_m"r_(X!Jq!b?Od9X=p>1KHWFGW,R.R-!JUeN"pP8M!KI2;NaE8#9Ek\%HZjiB,R/]Mjotmd$hd""5CO6B"pP*Y"p2jkfeue6?mGpY%L!%@M$=-[('YNT"pP8M"f37D"p-%S%O`G(iAOX>Qm;k<)En?,#L3MX!PSSh%]0R+V+(K>c=.7e"pr9Q(,e/q#'L$("pPuhG'3qcOX(,8HZfSrM&q*u('\p_#(?T0"pPup"+VX$(EWba!"p4q"pE/2"p4'82ATj.!JV$Veoh:Q:'$%U!T"(3V@081ng"DH"U5/47Mk_m!La%W,R+q\"pP.'"ssgj"pPD["qCiU"tg[%"pQh."J&9k!hpqE2Im!r#$k810I-i,c@8Rn"pNQh"uZs&"p0B%2F_3]"p-%SrC-qp2KgbfBd<le!LHOc"rZf'*[R@^.(rjioiq\JNBSs/&EQ(V%ZWkF%^$-+NXDoi:BlOZc2sk4"kI&b$&/_>mKggA,olUZ)P.9/!JqJe&!da'c?'Mm)pT3-[K3gZ2[Hi%%/pgPeliU0Q4r*s'&Fi:,R+@R,R+HTUE0VA-3aYT"pP*ir;hg/M`(bA"p=f)SN[2]80eBFJ5o>K"sa'I"pP>Yc:0^&#1ak"(^$@)Xq/5n#0'RHA-ND'EX#Ps,R-^B"pP+4Hi^kb$X>qf"O5[$#4a#DCEoAS#(?d1"qZqC"c>N.&gRYc!L[g0.Er's"l^d3*b>N[#$k81g*?k,,R+GU[0?_r"1i7siZn_WAHi>C"p'I%!KJ43Q:>Bh;BuGI7V)c.:1XWQ%V,cm,R+:(4pD7T"8r7YOsC5I"U5/5#*f4D!KI<f"U6!\#-@ol$_emN#Q]YaAHi>CL&m$8$0kLk,R+_&"pP9`!JYc.L'B15G%CjYA-N(3"p(l%!Ob1&"u7G.7NQ+F"pS@&$dp:)E[1hnR/rl`]`\*3\g.Ih!N$&)"pP8;"p<j1!JU`S&+0hfG'"#0!*0-/[LNeu!JXM^%V,c%MZJQ](';_]!KJ?.SgFKI"pT5O"pE@"hHpKT"ppk/!N($OrZ;<Xc2k'V^.&qG;Zkkc$J#6T[SC?;OsC54<!EO-,R,Yt:'M'Z"ppGW%&tQWAdS#4#(?[Aap)l3[=3Q+#UBT[G%Cj^A-N(3"p(lM!Oc<F#eD8c_ETp_,R+GZ"pP.-<`U8Q"pP9;"pip.!OsBZ#Q^e,,R+GW*X2Y^X^2$fIK]oXND9\["0H/9!J2R$!K0t_?5<YA"8r7Y!K1P*"r7=>"pO!3!LWt[)79^l!f/*+G%Cj^A-N'h"p'`"!L=d;"pb6aIK]o[,R+M05:-RY"pP+,"p0B%!Ra1Rl6HQ<#,MM*IL,L[!jO!TCBpsG#(?iP"pDL_!JXPX,R+:+D?^0j#)WGLOsC6C!qHO#,Fs:4!VdoE!M^,/>QtB:"pQO4%M7/qr;hflM]NW9('FL9r;hg'"U5_E2BpBu"8r7Y!K0,W"r7=>"paE=L3@,4?=X6@A-N'pL&mTX"pP>6#c]-SkTg?:,R+GT('Xl'"p='R"k"Y#nK\;CAHi>@>t.:),R+GW"pP+,"pLG@NWG+dQ3F)c2?B-F?R>rQ=p>1[A-R2],R."e)Fap-!QG<J*Y%fb7Ml92#3H;Z7LPed#21=,*b>N[#$lCq\Kh@`?3UT7"U5/8"pFmR"pC)7:(Ce=blN`'EX!9\,R-^B&u>eXIKSkNQ$rl@G%CjuA-N'h"p'a%!L@>."pb6akp-H;N>;^!"0H_ISUL_HM'N9>AHi>A+$N.X-;Fon#F5i:k#2bu-4%m*V,dVV!LI*r"pLh]:.?R$"8r7Y<Wr[CRNr)K%Aj=3#4;MH#.=uLeU@OQ@KE&&!gX/<D?5_d,R+@QD?^8q"8r7Y!K2+:c5$N\L&m#7NWI]3IK>-4"JuCJFpn:Y"gK<WV-F3/!L@$t$K_AD!fdAcV(Me&@KsOd$\f(mL(7c!N<+b7N<+b7!N$>C"p'G_"r+1E%ddL*NEc["'(0WaQ3aWA?38+I!TjZb!KmKG)jUp+VC)f/L'YL&!mV>q(Tmf<p)*cs)BS#Q+1;ou#,idO!L[g0*ea^[!pCm6\KD(\V?)DU"pP>6"6h'8(aK:iR/rl`Plf#-^a'*gAHi>?"p'I%!KLc&Q<%N#Z6TVT,R+GRQj*ZfV1*jr=<n(`*b>N[#$lCqkTg?:,R+GTfEMA4c$k*Fq'6.m,R+GT"pP1o.Eu0&Q"C1(kTC'M5=SZU,R+Hm"pP4_"sufM"pTl/"pSB<"u.A-omR&K!KB8D"p:u."pV"O"pb6a!JW32!hKe6!JLU+XoZLj#e"1I,R+G'Pm.9:IV<L&#.Po_-=mAc%V,c5!K0uR"r79b#"Af;/f$oe#"AWM#,rjP!Jr]=A-N6-McNR_(';_]*_'KX2GRce!SJ_&IK]o[,R+I<"pP.^"p9]-<ZYfV"pS@6"1]Z]!n\"fHj9`.!kTs6!Jgd?5Y*sn(BM<V"O)c(!hpqE4p;,pAHj9B7n,rP,R,S.h?F&8'>?("AHi_F6:OEK"GR&FNWB<Dh?hI/VFF>c?jYs""rGO-mKKIs!N$&'>R!Y%"ssb4"p*F'"p(Sbc!1HBN@%+')hfU>ei+N0NWebf')j[&&\\La)VtLI#&a\M%V5hS(1d\>Q52qbrWf;-7KK.`%\<pN:'^b`L+<>&"p=Q+7SY"9##7Wr/d<LFjXpS#5%OpH!LH8F"rk6F7L")_'AW\V"tTsJ!M^]*"U5/8"qfrD)SIa,oelsX#UBTq!LGtS"qSsB7Rf"AX[W>X!N%IMAHkU.,R-^r,R+G[7Krmtj[K9;!N%ah68h:;$]YJCh>n,#joM=^ju-!F?jFCV*6e]=c=-jh"qRRA"p**s$ks<\(_-g"!"1J?"pE0F"pV@Y"bfSb9,80-"K`.#"5Ob73!Rn;(EOV5"pP[1*X6(!"q^/<(,u?n]h9+^('2r.eK=HWW[%cM9,7^:"oSh"!JV$F&,$%l%@.JF$H<Lg-3_XV``!/WXT@8_*Yp^\!l?2e(-"G]*D7.heOpZX(';_qN=`3bRNr(<#2KIa(*45NN<eiD]h9+E('a^YPle?dng"DA+3auo(*45NScuhp!tVgB!k&6?`BFg`"/[p]joUiXjVJsE,R+GV%i,@QN!9Ij"pQ+L*Ynr""p0Jp(,uHaKbkdO,R+GV('Xi?XV]edbtAfQ('iY!+3bQX#$E2U(,u\ePtMl6(';/qXX(SQRNr(A9,7^5!f[ka$N<@-"Qflb"/[[Qm62#OjoXoR!mY6V9,7oL(1ekQ[2>0%Td0gC9,7^4#JD89%]rA>9,7_c,io''#/s4Nm7S30*WaLs"p]hu(-!31h+JM)*WhlV"sCSq(,uH1`>9R:"e>i."s+O>"p0B%#N\So65Bh!!gWkiNWTd2[KGq-"5Q-O$-i_ImKhZYm2$fM)'/tc"s*ru"p1eM(,uEXj\$@1r<Dou"r90#!p*&j(ENd?"pP=/*X3f6"sVk>(,uHqNCt$."pX/lKa(p1*Yp^N#HgB/#aPP;h@.C+4q,.2rd>g+".is29,7un!M(3iD]U),!U(d5m2$fJ)'/tc]cI<9*Yp^;"mm*I^a'*g,R+GR('XkuPp=7@j\$?j*XIH*"p9i$*Yp_P(?J1),R+AD9,7^k(0r!C]`X6ejVJsA"GI$\(*45N!QB1S!SSe'Jd&:]Q4Tno3!0$b"hl#n%Yb"=%NYkh#He>ujZEd.p($Kl"K#)u!lY?*(*45Nbm2KdQm;k?9,7^6!L3nt#IP?h3!5]U,R,;*('Xg8ocfAgL'XpqV?X1(p'r,Pc39LCmWCW%;[M"U]ljVZ*Yp^9'u`f](ENa^SH]>\*Yp^>"pcIY#Gi4Xh&.7<"4\Fn"s+O>"p0W,#Gk'/9,7W+"SDfO,h5O'9,7R$#FuF>!MpSR3!7,((EOV5*X2f8`?$$;*Yp^<"Mc8e"tg)a!Itpi9,7^9#Lrm0"p-%S-*9fU9,7`F"3h())T>>b9,7T*!j*p5#gH!X9,7Tb"l1BA&sZ**9,7Yq!JM$^!m)\l!!N?,"pE/)4pE2VKg#Ns!N%amAHk=&,R-Fr-3aLn"pP+'[4VUe2?a9qAHiLLAHk=&7n,rX,R-F>2?j31]f#nX-=mAf!KB9,"t$`A-3aMs4pD%b4pGR&#+e.F$]Y8E`WXin:BJfG!QJG0m1#(`@LC*n"9&H&"p(S*L-dLVmK/+NmKN[p/l)rY"kF+-jTNk:@LhfD$apGg!Kn]4!LGtS4q[HO4pD%bmT:(HXq@NP"i^od!mUkErX7:9hJ$:4"q1,;mLBG>c2iP7"rYDl#!Nf65"6<!L'XptTEjTd!N&$]mQ^iF#3JA[h?CVD*b>NY5&<DP!LQ&?#!NF&#!N5B%McBV)5URB2J!(F#UBU<#VZMg3"c?>!"'PQ"pE-G"p0B%!h(A=#&a\M7Nr$H+pJ(?,R,&["U5/T"rZ5L$iMse-3eJp!Lb8<:1OP5!Lm+j#*ob-!Mg-Y"o\am!L"%K$_@jZ`W6GGQ3G81"Ki.8@L0C`"NCVBhF[mI&.>'?"pS+O('Y$i"pOB1p)aUd"-s]3h@,DFp*1=SL'"4R!k!.Y"hkD2V?lU!(1[UZ0I-iLNtqWuXqdf7/g:K#+pJ('"p'cC#%p+D"pPni%L*+i"pOB1+PdnFM'N98A-N5=>uk!$,R+GW$NLB-"O)i*!hpqE2?a9h"U5_?"sLrL#"Af.:/2:1#"Be,"p'l4#&5Cf!Ra2%C*Wufoiq\*>R!))#!O-B7O%M1:'$(Z"82nqV*4p6@KFIT$a(#c$cQ?TeQ`;RQR31_&cF$W[5J0mU*Kp_AHi>>,R-^jM&ljd"pRNt"pRsNSjjK,NWFk>`Wr@KekU;l%KION"f;[1Q3!RU7U\an-=mBV!MMtXG!JMl,R+HBQpq9>7OP;=7KtCJ"p(J?-3XSX"U5UI"q6JD"pP>Y7RdSnNW\-)!=caKXp`NmhF:9F?iuDu%G_-Kk$eOd"qKK!7Krmj`@_;u-6D"l"9&C_ehRu[Q4g&1&[jA6"25K+L)&!0>6_MH);Z+:eg1pZjr44&'uh-o580o;)U9G%%L-hU!Jh,D$<@\O',1]^3#EbX!"1In"pE/]"p)jlX\K$(-=d;a7Uu^(OsC6<A-N5@,R/-Ubm"3+!Rba+U*Kq<A-N5@,R,SrA-N5],R.R%"pP.?#PE6D!JV)m$MFe/!JL[Ec30\e"3+dQ"U5CD!NcBWFt+U=Nd_*,"s^MV"pQJ$2Eae="8r7Y3$\\<G#SYM,R+Co('Xlp"pP9#"ssOc*:5?^r;hg7"U5G=7MsB6"8r7Y:'Ch3,R+n3(;0o3:'%A1"6(R1!N%IR"p("o!L>WTej9D'NXhWg%*ekV%?:e(L&mT9?jhtu*1[;BL11SJVD`T!!L<oj/ch:k!r`f!$D%9W%b:pBp'csXL'uQS^(/-`+V)3l)NFp6p'(cQ<ZRb5^)-b]^a'*gA-N5=A-QX(OTo]0"pSZ?"p3d07M]M=!JV!E^+K\G:'$%Y#aPgWrWCGT3$\\4%V,bJ^a'+jNBROI&Z%YlQ7c_Q633AQQ3#P`%c.3p"bm''^&aNq?j_o%rXmCM%IR[+,R+R@"pP-s#%dmp*`cVP^)mcL#e!21"/]BA^/G0#"q.:/#R3gF:+#@?"r\`kNE-2e!K18)"qCb6"pS0T"p(YJ^hX1o"p'_b"p)Oc"pSB<FuEk8IU6OhL2LN+"pfAQ#&a\MRL/s'mM(rf!KC+rAd[X#`F].1c!1Gg,R,SC"pP-c"u[6."p(,;KfN.6Qm;kJ"U5/3"rEP:DDiPI"8r7YBI!cl#>bX0!Pe`''gQ"p"O*55"0s0V#UBTY!K/9/``<3?Q2u^K2@@G+!NlHa&,$+n[KaH$!N&TpVFEHJ4uNTG2?j2Z"p(SZ!N%J-,R-.2"pP.?"p15=\e\6L#UBTU#%[uC`^2"!edn&W%CQ9F:JM5Yc4.c?#**'s$a'iML&m<D!N$&.>QuMZ"qDBE"p1ME7j]Da"pS?[4pEkK##Z]N"p(SB";m^W(%qq4p+m-FL(X+pecCd7edC7U*nCr;*;0d6h@$2I5QQT[([_Xu``i_?"qnokXp,F_(5>O$2[0Z`#.=^?$0D8CKat87"f2\0[UKKe"pX2k%L*Cs"pP*YV%s4=*[q9s-3XSX+9i"G<>H?`"pg35"Pk+&"qW$a"p(SJH6`\A#(%s,BI!cl%L!%@>Qu%a"pPP8(S)<F]cI=]66la:%L!%@$`4L6$bcXrKl.@F&ZlfL[UKa'"pD@?(*AN@-6?_&-39)Q,R,1c*X2Z'!Pe`(?mGpc!ItO^?sH5f,Zl1"!!``G"pE-F"p'T,2?AXM*s'psQ9G?3^)e*r;[Ddp#j)/J9h#9j2?n1+NA`[Q/n$@;!JqRMA-N5r"U7.+"pXHa2Dtb[",/Wk2D/Dj2?AXM%V,bJMBiBi,R+GR?R>t\#OX2S%B]_d"-s>/"dUc_#G(o2!O`*-^(0R0-=d;g4p;-KEWu`U2D-Td"pia3^'4ig%H[m;M$=HE%L*CD"r75rV&fdE-3XSW,R,-W"pP*Z$iNNu2?n1+2D/Dj/d<j2(D@#UeGK:Ql6HQ:+pJ6!!QP8E!k'9W,R+;4%L,5IL'<PS9a.*l&+0So!J^oo"SMr+!Moqu%>G;j!JCXU"bmG?`aT;S%LToPrW2[0_B]<g,R+G^NWp/SM?XXsU*KpaA-N5o"dT;Z!M3g+)%HhA"f;@W1X[NQr>?(L%L(\kjobQ"mKEdu"U7C!(B"F14IRN"!LYPE#44+=&;h_;h&-\,4orY)ScQhbPo1&4"p(S/-N,P#'`''ZVITl8"pO,imKC77k$Jl-A-O(Y"jR;>"m0>",R+FlZ3ER_jXtu`RNr(?)%Hil!fdB]"8s)I)%H_6!hKMm):Bc7CB^O="th9T"pqR\"R]e)A-N3t"pL;6jTV4Zl6HQ7"eGo%"pP8;ScnIl[U]RG%L&^4Xp0Rt_B]<h!qlg9jo`R?@0PKk"m-!F`>JaN"pO,l"q-S>`>JaNh?E$E"l<Yk"l9FF)RTU5"U5.e"f2Cb)U]l8#$qK</d2F`,R,UoL'AjUap&F^.3o*:Q3@Hs"p2aJSca^XL1CJm"pUq/Xp0ns#R6k),R+GWL'AI:RK8f\,R-[A"pQ*JNWec\`af8W%L&.$Sg<D8#R-4l"U5/8"p1>J#!r)2`>JaNecjn5h>rcCq?>FR"kIiJRNr(A)%Hj>"bm-X[4VUedNf##"c`co"pP8;NWec\<b)C<)%Hh9NWl"S"pP>:"8s)I,R+F4"pPml"pP9<"uGfq"R]e)A-N4/"f;CQ"g/"5,R+G&"pR*!"r/`o"ps!/"hk-E"U5Fd"0DaH)r`+I3X#Ed"dK5a"l1%+)%HfK"bm&;+JL+cO9(pW%L*+<V??c7r>?(G%KW^7[KHIG/d2F["U66;(6np7&&KRr#%q6H)%H\e!R:fG"8s)I,R+;#%L+Stp&k72r>?(G"p+]*!mN>$!K[A;,R+GW"pPJ3p&seok%"Z!%Kub3L'QNP#R'i)"U5/8"pV1^"u3\7"o`$:,R+AMp'(uK!K@ru,R+G'/d=H/-.2g^,R,I#"pQ^>ap$?qJg:OE&^CR`c3<'$#R-5*"U5/8"pM[m"r+Wo!PS^A"U60a+5Hs`!JUa^("O%PrW_5"6jK"k*ek-LekHb=eecmD+,3*'#/21n*W?):,R+A%Q3J6'"MGZW,R+IML'AmV"pPSBhKoG*rW\)Y"kIAk"o\[[mKNSu)[4dp",-pX"tCl&34^@>r>?(L)%L6\#&XPA"I1SC$b@SfhGa[`#!&C]"uc<'MJFL9,R,kVSd#Y@"pP>:SLt'M_B]<h#)3==(t&GQ,R,9c"pP4Q`Wh0.#R7^A,R+GWr<>!V5,)ftJg:OQ,R+GW%L*f>V??c7r>?(H%K`d8[KHIG/d2F\O93.-"pP84"r"j!#)*p)0GXf""L\HA#%msk)%He`"NCP_"8s)I)%Hep"P*\R+MoB.IN?90"pMsu"t#3QWDrtR"rW[."pE9u`>JaNecjn5h>s;Rg'-%2"kIi,Z6TVY<!EOT+IWJZ,3gEX#$qK<c34CV"p)C@"pX'4"-Oo6I2NM!Ftc+'SJ_o\"U9D[&=iiV!It32"U5/8+GBi^#0A+pCE^@q"tg+["rrLG"jU'O,R,gR[0@A_3P&u1qBQ7t"kEkmKa$$Hh?=)c"p:t2m02VUU*KpD,R+H2"pQ?a"s'E`p,W?U%LoNCrW0,=3X#Eb"0D_"!OWdT,R+DN"pP:a^BDh+/d2F[,R+@ah?GK^"l<Yk!qlf[jo`R?@0PKk"m-!F"4/:trZVON,R.TVXp+s;WWALl"pEa)"pCA?`ZG<VAd-0Z`XCX(;[0*)$C1k("8s)I,R+FteHQ]H)o?P',R+FlM?XQjV(R35_B]<g,R+GY"pQsM!S.Hcr@S6Xl6HQU#hfIb/dM>e.&nNsCBW`'"thCJ"pNs2"R[H<"U5"R"p+BL"t?8lU&gZ7"t,*A"u4OO"e?M@"U5-j"p+BL"rW:D"R]e)A-N3t"dT8!!R2Jl,R+FD/d<oE(@hKo,R+t]/d<u'%E\^>,R,.*mKOJb"p'5UmUQM+"p3W_#/)Vm#-fEX!LYPE"iUY>1:e_8efk;&"p+`,#0eb(-h8Mem26B<WWgZLc2ie:"pN!M"sALC"R]e)A-N47"g/#p"SEWB"f;I3"pP8;"tnUT"2,Q^A-N21"TAPlL*gF8iZn^3"o\]1%L*+JL'.Alr>?(L"pU(ih$0/KSco<$"s`dF"tfZs"pbBueJSi$"pL:p"2ukr/<V7_)$ZB2#.=WQXuHNc"pVL?"p0B%"nlI2A-N7H#/18t"qLpH[U]RH"pTed"sMtO!LX,:%(.C],Ea;VCDXqo"tgau"u1u\!It9l"3(NL`<4(P9*<0:!W<4V1l<O_kp-H;+pJ5R!S7G9jXp]@JKtF/%Y=lf#K8<E-L)i\!L-=`'W)+ic6EXU%M6VoAc]R_#JCubFtc+'qBQ8_IRs_V"qUAZ"qo?0"8s)I)%HhI"g.t+h(Aj8E?k`'Yp9MXA-N5?#.=Qh#+f?0A-N7P#0$i'"pbFAng"DD2$O7,"o\\f&GdXV!LYPE+b_Yf!MLbCr>?(L"pN!K%E0q'%Dj@p/d2F`,R+A4/d<Q3+JJn),R+V#"pQI7rW3"/>m:32,R+GWA-N5n"i^V@"hnLO"i^_k"pP8B"pKQ'U&gZ7"sp)Y"q6Y?$dp:)0EUqP#)3<D"o_jEmQ1UW,R+GVQ3IZK"JAa!,R+Y4"pP@3V$5ER*t$j."l9BB*WZ>6!qm&jp'-O%FU49>%upb2Smhi*p&i;F"MGZS"l9FFjosUX)$9L8"m,s,hJ<APp''R^#5oD@,R+G7XTf2s#`<R:W[%d$)%Hi]#)36YL&ll1"U7-p"pp8A"qI@Q!N'B3A-N5""muPk"m0/-W[%ci*Jt%;&$7I5)M04=ng"DD"U5/`'9r`U"R]e)A-N3t"dT5P!hC2="c`bX"pP8;"pV:W!L4N4"TARbNWo[nrW[!9YTsDUR4\[I]cEOB\g.J,#*oH9[K_F'3!68e#.=`-0:F-jJKtF()%HiO4p%T=Q3*p;Sg"@?"p0he"q.X\"8s)I)%Hc*"6K^a"8s)I)%Hc:"82iq!K[E_,R+GW/d<*N.ae?c,R,LT"pP7"QNiHo_B]<d"iUZe/fq$.#/VVi)$9L9"kEnV(Qp$JSgajJBEjb,Pm3h/ng"D]"kEl'"tGKp#e2,a#R'i*"U5/8"p<C."s/pQ"8s)IJd)F6-3f2*PqE4E\g.I_A-N5I"c``RQ@<?Kobe5CNWl!Z"pP>:"0*UNCC%TX#*oFu"b@p'/d2F`,R+:o"pQ<0c3&f3ecc6]"pDmKp'';%#R/KT"U5/8"f2CJ"kEh],R+CjSd$[="pP>:"8s)I"U5.%"p`[2"ruVJ"kHWW,R,d1NWo]<#)7I/A-N7("l9H<L)sk0l6HQ<<A"Ig.I@/h"e$\@/d2F`,R+@ajou6W%aH4.,R,.;/d;BN%ClM-,R,[I"pQTXNWK,i>m?;n,R+GW"pPM4"r6t["kEh]!qleoh?Nor)$9L8"m,lGhJ<AP"pO,m"p;(T#hU7()%Hi$"kEeS"8s)I,R+G//d;@O!jVip,R+dm"pQ35Sc[JRm26B<"pUq+XobJ2Q=L1("pVL?"pjcFZ2p@G"p`*["pP>Y"g'B\'nR\8mUQM+"pL"iQ3-Jbm26B;"pLS2"pN'n"8s)I)%HkB#,VM$*eGXA4W+_6"U5(+"ps*<"qLGS"kFk%"U5%;"sfHd"pVpi"R]e)A-N3t"dT1d#5&iD"c`bX"pP8;"sAdK"R]e)A-N3t%L&/1ScQeJ#R-4l"U5/8"pOrX"q[.I"R]e)A-N21"TAGY#)sK1,R+F,/d;LD#2'$F,R,EO"pPs>XTk@#OsC56(YT'&-)E5C#)XZ1L1CJm"pUY$V?W&kraZ3;Sd)>$"pP>;"j/(pIM_PZ"sa(!"q901"l9Ce#*'e5"pP*YN]7NV"pM^F"rt3"aoRn_"pXH!"sB'S"jR8U,R+@Q`<H@1%FJqrdNf#L$`F7C"pNS$,jHWZkTg?:)%HiP"jR5K#/DJgc34CV"pOZ'"qRsa[K^A&YTsDR,R+GX"pPV'"pN:>"HI!sA-N4g"l9F.%^I>amSjAp"q7F6"p_^bdVBT2,R,k6jou?h$cX7n<Yb_]#OVZ=hGXfr"U7F*"p*7,"q[IRmKR;V/d2F[,R+ALh?FP%"l<Yk!qlf[jp'?R@0PKk"m-!F#g+CsFq<d@"pP7n"qUPUN]7jRXp)NDh>r?;jp-VO#*(#9$H<A>[KQ"#PSVLO[L(Lc&.eIE"hkMuSo"VMecCd>hL8o83$RGc"hk2$N]7N^eHNYk#*(Ya,)$3W^'9TW3"#<^"l9HDN]7cE`<Es[#*(YU%.se<^'9TW3$n5$"hk2$#+?eA*sdl2#*oRmVAKT!h?9DO!nL9RA-N=r#+c$Z"qLp(Kc(@)Op7K`ScO]^"pU@t"q/Nu"h$8m$)Rm?`XJ^d!=bV(%L)Q,[K;a>FU7[F#fZuFL11\=%LTo[c3+"_R3Vt?!n[\_(%j2.#3dB;@0P3d"l9F6]bpnFecjn5"kIJn,R+G/rWX4N"SR0F)%Hh)"TAFmp,W?U%L&+#rWK>@_B]<g"U5/T#"8`^"q.se&EkAD\g.Ia)iY%@/eb4")>YT_W[%cQ,R+Gg/d;c@&#')m,R+MPXp,-""pPSBSd'3>Sp"cO,R+GWXp,?/#d,6N!<rY7"eH0X"TBVU%$hFlc2mld@Li)?#)35Wh(Aj8Qm;k?R4\[UeIdT<W$DQrR4\[X"7:+0(Q'IBFqToA"pP7N"qol?a8q\]"pCb7"p*'r!m)\lhIHfp"pNQYh?D28r>?(Keck1<"pP>:'Ts^Gn0A2B"Jl;5/e7\m%#H56!LYPE#H\]'&C;[,r>?(L"pLS##)+Z5&)&95J-(`8%L*+<!gXQ=N]7g9%K_ptQ36dt3!R>'!gWnJ)q#u9mUQM+"pFW&rWB<6eJSi"p'$0O"pP>9SLt'Ml6HQJ)%Hi\!i?1Poe$CPnK\;D?3UTA"U5/8"pafR"r+iup-rQs>m9p*,R+GW_?L:u"ho-aJKtF()%Hic!p0Xa"6CC1)%H`9!ql`p"8s)I)%H`I",6pV$*#6T3"+gP"hk2$N]7p,[0>Ck#*(YW%,D*$^'9TWng"DC)Y+(D&D\sf%H&K9VITl8"pO,imK)`dk$Jl-,R,:nh?F#G"p'egr>?(L%KtnrmK<D*Qm;k=A-N5R"muE"#$hE#fH^Y*!qlg,joUM[C^&Z!"m-!F!LO&I"l:9m"m,sm+9i"G"kF6a`>JlG"pV45"r$5H",:=j)%Hhq"kEdP!iBK4A-N4o"m,m["l<T%c8u47,R+GV%L*Ne%Sd4@<WpAZ"8s)IQm;lBB-mfiL'=.Q"kGR8@0P51"7??D%IbVIr>?(L"pO]&"pN*o`aK%jc3BjDc2in.]`k59c=gu<fd$b*'CZ2;#.6(4%DX4nmUQM+"pL:qScY3g2&Ql-"dT=h"pP8;"rR1^hJF,?/d2F],R+IDXp,HQq>lug"qK/e"q$G;"pSB<%,iH9/q/'M,R,((L'A0oL&lu//fi4Z]`E^bng"DM#+c#+&Z,S]A-Nj9"l9FN"m0>",R+G'"pP2"NWl"bO9V!TO9Pmi"p(>!A-N6m#*&nr",:=jA-N7H#/19?$\0K,mUQM+"pL:q"eKUB#JCub,R+FL"pP`k*s76A#3$m4mUQM+"pMF<^'0O-r>?(K"pN!K"qe]t"8s)I)%He("HET_]e0HmQ8JjN"pD(3"q\Qq_#]rV"riO7"pXTCM?0+t"t7_0"r?bTecCm;,R+GV"pP@d`W?'L>m84O,R+GWeclNX%(JM*Z6TVYA-N5E#)3/U"cd*tA-N6u#*o:]$)/[LmUQM+"pL:qScnatjV\O3Q3F,i"pP>:"8O2H`ZbTk"U9GY,R,PI"pPWp"pP9<V$5-J)$9L8"kE^6"csu6NZnZ3"p+H"!o5I4$i2+Qr>?(L"p<EY!i7LQ&;VS9L*?g+L'"O_"pPtML&ll1A-PL-#)39["jUW_A-N6u#*oG4#)7OI#$qK<i?SU3B3#3J"p4K&#&Psr"q:&J/q*q\W[%cXA-N5M"o\Q%#$hE3kTg?:"dT?'"pP8;#0eb($]u\=V@/Cgc38q')$9LM"kEm3)=\sVFrJ1$"pP9d"qK?4SLt'MN\q"G"p:.n"q0uI#*r3m,R+d@"pP;&[K55qVITl8"pVdG`Wb40#R7^A,R+GW%L*U#c5HQu/d2F],R+Id"pPWprW3"/>m:32,R+GW"pPI6"pL\G!j6&<A-N4_"kEbKc2j%3,R+GV/d;W=V$[6kR3VtYR4\[PV#e@4Jg:O)R4\[FKc/5<g*?kC,R+Ga%L*`Zh?3]or>?(G%KY\omK<D*cm/erR4\[KKc.]-OsC5OA-N5>ecjV_"pPtL"kI2gB0HLE"p*9Z!nAn,#fn7qn0A2B)%Hid".fVn!K[D\)%HiT"0Mb)"8s)I)%HbW"24m9SLt'MQm;k@"c`d!"pP8;!NdsX)hB7=r>?(L"p<EX"ra-[R;3qi,R,k("pPOb#-BK]%tPu1q'6.K"m-"*Sd(c?56d\`"pPFF"rH5D!LX,:(6'!c)hB7=CDHLH"tgP)"qT?3NWF_9,R/Dr"pP7i"qRsa"7?<s"U6BO"sT<b"q\QqVu`;="p_7G"qTuE(kHHSJKtFP)%Hi^"jR5KKe<N5a<Urq)%HiVDE\9\#"Af0"q7a^",:=j)%Hi<"ni/F!iBK4,R+GO%L*`*ScnEu/d2F]Qm;lQ!iQ;8/gn>E)>GH]rZVON"p(V'$/I3;#,3@IVITl8"pMF9"hnSZ"8*NA?QKBj"s*rV"q/9nV?UZSB*";%%L'!>^'"<Or>?(K"pN!K"rQkUr>>\1"pNQ^"q8!e'D34Q"U51&,-;'HY%8I\AJks^8-T:,"pUAGN<YsXr>?(V/dA0T*X7@+#GECI";[R5"r8)R"pP9<h$0/KQm;kMA-N5X"c``RefP=gmKJm_".g>;$*FQB`W<ePeJSi'NWl!Z"pP>:r@S6XJ0Y=%)"e(B^'9TW3%*ek"hk2$N]7R2eHOe6#*(YeA-N4O"m-#LNZM^8IUiWe,R+I="pPBk'a=R?)PJ8X"U5.u"pDmt"ra0\r>>g*"pUY""prX%klI2)"pUnC"qH_?%`fn"!M^,/)%HiT%Mf0F*Wu?Y"8s)IR3Vt`"c`d/"pP8;"3iG%&==^I3"2>^"hk2$N]7Z2NWm-$*0j/$#*'F`h&M\QYp9Mo)%Hih"0Md?XY'b]Qm;k;'[6go/eGR/"n<iB!LYPE$,luB)#,?\E#H=n"tg=P"r`";!LO&A"kF%Z"l9Ce+9i"G"jRP/"pbC(mUQM+"pLS$V?*i1Po1&8"pM.3"p9Z,"KE^Oc=@+`"pUY%V?W>s%gJU0,R+GW"pP=KLBI5or>?(H"p=i,-Ij^P'VH]UmUQM+"pL"iQ3*poobe5C"pLS%"piU%#,VP.+9htV#*oY="R]e)A-N47"g.sa"hbc`"f;I3"pP8;"r34FO_Z,",R,k1"pP[]ecl=b"p'ehr>?(L"pNic#1Y=0(#U>Ai?SU32$O6h!mUke!nJP"r>?)h%KbJhjobQ"/d2F\Qm;k^+pJ5Z!hTUU$fE97!LYPE##,2V"pU/7(s!s,GU*Itp1+@3"pVL>^'3q8%gKHH,R+GW"pP^F`<FNj!M1VB"pP7>#%]Cj"r+iu'!NVcJ-L`4%L*+<NX!07Q6HM;"p(n3"s'?^!T%>kA-N7H#/1;E"l<bo#.=`U"pP8;"pC#5eklVM"pE0SL'!ZOemnsh"pU(k"ps35Ncl?!,R+GW%L*uR`WMJD`\9RW"p(k-$&pP@%*9b!mKEe!"pLh,h?pb8mUQM*h?B2J&)JB8!S.FG%c1M/el)[r"pNQ^XTbR*#R-M/,R+GW/d;`P$_[Th,R+A<%L*15h?(Y6XVhTL"p*j3#5'SP#%/\\QiWU-"sns8"r)S5`>JaNecjn5"kI)c!qlfSh?Nori?SU2+pJ5m"t'TJrWW"+c=@+_"pTec"qo9.(53V9Kc(@9"U6"N"p2Ij"qfN6!fgdq"U5#$,R-+Y%L*U9-3KeV"8s)I!K]K')%HiT#"A^n!KA]8):0W5)?uK3#+c#'"pP*i"p(?0"h"Vq"r-kt%[\LGQm;k?"U5/B"f2CZ%F?4%($,VE@P2D2'[87Z,H2pmXVj;,"U89J"s10)"r6VQ&]l:&#%rAh/d2G;,R+:O"pPslScnIlG%:d\)%HhI"pM/X"q'!./q+YJ,R,F*"pPTg"rGB,X\/c9"pDUB^&uAcr>?(I%Kj]Qc3+"_M'N9-A-N5Q"nhu:#$hE+O<b#7A-N5Y"SMud"3tE])%HfK"bm-("pbA*JKtF(#*oGt[KN]M3!68e#.=`5';-TTL*?g+"U8$2"rb`="q\Qq/q+1q,R+d]"pQ-""pUbH!It=HA-N5B"Ps:T!It=H)%HiT"TAJQ"s>/qp,W?U%L&+#rWW6<'c@Ja"TARZL'73sKc(@(%Kub3L'"1hHNa<O"bm0R"SQ@1,R+D6"pPX;"ri(<m/Ui#,R+Cq"pP:C/fn&(()6k."M,i_!K/9//f"Qf"8r7Y2?a:#4p;-R"U64E"t,[*2@#s!"k"Y#<un^R*S)$]!JV!5#jr5S*cqVS!%%p<#GqUK``!-9"qS]a#)u(U"e$\@2?a9hL'FM$Q3YtAKarh?"O0+25%4SJ7Kj!&,R+I4XTeum#.eaq?mGqGE$NU3,R."Y<X&Yh"8r7YqBQ7\M`(bA('FL9-4XPj2?m[r-8lPZ!Jr'+7Kiu#HR8oU,R,kBiW]M]''3^.Jg:Oi#.b!C"pQ,l"pTZ)!Rat;,R+UX?3UU*$-!/R&#LCB!J^oO&$?&lNWGt'mLHZ,NWFe8$_AsX`Wsdu2?a9l,R,^2_?L,%"s.pi!n&=u:'Ch+A-OFS"U7.3,R.7<"pP._5os';"pQ,l"p*X-<ZYcUjrY,J"g2/<#IYrrk#2?D<XA+("pP8B*X2lq"t!@b"p'Q+-4XPjr;hg/"25NQ$gn%M$GHS%rC-gA@KasX%fQP'##HQL"pSB<*d.RGn0A2BM'`E-"pQ+L"ssgj"p1bL<X*+><WS1\V?S*t#$(p/4s\BX2?Adq>Qt\/:(A\Cm7m\A%h.S;hKBZp!PTER$+:)!rXJsk0aQVJ)m0=@#!sR>-5L+rr;hgW,R,"d*X2Z0()6k.Q6-CP%&R&.$+:llc>HPb"tTrc#%\!>g.+n=,R.9N('Xfn"pP+'-&l.6eQ`:_M]N?S('FL9"s>/qo`9slA-NeO/-],rM`t/G('FL9]g`/0YTsDV<CR/o-A;JXeOB]H!Jr-FM`q=d('F41!jEpS!LZ[e(;0h/"puU[!NI7H*b>O6%V,bb![Iss2H0kbW?_[.A-N5?A-O)=/-],r$E[>57KUqP!Pfrb7Kiu#A-N(Z,R,S2*X2\M"s,*F"p;(T*Zeks-5L+r<ZYcU?7'1e"pPT="4/:t2H0kb,R+:r"pP.e<`Uha[L\*Z!=[fp#EAlZ<_imb%@.4\XU=2u@Kqi2%.4FV"p(SRm9Bir,R-.L#R1FZ!Mfah7fePR%7"/h"O+(M"Hk#7-=d;b#$ju!RNr(AA-N5>A-QouA-R38EX$tV,R/,j2?j2n"8r7Y!K0,G"pP2.%E1d?`D-R@<Wr[H+9i)D#"AXA()+Yl:*sNV"3s5.#`]=r"K"?`:/;&5%u(J3`=;6C@KqPs#-J8m"p(SJZ6TWL?3UT9AHi>C<DH:)"t,sM"p1eMV,da(?3LNW+9hu9,R."Q"pP.'"t"4uL'C6q!gZOgA-N+D,R-.:+dE6U#$so;"p(GD%L-hU##Z]N.7R8@#%8b]Jkf8L@O)-Z-=[5a+X@7=CF7:1#$q>Y%L-r!"ssA-##QWM-6?_&"pcIY"p(J?!K/9/"pP2.#jj5J#"U!D!PfrbTd0gH$-!/M4qI''[MB]d;[2Xt%-@SV"tCl&h-PP.nK\;OG!Q<5,R+H""pP+]"pT>u^'4dJc2iY-/d0`4h*qF!@Kc*E!lbS5ND9W]i$8LF!g*Zf+dHA_!SJ_&+B<"X,R+X9/d;B6Ke<Cm!KBPn"p<CF:/3EQedJ'9!=[6a$ha_S^.qHV?jj+C($u.DY$qjSVCi&Sr_<XumK&7b4r&ec)hnBj"i^eM'\3;\&[hn0Xq'<+!Ji'@(X<9"p1juC"rs3C"p2:[!NQG086gO-b^fa*dNf#""0Db.%KZ-9#'(sn*[Y;B!h(A=Q!$9T2BEV="p^D7-3cLVNXl%(TF$>]%AkHQ^-Dk9&+1JE#L6'p!NICL(1[UR#UBTaCBL[C"qC[>/d<4>#F,c<H><Vb5U6OdCBL[C"r75sp'(jL'V8/()_MHJ2@#KW#F-&D2?A\U(aK;<!M^D7"U5/8"pLhE"0EmR#'_Bt"pcIY/gb*5/d<j2/e2\U#F/FM*]>&L,R+L=7h,_`,R,"iCd&&:%d#AR4q<$kc3`>PG6uP""MP,+hI6WE4q-!H"u[65!!XhsbPhJJdNf#",R+GSQR2n<"f2t8!JU`c"MP'=!La7JPpR!D%.6jq%K6b7"ssA9!LsFM[KtH5&"[54-mfJM,R+`**X2Z)"pP+'"s.q["pcIY/cgqa#F,Ih-8$nk/g`26"f3Ob/gbBe/d<j2#')p\#'(sn('0aK"9&J##1`g0#_iJB#PJ1e:'[%2VIfmi-39A7&"Wrj#Nc&W#j)Knp(-k+-=JM3/j1g6!KJcBG%i9U"U5/`"pLhU"pT>u"tg*2"0F0Z"rSZj!MLF_+8ur!*W_T[QSo#D"f2t82D/eu#&kgl#$<,Tqu@!&,R+Cp('Xg)jV@lV!N$>QAHinS65C=XrWJN^"GQpd"9&G[$gnd+Q='pZV@rh:"f>`=VB-&8"r7CD"r8Os"p(_L()CHc!JV*P%*f=]!JLZBc3f7HQ:><k!KdQq,R+Gg(QJ]7*UWs'>Qu+<%L*=.V%*Nc%V5he#UBTi%V5hK!LX]="rj*f"pQh."q@,.SIPf-SQl@@,R+_t,R+H5D[$Cp,R+__N>;_+-+t&p"p(SJV-F3?'##tGhB%\N5nU7/#+cOS*nG][%JCROQ3d2%J-tEHjq@(_=X41V!r)s(!R(WC"[GU`"O*P>!Lb8<(1[UR4p;-K"U5=1"r65h%PB)!"uZL=!idLMRQit?,R+GS4pD(?V(Me8L'.DamKfBbRL5>]4q@Pm^.noujpU#T4opoQ$e>Nr^((16@LnJ#"I9)N2Ekh="r\`k633K,,R+@Q"pP,!"p4'8,`PqR*aqN-7VP%U_B]=L2Dta@"p:tk/j9Ijbt%l'MBiB98-T8"2C9at-3VjGmKEM)TFL;i"m.-4L-Q(E#K@5a`XK"#!Jq9p,R+H*A-N5D"U6Rp"pLh]+-f"n#%8b]XZeX$X][gi/-\9?A-P4%"U7.+EX#6-"f4*]2E!H6"uZZ?"p'f2$3?PA,R+@Q"pP+L"f3gTXW@WM80eBA4uP;'$N:4*4q#)S%DF&N"25)eNWZD-@KN,3%$h(r"umk4!L4=i&<7-aXX4ZE5'Amf;'Z>M"tTr_!Jq"ERgoW4[LN3:U^!\&`X8"4P67BeV?,f[!=l79!qm-'h@'dp[Kir?#2Weo$.]U#NX5&F!Jq!g)AWMm"U5/8%L&[M"ssA-!iR@K)]o+cb`)T0U*KpG,R+GS7KrmnogStA!N%IN!qo(f"82bM"9&OkXYp36@Kasa$-iRs^3g%V`ZBjD(p5b+&!e6-[NEd[@O)-c%V,bJ*s^@LhED;7[MB\del+1,"q?:t#"BYF"p0B%7ODXM7KtCJ7KJK<VC!Ig#"Adt#"C4V"pPVa"st[,*X4nUXW@M&oiq\=AHi>KNCHAY%CJY+!O<sT![IsS-=d;b/n>/U5%Fjm#''oK7Uu]m#UBU$!M^D7>QtB:"pPD$'VuiS"p-%S$0>Cr!K/97/f"Qf"8r7Y2?a:#M`qT0('FL9"p-%S-38r]=<n(T%V5hKM?1dn#"D>g"p(tS"p(SB%V5i6!KBQ<%OdT37Krmj7OD[N!LP,:D\N*S%`SsLa8s3a5Y)+\$N[;5"O)u."4AG!U*KpI*S(Dr+5R$MA-NgPOTm]j%L+Nd"tgZF3\CUf"pQ,l"p):\-5L.s"pS?[#'qO!"0EFN(,.'^3$\\L3@"e5-3XSX>QtK4#!O3T/hT%n[KiBj!=m*P#keag/l)W4?j?<7%ugt*p0nQX2@]?j/d;?R,McT/iZn^t"U5/3"t50P"thfF/iEoF"u^`s-3a`$"8r7Y-;l03!K/QO*ZbF^/g^UO/hU]>"rJTi%L-hU"p'f,?mGp\%V5hKmQrNYD?I$K`WMe5:C"lK[K6(S!nL`_#IYGNjq#aH!N$>9"SOGPjoU8M?js1N']'4p!Ke,b&$?/?[W)oANY7?T%H_d>$Dnf_mLfF^M'N9>AHi>>N@lgf%.,YJ"p(S*OX(,`UD=%7"pP84"pjm,$2n*5_'B3pn-K:"HiipA!pF;&#Y82c"O$WA!QlYlRNr(A"U5/3"sMM<-7046!O;`o,R,I;2?j5?-3aLJ!L<f,&`+/_!L!TI%b:^,V@3U1"pK_jNWrB,rXOA]!N%IWN><iF#FuV2-8oE>"SP//!KIMQ[L_d,:BlOVScZnY%*i5k&"XJpNXrR-0I-i)#&a\M!M^,7V?R5/"qCh<)3#C8"qW$a"t:f%'\-Fmh-:.">Qu5V"qD]n-4IRm%^&,>"GR*"`WERL:B[g+^'4KW%\@&7!NlTcQ49^#/nG4l!LG,["r$)F"pSH\#6kGZ1+j7\#6m'f"O+1P!V.K?:,""!iZn_/!M0K!!JZ&;!nnn(!J!'4AHi>CA-P4-"U7F+"pLhu"pPD["p3d0Pu\%m5$oW?###4**sTG;N[=u(c8HLI;[L/>"pK_c$-b(+"JR.G#$qK<OsC59'c$uV!npZj&(MGE##Z]Nr;hg'"U5/5"pDmt"p<j1"p(J/('OmH"U5C#"qch9"pR(5"pQM%"f4Zl!jWpQIPqs")\sTf"pEJ*"p<:!G('Mh,R+jo!JUda#E9K<"p-%S!KI<f!N#sQ!JVA+Y($TBV?)ta"3r+l#L3C:Q3WCn@L_`6",6p7!hpqEB`Y(8XoXi;!KLjp!N#mPNXN!*!MBo0%[I-mr]gpaAc]%>`XTX4+s[?pIK]o[,R,CA2?j2E'"o&X2?k]:Xu6<XXp9pe#Gr7;&&&4=!NlHr!o=!eSctEF.h8ug#.=\q!Nm@W,R+D%IKfi<"8r7YMZJP:"p=f)!K-u=Q3IHir;hfW!M0Jt"8r7Y:a?5L2H0kb,R+:$"pP*q"pP9<$3hY!/fL!e!N@=K/d2F`#dsc!r[%Z#KaQB>"f4*X"h%l(.0^07,R,SZ"pP-Z%0dXp#'NUS"p(YJ-u0R2Bd<m8!K0t_"pP2.!khnDm:HM+D?U4Y+9hu)%c1M!XrleI?;q+8,R+=K"U5"<5[6,NRK+-p,R+Cq"pP-t5!B0^jpJgh!<`<*$e>d\5#23^!M0CR[0VLp@KinW"82nQ"p(S*-=mBF!KB9,-6S:34pD%b!l?2e2?a9h"U5+;"ppPY)l6"-!R`(pTa./IQ3Q1H%V,bR\g.J4#jqlt<X53<-5[)]$e>a[V@9%p/n>.g"qOa@<b)C=+s[@>2?a9hAHj3@7iji@,R-F<?3UTPFtj1*,R+H""pP-d:'P/Q[<_kAC*Wud!M^D7>QtB:"pQ@G*rR,`r;hg'M_5JA%KlY1#%/\\8N'NP#&tmm-5L.s"sG5r%L-hU0`d\;,R+dM"pP*a:'NEuKcpJbQm;kNQSo$K"._=N!JUs<%-@bc!JLs=L'aCl"5[Jr>QtJ:"th.&2Bh`uhDl1&[MZ^h!TkR6!R;)HQ4Ns5@2-uq&*=DZ!L=Du"TA\p`\InHNYpUg+1<'p,R+hZ"pP+%[O*&,!ra8F(_3,qSd37^L'CQc#$j+jcm/f"S-K6!!K76a5Y,.-baAG>\g.I_AHi>@A-P4%%mEGm!jW"7"pR87"p<R)r;hgGL.2p_NXMEZF:S9S/d1T;[K>"[;[9`8%^#lQ<XrXE!eM[%SQl@'AHkUN6:OFF&!dI_[K-l8Sd=H]SjlK_?ieO\.,>(+Smh`G<WU2L7KrmjSj`^@NZNoX^&a6+mK]m##,W=N)4hK7SeL6?M@\hRp([3$F;_4g$-!C>N[Fm_Q3c=P(X?V0,R+CC4a%$=$cX9S5!EN^"pS?s!P0N\3[=n6%V5hK!K0E2"r7Ju[S@lT$0E<7%N%*);`4`4$B>56!Lb8<#&a\MC-NaLE?k_m2J!'s!M1oM,R+H:QTbUG)Wa-`/cgqa>QtkL^.o4;[K2*i^(8Ko-38f&$2+eXXot>*!KAuH"rN=X"pS`d"pR[F)sofm7Rgqn4opOLfd$bk%o*<0#X/FsAHi>C,R-FZ,R+H,*X2Z@"pP+'V'Z?M/d2G."U5OG"rsa'$O-k^!Pe_hJ,t`:X9#d383hP(S,`t&,R+Cq"pP+V"u[6-"p0r5Ad38[/nSVu/p7>KOsC59"7cWs#H_<]!Q$)dCD4qs,R.Ri<@.nn%-7Pb#'qO!"pSB<%L-hUAc[m'VCiOqSn80-^&`a%joiC7&Y="Q*8V"1!ep`.'CH^?#GqNh)Q"52`WMe?;'Z>^`F]A[,R.Q]"pP*k"thfF"pS0TQ3LM<!p1R.(^82sh?fK##)6%]?"O:q,R+GW++=E-r]UL?NXcm8;[UMF$e>B^"p-%S84HP_#&#7d"p(J?V-F3/>QtBC"qDcp2?mV9#bD'OFB/qG4orq]L'N_=Sl6C/"psE$-3dTueKt<V%V5h\!KC]'%N)TcAd/:5$h[a/8gFT^BI!cd-3XSXM_5js('FL9r;hg7M`qmY('FL9`C:"8:'Ch("U5-i"pWma7KLmZ,+VsC-=mAc!KC]'-7@>UAd/:5AhIX!#'_BtNccE*"rNp?#%fJu#&ZV8#'Mn8#R2+k!fR2:5Y,,b%fr_Y"O)Ar!l?2e*b>N[!KB9,*Xo/74pD%b##Z]N!TG@/JJd5JQ5&0Vj]i!0N@lOp.E*n?*W_6A,R+k""U50'"tB3t(p,\"4pGR&!K&-,#%[uC!KB9,"sMed4pD%b-39)Q,R,+AAHi>uNBTNY.BPc7"p(SRh-:.J>R!(u`]a[1h>r?@ecD'I2?ALB#j)Jb`XT([$kBu\%d"Yc[OMcA[Lg.o*k#5dD[$Th<B^ls"sNpN"pQJ$#!NN.5"6<!#"Cdm5"6<!5"7_P)s%*f"tTsB![It6!M^D7"U5/8"qK`9*7ZA>#$<,T+=&WH&f(a4L&_Pt,R+Cq^'4V4'u",XFs7.I,R+H"D[$CQ,R+__,R+Gc,R+GaAHi>],R,S*('Xf^"pP+'!JUd?%E8\A!JLX<Xp]q)"Qj/#AHi;""U6:X"ruGO"u[f=2EhU]"pP8B"su6="pS6V/d>,tKe<CcL&o!tXouH>Kab*l.YSoEmU?==ec`DjXoX7eSe%t-2?AL8!lcAU^'hqi(1d[Q!KB9,"qZJP"pQh.*;qc!/n%6k"u%;,4sjeE"pS?s"s>/q"p(S:X]u&oAHj1^NBTNQ)P'%m"p(SJ@iW3H*53;nQ?Et6!o>gV`WD/RY"^ee^(\3g)hqf=!NlXpL(h97:(lt%%AjH/^+C,'"p24<"U6(p>^QR*!"(t^"pE/*"p2(UaoRn_"r=$9"p0r5"p(J7*X)`P"U5g?"r`1B"t$0OV(MoU#UBTi7V)c.!KAuq7N;:a2?j2Z!JUs\&&o(@!JLTHmLPR8"Q!SnQQ?I!(@<.D)VnX&'a(%&&;CMJmSsnnjoY5a"K!dY'Wr=VQ3N(\;%0TF&C(R,%L-hU2@a:&4r.]64uR48!O<sT[9Nn?AHjJ-68g_S&*<qbmK!Qa^'D@Uc8JH-?k0UH(qg6QVIB_[/dmC92?j2Z"sP;s2?k]:#'hHu^2FP="rbbq"pPni2Ahui2?m^s2?AdqV?Rn9"uZYd4qRBl!JUmB!hK\k!JLg)`X.>G!JY%f,R+P2AHi?0Cc4HuQo7,d2BB1+!Jr'+"@NS$#VZZ8&f;BDbMN:+;BuGL!M^,/%Ft^f((Q?H%N#K>"QfsWp'\$%#%[uI(1ROi(,-dF-3XSpA-O3b/-[^J,R,k*#keH.*VKN1GQ\uZ#tR<d"O+^_"PP+*7PtASL(0GLp'Kj^!KU7k$`4*q/i!jDA-N5*A-Pd],R-^b"pP4YG'7b6"pP9;"p'T,G(+dU".CJ>!K2+*"qCb6"pQ4r"p:#6'U;o-*X)`p"U5pj"rlAVG'5KLNHP>^Z6TVa!JUdZ!U9]R,R,X0"pP6o"tgBr$dCR5m61[X#UBT]"qb/G,R+@VAHi?`A-O@b,R,k:L'@dTnc>-Z"r"*?"pS`d"pP9<Fp9rPc#s,I!N%ai+5V/FSd_2AK*^$8^(83aCD39J%$hIUY'pJ-"rscT"p'9#"p(SRQ9mcYXq%$)"0MY*#,V[g$cYA+mU-6b-6nKPXok8)2J!'sGpET;R/r$HXT>*rRNr(Q$N:51jpB<:7Uu]>:1OQ8<b)DH!M:iZA-Q?E#JOeFD@D7A<Yu=d#G)CmNX"WET)mjN?=!g:!n%8b!VmuF#$qK<#&a\MDI`q]A-N'pNWGGh"pPS=!KL[pdNf$7A-N5="U9-^"3gk$!NICL!M^D7"U5/8"qmIJ"ssOc"p)Lb!LWtC"Q]Z9##Z]N!T>:.*b>N[-=mC!M?/6N#'Mm*"pR=<hJO*?joL2?V@)/aL&lu9L'=sc!q'M",R+Xr%/'lTILc?!()ROq%[IIi[KN1#L'7bi"p1=rL'@])YQ:-m"q%1-"pSK]'`j$m"sG5r"p'fd/n>.j-=d<U7Uu]mq'6/N,R+GSD?^0Q"8r7Y!K2+2ILZJY"8r7YL'7bk"p:Cs#6oH!!i-&0>X1i-bbb@R66la<-3XSXAHj+(6;C96"hk#gjoGb5FodU5"I;eYra5uNXsTDAp)[ZF\g.Ic,R+GS@g3,e$DoF."O7%&$f238SONX6@LhN$"uZS7*X6@)"pP+',4/V>-3XSX>Qu;;"qD?d"p4WH4r.Z5:(Ch>rF/c%&gRZ-.TJqB,R,']"U50O"pBWLWWmJh,fO?t0dHrm/p:+sOsC59A-N5>>u"-I,R+GW"pP.7%Nb"B:+g)^:'$>LVCj4g##5@'"pP>Y"pRmL"p+QG:+g&]<]4Im?9Vm(:'P_C/l#p]!MLbC!N%aZ>R!q-#!N^N:/2")rX7jT!=[ff&&o"6:/;5b"HEl($2$Jic=.<="q'2s:'L`r!LO&Q(:G3[c8uI.FruGGedqIS3$+A(!N$Wuc799orZAQd[K3*9h@P\M$C2J<&B5[_!WE+s,R+@Z"pP+E"pQ7s2D,bV"8r7Y!K0,_"s*mF"pPtk"th66"p0<#%L-hUSONbei$8LB%c.Bj#6"XX"0N-Z[711N@KZ$#$GH_92@a7%!T54-I0>9/*KCj-!MEF^,R,LDO9c%V]E4<!>^QbW!";C`"pE/&"p1MEr;hg'"U5/5"rc#E"p0r54opX,VB-A(#!N4l(*K_a4sjhF"puU[2?k]:4opX,VBut9p-&Z]&+2@p+8-`;V?*P79G=9+#*p%=c>4GcmL.SbQ2uRJQ552Y%uu#H(tK7aQ3cV\\g.I],R+GRNA^ue&X>6T!JUWp$.]O9!JLRJ^)uGhmRCXo*X)`LAHjL;,R-FR@g3,[0GYu^@g3\q%Yb`72@#6a()RJ:"/Z@,V?Ebp80eBJ9G<-b%^$.n"u7G.#!+"6((Om[V)AJ](1d[`W)Z#F"q.7."p(AB-4XPj!Jr'+!M^,/M\ZL$"p=f)m2cE8#UBTpGR"BV$:%-e"O)i*"p(J7j]huo"U5/X"sN(L%OLLI-5L.sJcV8d"s2"k"pQ7s"pP,S"stC&"p2pm!M:%>"I9pc-6?_&/gb-6/chXmU*KpiN@"i1%J;%K"p(S:NEcZ7AHjb%67+$;&)ISHec>o^ScS6gL*f\,?k(*YN[4!u"GU@nVD\^o"ssNT"suNF-3eK9bpEIN!N&U4"i_knQ63h@%fuYG%'Bcrr_F%'p)BS1%G#t>#3HcBVAn)b#&a\a-?a\F3$\\4!N&<jN?0DN)tb6Q-;J+V#&tmm-39G[VEPFe"ssNT"rt<]-:VPN-39)Q,R+qLAHi?9CaLbUQmOF4-3r?--3c"*"t;b`-6?_&#!!q5-4XSk-39)QV@FrT"ssNT"st*s$3k`#,T'3G&i^.>!!b1f"pE/B"pP\c"qCiU"pCqO"p(J/('OmH"U6$-"pVa^'%J<4oe$CP\f_1XAHj1WMafl?('CB6:**sN!K&-,DA%s6(p4p?"pSB<joL3cc30F?*W^rq"n!c*p'Ue.%V5hTng"Dd"U5/3"rN%8:*TXA!ML$Y!r`Q+N[Pt:p)Dit(?SR2%ZVOKmLSH-CEAKFXo[r3#53?L(Z#mu`X:jV#UBTY%V,bJ"qOI0"pdBsE[1hn2L\g6,R,U_/d;BWoe$93!KAEG(+?R;c49h?!=lOE"eH4D(/G+:&&ndEPmuSXhI6s("p<-T7Lk\g%c(@oTHj_*#Q4i-"uZg("pRUD#'h7h"p'T,r;hg/AHinP&!f+#c9>h^NXcm,;ZZRp"m-<7"pPSr"pP>[`?k`m![Isi/nG4k2J!(F*t7QqVEP=+SdlSC;[M"X%up[fr;hgGSgG3T"pQ[\"pPD["pT#l,Gd!r2Ek[V!K$rH,R-.Z,R+GaAHi>EA-P4%OTn8b"pR6l"p*s6^]BZ,!K[Ne5\s89!"2>&"pE/B"p0r5!l@/kl6HQ<AHi>?AHkU.Tghce#"D>g#R1>U"pRP?"p1eM7M]P>#"h*L"8X8I-=mAcej;hq[K>Ru$-iPR%$h-Q!rbYb:1+?n#PJq`edChS!N%1S,R-^B7KrsH&_-oL"tTsJ*b>OF2F9l&!R:h6p'%l*:C"lKQ3!:@%fTi>$gnLY:(H]$"p(SB?mGqG!N%aZAHkU.QUXG.'(n]p"p(SbJg:OiUF$0G"pP84&Bug2XYp=e4p;-'<B_f/$H3%."rJTi!L4Yu)<Mjo:(Ch>7RhYu#"Ec4"sP;s7L!E."pcIY7L!E.#!jL=#"U!D2?B.&NBSDc&B-g67Rgqn!ffT:"eH*nD?\ktL.4sF?jFse,_ZVtVIB\J7KnMK7Krmj!M0kb'ZM,QNa30USf5lm&"[M:#0%Lg:'eiBeeJG8^)6hT*N",t$AK8?!q$)QD[$ff>QtZBr[o.tL&m#6Xp:O)/cgY9"jSF5Q3.%p2J!'n!LH8F"s'7("p+3=7M]P>!MC\B+=IO#('kN]$Q(QW"O4^^$IU1(MBiB1<IP,R*h<D="pcIY"p'fd!K17g"pP2.-f&A<[;H-PFp/'P,R,0`"pP9h+,,.I#biRKR/rTXeHi/U=sO:RW]tq:,R+G\Q3IGs.05h;+pK*BrBC=SSmFAWas7/l+pJ5Z#1YAU#(Bl<!T/>M?8r"GiZn_W+pJ5U!iH'j?8r.C,R+:@4pD7t"8r7Y!K0DO:'Lh!"8r7Y+=%-q-=J5*!LP2d"qDTS"pg)3!J)L#!LP2</g`"F"8r7Y2?a9h,R,=7Q[T-I(PR;=eH)%d"U7-t"t8k.!M+*3"7d]A-3XSX,R+s:"pPCf"p1MEIP,19%+[ao!=k-%%?:ItISUB<&,$4QV%/Mn@KGU$!M0JO"p(SRQ!=NB,R,;/Ad/=2Q"C&`-=mB+!LJ7a-7JP9IKfhM#&tmm#kB5F"qQ.e!LS<?IXWBXQ$ran!LS<b#$*.3"p:#6#Po\M?5nV''D;a(XT=?/;@Wm8!JZ&P#FtmRL/JKcIK>0("pP8B"pR@=&>agZ$(<+D3!#!CNWG/H"p'edQ6HM;]`IR&Q<lNRQ8JjI"p(:r"p;F^XT=>,;@Wm8M_6>'"p^\"#.c&a`I&F*M_6>$"pC1l"pPSb]`F$D,R+GX"pP@m'$YS'obI]82?a:&!VQi-^&cMCP6RTuc2k'KMB1OoIMKj'N`-CYc4?c[*TdMB,R,0q;@Wm\M`*1?"p^\"2E"GC"pPSj!e;O#!L[g0,Llgp"Hk#7!K2C2('Y#WL44SLXT==\#bD5%!fd;9$LS);!KI27"c`YDmL/_tQ3@Hq"p0bb#(@mi"pKl0#%8b]V+q0uL(;3EjpZtFKaO[c"/Tl9Na`0hILa("!gNeEISppcMZJQ]"p0bb"8F,G!L[g0,fKe`"1f`^!L[g0"rF*CD?aPqr@%c8dNf#>"U5/3p&`cnV?)D\Q3Oc,L&lu7rXb=q#M*>i"U5D'+mf;jIQASU!N%%F#5nUi!N'0_VI!d;"tTrZ,R+:HIKfhp"p;.q!i[FLIT-LU,R+:"ecl6Gc2iY%[KiZ8-38f,%c.9cQ4<7kW[%cL8-T8$(+(X\$`4BA!MBabXpseG"3*S/.gD:l/e>Z[#-@oaaWq&p,R+GT"pP1P#Mjh4#&kglm3Vu@RNr(RM^AW2"pM+0-72V#SH4X$;@Wm9,R,kR"pP1X+QXpR!h^eC!K.El"pP2.*W6`Wr;hft,R+GTIKftd!gNeEISppcL'ErmSdWO=K`d>9h#W62@LDf>%\=$!"pPTe&>^t-IWf[Z_'B3hMgbj2"p1%jIXY01!K-u5IKfkeh$+&oIXW=DIWbd]i$8L2;@Wm6%jin]#3?$ndO$:e"t"Ko"+_^%-PYA@"0m<("pTO"IKiUo%tt%OD?U4s,R+C*-3aYL+QXpS-?]FDfd$b+?3UT82$O6h"p'I-%fq\7!JV*("g.mO?7$)`%H\0"rWJ5`PQSl+Xp=Y$&-qV*!KIYe`bbeNedJVb$0G\'+pJRd##>8["pFE@"p'7'IUiWe*b>O^&0qI/Tg'k.,R+GR"pP.m*9DVQ!l-&c^d!8P,R+GS%jhK(QmN"A-5YbE]`FKI,R,:t-3aPH"p1G^"G%g&/lEGh!K/ig"U50:"p0cJ!f]YQ"gK<Wcj'aZ86d,8rr<j#,R+Cq4pD2U`b&7*Y!,P`LC1HqrW:@G!MrNu#j)2[!JCKFc=RF/".is+ND9Si"f4rp!SSe'V&;H$,R,"f/d;E0",1>+"SsAJl6HQ<R2,u/XU*>URNr(>7W&D2HVS0bND=(L"f4rp#a-G;&gRYc1BtDp,R.RU0Ko[YNE0Xl"f6);DJh-l#$E2UIU6OhD?b+c/oDDADJi>nDE`DZ!V.K?!LXu5"ml?%#LXk%\g.IaQUV/["f4rp#0J1q#$qK<5%Fj%OsC5IAHi>>$BA^&D@EZi()RLH"KhdFp'L.c?=X6H#''okMBiC<AHi>?A-O@b"U6Rp"pLh]"f4Bd-727f#'(snDJ!<hKk;8VMBiB.R2uP;#L*SV#&kgl/LCW*%PB17/N*b:!jX'U%V5hK/nnp0(1d\^Amo:h=<n)g->4_1!KmPe"pQDt"pKl0Kf0l&2CSh4E!b&1"uZUq4pG[G"pP*Y!JU`sVG7Gh7KJ2K@LC[<&+0Ia(/?:_!Kn]4Aeen^Al9[&5"9-rOsC5q?3UT9.0]t\,R-G%"pP1@"th66"u[6-#F.IV!hpqE7T9Qr,R+>@UCIK"%L*+<('UNP#F-/:!LXuM.K'=(!g4f5<Wr[3+9i"G+9k;,"5Y@[!o<s4!k&I0V+q&F@KPZq"7?2eV,da(!LI*r"pLhm"p(YJ$78^V!pV$84tmHN!KB94%L&\(5#+j,Pu[pF<\P!g<\R!D9aUN"c3T_B:'$+PV+q6OL'%&LKFH-,(';JVY$qU<;D*+u0Lf)LHVRmR,R+HZTEYFR7TOC."pcIY!n&1q#''o;*sh!]mM5oe`\&);;Z_se$'#,-"5"k'(1ROQfH^Y:AHi>@BkCreAHla!,R/-%Ad/@AQ!OKX#%[uD(1[VefH^ZE"f2D(#"D.!"p3I'm61[X7Kiu$2Im"D<Wr\&,R+FK<X&T8Pu[pFW$DQP,R+GR0bssC,R,"g"pP1^"pP9<"qCiU"p9B$/d>kkVB69H"h$<"#K@Mrk#2Gd"p;:C#F.IVPsto]-:e=G%V5hs/nm4%(1d\.2ISL-\0M8B<!EO0"p2k8"p2"S^+p)A"q0PqoE)fbkQ.ac&-:Pe-iaGOLgWB['QcLjVh.+Nd$d0EEVM_NdnDu:\edP*B?a!r(*6FA3VTjXB6M&PH15YGj53DgR3fi,%$1Xr-hd<s-RL2o-P@e3-YOk3%V2(5Bd`>$9.*C\]pqS?-hURHS2MaH_%dA<MC/`Ye_^qA2o*\r26YP$T$==i81/Y;4G5#N-P7`uS2hsir3dGIfpi'+-e$)6-OD/_-R:'e-^#hXS<E)Br3dDu\XWjO-g+ZUnH((D$*X13?l%h$l[^Cp-TEJXq1eDf-RT0I1FK"Hb)E?j-gBjmqr[Dq2^^X6V^GRo#-V)UUJ&#c+"%gTW0rj>S.@"-jV$-A17U*I>2.F"B.!^++n;EWk\qpE-_DdDYF[,+&!n+;-OHDnk^Ot4-fsPtq>'O`f!QF"0gs@!;_&FWbkqJDBse)K=+^['-c1CsQ^eI3-e,45nVAPYUaQ-^BdZF7""-/X?<97Z"/"B8"pE/S"p1N="pQ[aeP68P[KQj9V?+(.*W_o7$B>/K%DDk;!mV7X"I1;eN]@KmecLR/c2jXDmK)_X9eJ(*"SOiN2$O-U#!N<Y%L)uSXTRQ_;K2mBh%rNb#0mDS!MrNp"qM6Y+V)3j7Rd`NXp<f5GQX64*Y,Uf^'qFT3<uto$/Q-ZSPB=mg2R<#E<ZUKIKfqDNWZs4@0))`!KI28!K@,7"N:CrIQ.GC97?nr"r.6rIKjh4"pP8<h#W66>&a`I.1c+V!mVOp%L-hU!jX'U#m%I=%0d"@XoY[0!SpBL!NlODblQWn9*)I%k%+RI!L?Ok$H<+T$bcY4E<ZrN,R+H>"pP+-!NlV9"pk==9*)1"!PJNirC[0o!OaQ]SHJg`Q8JjJ"p).5Q3MSR]e0Hs!LFi/"LSQ5('P*FQOX.P[17olU(5`V`XTog0*CnSh?C#.#hD!A*8V(c']&k^!QH%TSe\[N>TUTr(PVs^V@=#qefM76']&rr&H3N,[N1B8JT1l)+pJ5Q#Y#V2E<ZUOIKfkA>lp!YNa=RI#]U%LW?_ZP,R+GS"pP-Z!M0=X"q:b@!O`$3[KZpB!jtZ%!O`0.m/i8?9*)I)"cWT.h+IdO!M2^\[REVXX^t-+Q8JjK"p).5IKj5#NWS;[@0))`!KI28!K@,7!i5pPIQ.Dj97?nrE<[.p"pP.u"p4QFJH-JdE<ZQj"pP:3!S.Ga"pk=e9**TJ"3gl&V+UjG"3r1qh>t>eSH@(;Q3cURqJc]B,R+GV"pP@M"p+QG?3-m/&(VVa`?#&Hob.N55mAY<QR4$p"77E9"L<"O#m&le!R:lR"pP85!S.Ga`W;A0!R:lMm/i8?9**lN!pp$EX\/]7!R?)rh#Z>)9**<@!Q>)qKhDHdecE_k!SpBL!R:hmeH+K!9**lP!M':W^/,';[K54cecD<E"q8KX"p^<2eclQf#m&l`!S.GZV$66Z9**lQ##tg!h>t>Jb&IUd,R+GV"pP:#L'XAhOsC56!N$&'"pP8<!KI2HXp#"6"p*N\Q3K?h"pP,0Sd#W.#m$n("U5/8!SmdZ"6sk1!M]i'rWWC_$,/e8!hKY:$hb&5L.MoY"R[T<$FTtd$&/o/mKU[8$O;Cm!M0=hmK&Y$"pL8##%@VOPo0UgXoYs4!Noi3!NlI3!NlH_!gNqCXoXhJ9*(mj"MFuH!qL=8)$-<5!O`&pY$h@6XoYs3XoXL_Plmi`!M27P!N#n#]`J()_Job_V@Ee3-3Bb>hF%eGQ3sbs^,?)u8IbA(`[2%I?3,Tc%e]ppV?Wo%$O#Sm$EaQ0-39G[!N%aq(>^ABedKc/=9>?7%JCI$Nd2`/VB!kh*2\&c'&F>BrWB;<\8_]gVA9@;-7/ot-71?VeKt<V!N%INE<[`o,R+G[,R+GY"pP+Nh?I./"pPA7"p+hN"q:b@!S.;&N<.j.9**<?"jI4Im7RK:"pCb(h>t>J^a'*b,R+GS,R+HE"pP1H"p0<#"pk=e!L<oo`WcI`!ej8J!T""ASHOpF^,6)r"p*Q]"p9r4"0!OM)$-T=!O`*DY$h@6XoYs3XoXL_]`al,!N%gY!N#n#K`pd9YTsDT!T""^h?F/j!SpBL!T!jZ`<#X)^/,jP`W=osjoM"\"qT8pV?TR:!rYam2$O*K!O`#`#!aF<-6?_&-39)QC*!HL,R,S"!N$''Pm!(>L,B/9"p).5"p2ml-5JlO!LX>0!S%7&#!+"6!IuF")$,a%!O`)qY$h@6XoYs3XoXL_Pm,\=!KK,A!N#n#K`o(^YTsDT,R+GSc3=@6!k9KcUD=?B"pP84h?ERW6jfLm$Jl-0"-*_;L2&=N-4c+F^(84cEsTbledAj.%b=\^(ENbB('Xm&-3WEJ"pPSZrWZXL"UO5u"p:D;!RUqK$!mqH%0d"@B1>o+).mIbc3CF/h/3AuU(31eh?_[50*nukmK;!_ei#MsIK]o\8-T1HNFlcL"T;p5<]C8rPtPFq2$Q2F,R/]]#6k77$j7Oe*"WAi!!!$"]=],HU=function(X,X,J,e)e=J[34][X];return e;end,Af=function(X,X)do return-2,X[36]or 254+241;end;return nil;end,j=function(X,J,e)J[28731]=(5434861137+(J[12580]-X.S[0x9]-X.S[0x9]-J[0X02498]-J[0X66F0]));e=-0X1+((J[0X3361]<X.S[5]and X.S[0X7]or X.S[0X6])-J[26352]+J[24343]-X.S[0X7]);(J)[13644]=e;return e;end,_f=function(X,J,e,C)if not(J>0X9c)then if e[0X2C]~=0xe2 then C=X:If(C,e);end;else X:Of();goto Z;::_::;X:bf();goto j;::Z::;C=X:df(J,C,e);goto _;::j::;end;return C;end,N=function(X,X,J)J=X[0X354C];return J;end,Jf=function(X,J,e,C)C=0XA6;if not(not J[0X5bfd])then e=X:Hf(J,e);else e=(-0x37+(J[0x43F1]+X.S[9]-J[0X305A]-J[0X3e74]<=J[13644]and e or J[0x5da2]));J[0X5Bfd]=e;end;return e,C;end,os=function(X,X,J,e,C)return C*16777216+X*0X10000+e*0X100+J;end,m=require,RU=function(X,J,e,C,W,y,I)y=nil;for T=0X59,0x00C5,0X36 do if not(T<=0X59)then if T<=143 then I={X.y,nil,nil,nil,X.y,X.y,nil,nil,nil,X.y,X.y};else y=W[14](C);end;else J=X:wU(J,C,W);end;end;e=W[0Xe](C);return e,I,J,y;end,Nf=function(X)end,gf=function(X,X,J,e,C,W,y)e=({[0X1]=W-W%0X1,[2]=X%0x4});(y[0X01])[X]=(e);J[C]=e;return e;end,Xs=function(X,J)(J)[27]=(function(e,C,W)local y,I,T;T=X:J(T);goto l;::O::;y,I=X:W(T);if y~=-0X2 then else return I;end;::p::;X:K();goto J9;::l::;X:u();goto p;::X9::;X:Ts();goto e9;::J9::;T=X:Ss(T,W,C,J,e);goto X9;::e9::;T=T-T%1;goto O;end);end,q=function(X,J,e)e=0X39029eD5+(X.S[1]+X.S[4]-X.S[2]+X.S[1]-J[0x4695]);(J)[2550]=(e);return e;end,iU=function(X,J,e,C,W,y,I)C[39]=(nil);C[0X28]=nil;C[41]=nil;C[42]=(nil);e=(101);while true do if e<0x32 then(C)[38]=X.F;if not(not W[8011])then e=(W[8011]);else e=X:Is(e,W);end;elseif e<0X5f and e>0 then(C)[0x28]=function()local T,T,f,U,m,E;m,U,T,E,f=X:_s(U,m,C,E);if T==-0X2 then return f;end;local r,d,V;V,r,d=X:ss(C,d,U,V,r,m);goto I9;::C9::;V=X:ls(C,V,m);goto T9;::W9::;X:js();goto C9;::y9::;X:Ns();goto f9;::I9::;d=X:ps(d,m,C);goto W9;::T9::;d,T,E,f=X:Js(d,C,E,V,r);if T==0xa077 then do goto y9;end;elseif T==-2 then return f;else if T==-1 then return;end;end;::f9::;return V*(0X02^(d-0X03ff))*(r/(0X2^52)+E);end;(C)[41]=(function()local T,f,U;U=X:Ws(U);goto r9;::U9::;X:Ks();goto d9;::m9::;X:us();goto U9;::E9::;T,f=X:SU(C,U);if T==43097 then do goto V9;end;else if T~=-2 then else return f;end;end;::r9::;X:XU();goto m9;::d9::;U=X:CU(C,U);goto E9;::V9::;end);if not W[14767]then e=(79+(W[12378]-W[12378]+W[22637]-W[0X5743]<=W[24296]and W[0X13C6]or X.S[2]));(W)[0X039aF]=(e);else e=W[0X39aF];end;elseif e>0X5F and e<0X69 then C[37]=function()local T,T,f,U,m,E;U,E,m=X:UU(m,E,C,U);goto z9;::g9::;T,f=X:YU(U);if T==0Xbb6 then do goto K9;end;else if T==-0X2 then return f;end;end;::D9::;X:aU();goto g9;::K9::;m,E=X:yU(C,E,U,m);goto N9;::z9::;X:gU();goto D9;::N9::;T,m,f=X:fU(E,U,m,C);if T==-2 then return f;else if T==-1 then return;end;end;end;if not(not W[27804])then e=W[27804];else e=-0x30+(X.S[0x2]-X.S[6]-W[0X703B]+W[0X703b]==W[28731]and W[29733]or W[12378]);W[27804]=(e);end;elseif e>101 then C[42]=function(...)local T=C[0X26]('#',...);if T~=0 then else return T,C[7];end;return T,{...};end;break;else if e>50 and e<0X65 then C[39]=nil;if not(not W[17845])then e=W[17845];else W[0X001b90]=0X58+(W[18069]-W[0X6c2E]-W[5062]-W[27804]+W[0X4D8F]);e=31+((W[22339]+W[27694]+W[8011]>W[0x219F]and W[17393]or W[0X285E])-W[0X4a33]);W[0x45B5]=e;end;end;end;end;J=(function()C[0Xf](X.c);end);I=(nil);y=(nil);return I,J,e,y;end,EU=function(X,X,J,e,C,W)if W~=0X3C then C[J]=(e[34][X]);end;end,L=function(X,J,e)J=(-5945920082+(e[23970]+X.S[0X3]+X.S[9]-X.S[0X4]+X.S[5]));e[30941]=J;return J;end,aU=function(X)end,df=function(X,X,J,e)if X~=175 then J=e[0X25]();else J=e[0x29]();end;return J;end,W=function(X,X)do return-2,X;end;return nil;end,wU=function(X,X,J,e)X=e[0X0e](J);return X;end,F=select,Ks=function(X)end,S={50140,2264512365,3576561637,1307939176,959867143,492557402,3456605620,3960917562,2717430416},xs=function(X,X,J,e,C,W)W,J,e,X=C[0X10](C[0X15],C[24],C[24]+0X3);return J,W,e,X;end}):T()(...);