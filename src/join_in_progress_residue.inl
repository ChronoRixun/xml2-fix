// Included in join_in_progress's game-thread namespace, after frame_gate_run_bridge. latejoin-residue P1/P2/P3
// (O:/Projects/agent-work/latejoin-residue/RESULT.md): canonical synchronized-RNG epochs inside a late restart's zone
// load, the AI brain free ring back to identity order, and the development-only probe ([Dev] LateJoinProbe=1).

// ---- P1: a synchronized-RNG epoch at the start of each deterministic segment of the zone load --------------------
void segment_reseed(const char* segment)
{
	if (!restart_reset_enabled || !late_restart || !restart.should_reseed_segment()) return;
	__try
	{
		const auto count = *reinterpret_cast<const std::uint32_t*>(rng_count), seed = *reinterpret_cast<const std::uint32_t*>(rng_seed);
		fn<void(__cdecl*)(std::uint32_t, int)>(seed_setter)(native_seed, 1); // the synchronized stream: seed, count 0
		logger::write("join in progress: canonical restart - synchronized RNG epoch at %s (this PC had drawn %u since the last epoch, seed %08x); every peer continues from %06x with count 0",
			segment, count, seed, native_seed);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		logger::write_once("jip:segment-reseed-fault", "join in progress: the zone-load RNG epoch faulted - this segment keeps the running stream");
	}
}
// ---- P2: the AI brain free ring back to identity order at a late restart's zone load --------------------------------
void canonical_brain_ring()
{
	if (!restart_reset_enabled || !late_restart || !restart.armed) return;
	const auto& d = actor_ai::installed();
	const std::uint32_t stage = d.stage, controllers = d.controllers ? d.controllers : brain_slots;
	auto* pool = reinterpret_cast<std::uint8_t*>(d.primary_base ? d.primary_base : brain_pool_stock);
	__try
	{
		const auto free_count = at<std::int32_t>(pool, brain_ring_free), read = at<std::int32_t>(pool, brain_ring_read), write = at<std::int32_t>(pool, brain_ring_write);
		const auto bitmap = at<std::uint32_t>(pool, brain_ring_bitmap);
		if (!join_in_progress_core::brain_ring_reset_wanted(stage, free_count, controllers))
		{
			logger::write("join in progress: canonical restart - the AI brain free ring is left as it is (stage %u, %u controllers, %d free, read %d write %d, held %08x)",
				stage, controllers, free_count, read, write, bitmap);
			return;
		}
		std::array<std::uint32_t, 32> ring{};
		std::int32_t new_write = 0, new_read = 0, new_free = 0;
		const auto held = join_in_progress_core::rebuild_brain_ring(bitmap, ring, new_write, new_read, new_free);
		std::memcpy(pool + 0xc384, ring.data(), sizeof ring);
		at<std::int32_t>(pool, brain_ring_write) = new_write; at<std::int32_t>(pool, brain_ring_read) = new_read; at<std::int32_t>(pool, brain_ring_free) = new_free;
		logger::write("join in progress: canonical restart - AI brain free ring rebuilt over the %d free slots in ascending order (was read %d write %d free %d; held %08x = %u slot(s) kept); brain generations untouched",
			new_free, read, write, free_count, bitmap, held);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		logger::write_once("jip:brain-ring-fault", "join in progress: the AI brain ring reset faulted - left as it is");
	}
}
// ---- P3: the probe ([Dev] LateJoinProbe=1): milestone snapshots and the synchronized-draw caller trace -------------
bool probe_enabled = false;
join_in_progress_core::draw_trace* draws = nullptr; // allocated once when the probe is on
// follow-up 5 state (the functions follow the zone-load bridges below)
bool smart_hold = false;
unsigned smart_holds = 0; // updates handed to the stand-in while held
std::uint32_t smart_manager_getter_at = smart_manager_getter; // mutable for the owned native fixture only
unsigned spawn_notes = 0, spawn_notes_dropped = 0; // the spawn probe's lines per late epoch (reset at the zone load)
bool probe_read(std::uintptr_t address, void* out, std::size_t size)
{
	__try { std::memcpy(out, reinterpret_cast<const void*>(address), size); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> T probe_at(std::uintptr_t address) { T v{}; probe_read(address, &v, sizeof v); return v; }
void snapshot(const char* where)
{
	if (!probe_enabled) return;
	__try
	{
		const auto frame = probe_at<std::uint16_t>(native_frame);
		const auto sess = reinterpret_cast<std::uintptr_t>(session());
		const auto clock = reinterpret_cast<std::uintptr_t>(fn<getter_t>(clock_getter)());
		const auto pause = reinterpret_cast<std::uintptr_t>(fn<getter_t>(pause_getter)());
		const auto& d = actor_ai::installed();
		const auto pool = d.primary_base ? std::uintptr_t(d.primary_base) : std::uintptr_t(brain_pool_stock);
		char zone[zone_name_size + 1]{};
		probe_read(zones_static + zones_name_at, zone, zone_name_size);
		// follow-up 5: the dormant-entity manager (count through its vt+0x14, cursor +4) and whether its update is held
		std::int32_t smart_count = -1; std::uint32_t smart_cursor = 0;
		if (auto* manager = static_cast<std::uint8_t*>(fn<getter_t>(smart_manager_getter_at)()))
		{
			smart_cursor = probe_at<std::uint32_t>(reinterpret_cast<std::uintptr_t>(manager) + smart_cursor_at);
			smart_count = fn<std::int32_t(__thiscall*)(void*)>(at<std::uint32_t>(at<void*>(manager, 0), smart_count_slot))(manager);
		}
		logger::write("jip snapshot: where=%s frame=%u session=phases:%08x/%08x/%02x clock=step:%08x%08x now:%08x%08x pause:%08x%08x paused:%u game=t:%08x dt:%08x mode:%u rng=%08x/%u/%08x world=w:%d r:%d f:%d live:%d brains=w:%d r:%d f:%d bits:%08x cam=yaw:%08x mode:%u hist:%08x/%08x flags:%08x pause=%u/%u zone=%s/%02x ai=%08x/%08x/%08x/%08x smart=n:%d cursor:%u held:%u",
			where, unsigned(frame), sess ? probe_at<std::uint32_t>(sess + 0x424) : 0, sess ? probe_at<std::uint32_t>(sess + 0x438) : 0, sess ? probe_at<std::uint8_t>(sess + 0x445) : 0,
			probe_at<std::uint32_t>(clock + 0x34), probe_at<std::uint32_t>(clock + 0x30), probe_at<std::uint32_t>(clock + 0x3c), probe_at<std::uint32_t>(clock + 0x38),
			probe_at<std::uint32_t>(clock + 0x14), probe_at<std::uint32_t>(clock + 0x10), probe_at<std::uint8_t>(clock + 0x20),
			probe_at<std::uint32_t>(game_static + 0x3e8), probe_at<std::uint32_t>(game_static + 0x3ec), probe_at<std::uint8_t>(game_static + 0x268),
			probe_at<std::uint32_t>(rng_seed), probe_at<std::uint32_t>(rng_count), probe_at<std::uint32_t>(0x7ad8b0),
			probe_at<std::int32_t>(world_static + world_ring_write), probe_at<std::int32_t>(world_static + world_ring_read), probe_at<std::int32_t>(world_static + world_ring_free), probe_at<std::int32_t>(world_static + world_live),
			probe_at<std::int32_t>(pool + brain_ring_write), probe_at<std::int32_t>(pool + brain_ring_read), probe_at<std::int32_t>(pool + brain_ring_free), probe_at<std::uint32_t>(pool + brain_ring_bitmap),
			probe_at<std::uint32_t>(camera_object + camera_yaw_at), probe_at<std::uint32_t>(camera_object + camera_mode_at), probe_at<std::uint32_t>(camera_object + camera_yaw_history_at), probe_at<std::uint32_t>(camera_object + camera_yaw_history_at + 4), probe_at<std::uint32_t>(camera_object + camera_flags_at),
			pause ? probe_at<std::uint8_t>(pause + 0x64) : 0, pause ? probe_at<std::uint8_t>(pause + 0x6c) : 0, zone, probe_at<std::uint8_t>(zones_static + zones_loading_at),
			probe_at<std::uint32_t>(0x78a9e4), probe_at<std::uint32_t>(0x78a9f8), probe_at<std::uint32_t>(0x78aa04), probe_at<std::uint32_t>(0x78a9e8),
			smart_count, smart_cursor, unsigned(smart_hold));
		for (int k = 0; k < 4; ++k)
		{
			const std::uint32_t handle = probe_at<std::uint32_t>(game_static + game_party + 4 * k);
			if (!handle) continue;
			auto* actor = static_cast<std::uint8_t*>(fn<entity_of_t>(entity_of_handle)(&handle));
			if (!actor) { logger::write("jip snapshot: where=%s hero[%d]=handle:%08x actor:none", where, k, handle); continue; }
			const auto a = reinterpret_cast<std::uintptr_t>(actor);
			logger::write("jip snapshot: where=%s hero[%d]=handle:%08x own:%08x brain:%08x ctl:%d ai:%08x xyz:%08x,%08x,%08x ang:%08x,%08x,%08x hp:%08x",
				where, k, handle, probe_at<std::uint32_t>(a + 0x1c), probe_at<std::uint32_t>(a + 0x7dc), probe_at<std::int32_t>(a + actor_controller), probe_at<std::uint32_t>(a + hero_ai_flags),
				probe_at<std::uint32_t>(a + 0x20), probe_at<std::uint32_t>(a + 0x24), probe_at<std::uint32_t>(a + 0x28), probe_at<std::uint32_t>(a + 0x2c), probe_at<std::uint32_t>(a + 0x30), probe_at<std::uint32_t>(a + 0x34), probe_at<std::uint32_t>(a + 0x27c));
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { logger::write("jip snapshot: where=%s (read fault)", where); }
}
void __cdecl note_draw(const std::uint32_t* sp)
{
	if (!draws || draws->phase == 0) return; // the probe round: phase 0 (the warm host before its zone load) overflowed the ring
	const auto caller = join_in_progress_core::normalise_draw_caller(sp, draw_wrapper_float_return, draw_wrapper_int_return_a, draw_wrapper_int_return_b);
	draws->note(caller, *reinterpret_cast<const std::uint32_t*>(rng_count) + 1, *reinterpret_cast<const std::uint16_t*>(native_frame));
}
std::uint32_t draw_resume_at = draw_resume; // mutable for the owned native fixture only
// In place of `inc dword ptr [0x7ad8b4]` at 0x560090 (the synchronized draw's entry; entered by JUMP, [esp] = the
// caller's return address): the note, the displaced increment, then the rest of the draw.
__declspec(naked) void draw_trace_bridge()
{
	__asm {
		pushfd
		pushad
		lea eax, [esp+24h]
		push eax
		call note_draw
		add esp, 4
		popad
		popfd
		inc dword ptr ds:[0x7ad8b4]
		jmp dword ptr [draw_resume_at]
	}
}
// Off the draw path (the game thread, at the milestones): the entries since the last flush, one line each.
void flush_draws(const char* where)
{
	if (!draws) return;
	unsigned n = 0;
	while (draws->flushed != draws->head && n++ < 4096)
	{
		const auto& e = draws->ring[draws->flushed++ & (join_in_progress_core::draw_ring_size - 1)];
		logger::write("jip rng: phase=%u frame=%u count=%u caller=%08x", unsigned(e.phase), unsigned(e.frame), e.count, e.caller);
	}
	if (draws->overflow) { logger::write("jip rng: %s - %u draw(s) lost to the ring", where, draws->overflow); draws->overflow = 0; }
}
void probe_milestone(const char* where, std::uint8_t phase)
{
	if (!probe_enabled) return;
	flush_draws(where);
	if (draws) draws->phase = phase;
	snapshot(where);
}
void probe_period(std::uint16_t frame)
{
	if (!probe_enabled || !probe_epoch_late) return;
	char where[32];
	std::snprintf(where, sizeof where, "frame %u", unsigned(frame));
	probe_milestone(where, 4);
}
// ---- the two zone-load bridges (CALL replacements of argument-less cdecl getters; the site's return address stays)
void hold_smart_update(); // follow-up 5, below
void __cdecl zone_load_entry_notice()
{
	if (late_restart && restart.armed) { probe_epoch_late = true; spawn_notes = 0; spawn_notes_dropped = 0; probe_milestone("zone load entry", 1); }
	segment_reseed("the zone load (CMap::LoadMap or CMap::LoadZone)");
	canonical_brain_ring();
	hold_smart_update();
	if (late_restart && restart.armed) snapshot("zone load epoch");
}
void __cdecl zone_start_entry_notice()
{
	if (late_restart && restart.armed) { probe_epoch_late = true; probe_milestone("zone start", 2); }
	segment_reseed("the zone start (before the players are placed)");
	hold_smart_update();
	if (late_restart && restart.armed) snapshot("zone start epoch");
}
// ---- follow-up 3: the camera anchor of the zone-loaded finish ------------------------------------------------------
alignas(16) std::uint32_t camera_anchor_handles[8]{};
bool late_restart_epoch_for_camera = false; // set when the zone start anchored on the party; consumed at frame 0
std::uint32_t camera_anchor_table_at = hero_table; // what the loop at 0x4868f4 walks: retail, or the party copy
void __cdecl select_camera_anchor_table()
{
	camera_anchor_table_at = hero_table;
	if (!join_in_progress_core::camera_anchor_from_party(restart_reset_enabled, late_restart, restart.armed)) return;
	__try
	{
		void* game = fn<getter_t>(game_getter)();
		alignas(16) std::uint32_t list[16]{};
		fn<void(__thiscall*)(void*, void*)>(at<std::uint32_t>(at<void*>(game, 0), game_party_list_slot))(game, list);
		const auto count = list[party_list_count_at / 4];
		std::memset(camera_anchor_handles, 0, sizeof camera_anchor_handles);
		for (std::uint32_t i = 0; i < count && i < 4; ++i) camera_anchor_handles[i] = list[i];
		if (!count || !camera_anchor_handles[0]) { logger::write("join in progress: canonical restart - the party list is empty at the zone start; the camera anchors as retail does"); return; }
		camera_anchor_table_at = reinterpret_cast<std::uint32_t>(camera_anchor_handles);
		late_restart_epoch_for_camera = true;
		logger::write("join in progress: canonical restart - the zone-start camera anchors on party slot 0 (handle %08x of %u) on every peer instead of this PC's controller 0",
			camera_anchor_handles[0], count);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		camera_anchor_table_at = hero_table;
		logger::write_once("jip:anchor-fault", "join in progress: the party list could not be read at the zone start - the camera anchors as retail does");
	}
}
// In place of `mov ebx, 0x70b814` at 0x4868f4 (a CALL replacement of a 5-byte mov; EBX is the loop's table cursor):
// the selection, then EBX = the chosen table. Every other register and the flags are preserved.
__declspec(naked) void camera_anchor_table_bridge()
{
	__asm {
		pushfd
		pushad
		call select_camera_anchor_table
		popad
		popfd
		mov ebx, dword ptr [camera_anchor_table_at]
		ret
	}
}
std::uint32_t zone_load_entry_target_at = zone_load_entry_target, zone_start_entry_target_at = zone_start_entry_target, zone_load_zone_entry_target_at = zone_load_entry_target; // mutable for the fixture only
// In place of `call 0x4457e0` at 0x484070 (CMap::LoadZone, the zone load a mode-2 restore actually takes): same notice.
__declspec(naked) void zone_load_zone_entry_bridge()
{
	__asm {
		pushfd
		pushad
		call zone_load_entry_notice
		popad
		popfd
		jmp dword ptr [zone_load_zone_entry_target_at]
	}
}
// Follow-up 4: at the first shared frame the camera is re-initialised from the party anchor on every peer, so the
// camera updates that ran between the zone start and frame 0 (the probe round: the joiner waited ~1.3 s longer at
// the barrier, yaw history 60.7 vs 67.4 deg) are discarded and both peers start frame 0 from the same camera state.
void reanchor_camera_at_frame0()
{
	if (!restart_reset_enabled || !late_restart_epoch_for_camera) return;
	late_restart_epoch_for_camera = false;
	__try
	{
		const std::uint32_t handle = camera_anchor_handles[0];
		void* actor = handle ? fn<entity_of_t>(entity_of_handle)(&handle) : nullptr;
		if (!actor) { logger::write("join in progress: canonical restart - no party anchor at frame 0; the camera keeps its state"); return; }
		auto* camera = reinterpret_cast<std::uint8_t*>(camera_object);
		if (at<std::uint32_t>(camera, 0) != camera_vtable_value) return;
		const auto yaw = at<std::uint32_t>(camera, camera_yaw_at), hist = at<std::uint32_t>(camera, camera_yaw_history_at);
		fn<void(__thiscall*)(void*, void*)>(at<std::uint32_t>(at<void*>(camera, 0), 0x74))(camera, actor); // the zone-start camera init, again
		logger::write("join in progress: canonical restart - the camera is re-initialised from party slot 0 at frame 0 (was yaw %08x history %08x; now yaw %08x history %08x)",
			yaw, hist, at<std::uint32_t>(camera, camera_yaw_at), at<std::uint32_t>(camera, camera_yaw_history_at));
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { logger::write_once("jip:reanchor-fault", "join in progress: the frame-0 camera re-init faulted - the camera keeps its state"); }
}
__declspec(naked) void zone_load_entry_bridge()
{
	__asm {
		pushfd
		pushad
		call zone_load_entry_notice
		popad
		popfd
		jmp dword ptr [zone_load_entry_target_at]
	}
}
__declspec(naked) void zone_start_entry_bridge()
{
	__asm {
		pushfd
		pushad
		call zone_start_entry_notice
		popad
		popfd
		jmp dword ptr [zone_start_entry_target_at]
	}
}
// ---- follow-up 5: the dormant-entity respawn held until the first shared frame -----------------------------------
// The zone loader puts a visited zone's dormant monsters (CSmartEntMgr records) back before the placement loop; the
// manager's update (vt+4) respawns the ones inside a box around the camera, every game-update tick (0x46f2df), so the
// ticks each peer spends waiting at the barrier before frame 0 - a different real time, with a camera that drifts
// differently - place different entity sets (probe round 2: joiner +4 AI actors before frame 0, +2 at frame 0, never
// matched by the host). While held, the call site gets a stand-in whose update does nothing; the frame-0 epoch
// releases it and puts the real manager's round-robin cursor at 0 on every peer.
__declspec(naked) void smart_update_held() { __asm { ret 4 } } // thiscall update(bool all): nothing
alignas(16) std::uint32_t smart_stand_in_vtable[8]{};
struct smart_stand_in_object { const void* vtable; std::uint32_t cursor; std::uint32_t pad[2]; };
smart_stand_in_object smart_stand_in{};
void* __cdecl smart_stand_in_getter() { ++smart_holds; return &smart_stand_in; }
std::uint32_t smart_getter_at = smart_manager_getter; // what the site reaches: the real getter, or the stand-in's while held
void hold_smart_update()
{
	if (!join_in_progress_core::smart_hold_wanted(restart_reset_enabled, late_restart, restart.armed) || smart_hold) return;
	__try
	{
		if (!smart_stand_in.vtable)
		{
			// Retail's eight slots (guarded bytes) with the update replaced; the site only calls the update.
			std::memcpy(smart_stand_in_vtable, reinterpret_cast<const void*>(smart_manager_vtable), sizeof smart_stand_in_vtable);
			smart_stand_in_vtable[1] = reinterpret_cast<std::uint32_t>(&smart_update_held);
			smart_stand_in.vtable = smart_stand_in_vtable;
		}
		smart_hold = true;
		smart_holds = 0;
		smart_getter_at = reinterpret_cast<std::uint32_t>(&smart_stand_in_getter);
		logger::write("join in progress: canonical restart - the dormant-entity respawn (CSmartEntMgr update) is held from the zone load until the first shared frame on every peer");
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		logger::write_once("jip:smart-hold-fault", "join in progress: the dormant-entity respawn could not be held (vtable unreadable) - retail timing");
	}
}
void release_smart_hold(const char* why)
{
	if (!smart_hold) return;
	smart_hold = false;
	smart_getter_at = smart_manager_getter_at;
	__try
	{
		auto* manager = static_cast<std::uint8_t*>(fn<getter_t>(smart_manager_getter_at)());
		const auto cursor = at<std::uint32_t>(manager, smart_cursor_at);
		const auto count = fn<std::int32_t(__thiscall*)(void*)>(at<std::uint32_t>(at<void*>(manager, 0), smart_count_slot))(manager);
		at<std::uint32_t>(manager, smart_cursor_at) = 0;
		logger::write("join in progress: canonical restart - the dormant-entity respawn resumes at %s (%u update(s) held; %d dormant record(s), cursor %u -> 0 on every peer)",
			why, smart_holds, count, cursor);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		logger::write_once("jip:smart-release-fault", "join in progress: the dormant-entity respawn resumed but the manager could not be read - its cursor stays");
	}
}
// In place of `call 0x4b76f0` at 0x46f2df (the game update's manager getter before its vt+4 update; cdecl, no
// arguments): the real getter, or the stand-in's while held. The site's return address stays on the stack.
__declspec(naked) void smart_update_getter_bridge() { __asm { jmp dword ptr [smart_getter_at] } }
// ---- the probe names every entity the zone spawns (0x4612e0: by definition / instance names) ----------------------
struct spawn_note { std::uint32_t caller; const char* definition; const char* instance; float position[3]; };
spawn_note last_spawn{};
constexpr unsigned spawn_note_limit = 2048;
void __cdecl note_spawn(const std::uint32_t* sp) // sp -> [the caller's return, definition, instance, position*, orientation*, flag]
{
	if (!probe_enabled || !probe_epoch_late) return;
	__try
	{
		last_spawn.caller = sp[0];
		last_spawn.definition = reinterpret_cast<const char*>(sp[1]);
		last_spawn.instance = reinterpret_cast<const char*>(sp[2]);
		const float* p = reinterpret_cast<const float*>(sp[3]);
		for (int i = 0; i < 3; ++i) last_spawn.position[i] = p ? p[i] : 0.0f;
		if (spawn_notes++ >= spawn_note_limit) { ++spawn_notes_dropped; return; }
		char d[48]{}, n[48]{};
		if (last_spawn.definition) std::strncpy(d, last_spawn.definition, sizeof d - 1);
		if (last_spawn.instance) std::strncpy(n, last_spawn.instance, sizeof n - 1);
		logger::write("jip spawn: phase=%u frame=%u n=%u caller=%08x def=%s inst=%s at=%d,%d,%d draws=%u", unsigned(draws ? draws->phase : 0),
			unsigned(probe_at<std::uint16_t>(native_frame)), spawn_notes, last_spawn.caller, d, n, int(last_spawn.position[0]), int(last_spawn.position[1]), int(last_spawn.position[2]),
			probe_at<std::uint32_t>(rng_count));
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { logger::write_once("jip:spawn-note-fault", "jip spawn: a note faulted (unreadable names)"); }
}
std::uint32_t spawn_resume_at = spawn_resume; // mutable for the owned native fixture only
// In place of the first six bytes of 0x4612e0 (entered by JUMP: [esp] = the caller's return address, the five
// arguments above it): the note under pushfd/pushad, then the displaced prologue and the rest of the function.
__declspec(naked) void spawn_entry_bridge()
{
	__asm {
		pushfd
		pushad
		lea eax, [esp+24h]
		push eax
		call note_spawn
		add esp, 4
		popad
		popfd
		push ecx
		push ebx
		push ebp
		push esi
		xor esi, esi
		jmp dword ptr [spawn_resume_at]
	}
}
