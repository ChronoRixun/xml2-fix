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
// p060fix/AI: on the pool the published CAI descriptor names: the stock 32-slot pool (stage 0, and stage 1 = the
// standalone ActorPools=124 install, same layout) or E4's 96-slot pool (stage 2: ring/counters/held words at the E4
// ledger's operands, actor_ai_layout.hpp). The rebuild writes the ring and its counters only; held bits, free/live
// counts and the brain generations (E4's reservation accounting reads the counts) are untouched.
void canonical_brain_ring()
{
	if (!restart_reset_enabled || !late_restart || !restart.armed) return;
	const auto& d = actor_ai::installed();
	const std::uint32_t stage = d.stage, controllers = d.controllers ? d.controllers : brain_slots;
	auto* pool = reinterpret_cast<std::uint8_t*>(d.primary_base ? d.primary_base : brain_pool_stock);
	const auto ring_at = actor_ai::ring(d), write_at = actor_ai::ring_write(d), read_at = actor_ai::ring_read(d), free_at = actor_ai::ring_free(d), held_at = actor_ai::ring_held(d);
	__try
	{
		const auto free_count = at<std::int32_t>(pool, free_at), read = at<std::int32_t>(pool, read_at), write = at<std::int32_t>(pool, write_at);
		const auto bitmap = at<std::uint32_t>(pool, held_at);
		if (!join_in_progress_core::brain_ring_reset_wanted(stage, free_count, controllers))
		{
			logger::write("join in progress: canonical restart - the AI brain free ring is left as it is (stage %u, %u controllers, %d free, read %d write %d, held %08x)",
				stage, controllers, free_count, read, write, bitmap);
			return;
		}
		const auto words = join_in_progress_core::brain_ring_held_words(controllers);
		std::array<std::uint32_t, 3> held_words{};
		for (unsigned w = 0; w < words; ++w) held_words[w] = at<std::uint32_t>(pool, held_at + 4 * w);
		std::array<std::uint32_t, 96> ring{};
		std::int32_t new_write = 0, new_read = 0, new_free = 0;
		const auto held = join_in_progress_core::rebuild_brain_ring(std::span<const std::uint32_t>(held_words.data(), words), std::span<std::uint32_t>(ring.data(), controllers), new_write, new_read, new_free);
		std::memcpy(pool + ring_at, ring.data(), controllers * sizeof(std::uint32_t));
		at<std::int32_t>(pool, write_at) = new_write; at<std::int32_t>(pool, read_at) = new_read; at<std::int32_t>(pool, free_at) = new_free;
		logger::write("join in progress: canonical restart - AI brain free ring rebuilt over the %d free slots in ascending order (stage %u pool %08x, %u controllers; was read %d write %d free %d; held %08x %08x %08x = %u slot(s) kept); brain generations untouched",
			new_free, stage, unsigned(reinterpret_cast<std::uintptr_t>(pool)), controllers, read, write, free_count, held_words[0], held_words[1], held_words[2], held);
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
unsigned touch_lines = 0; constexpr unsigned touch_line_limit = 24; // follow-up 13: `jip touch:` lines per late epoch (reset at the zone load)
std::uint32_t last_attach_parent = 0, last_attach_child = 0; // follow-up 14: the last CActor OnAttached (0x429710) this/child seen by the entry bridge (probe: `parent=` on the touch line)
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
		logger::write("jip snapshot: where=%s frame=%u session=phases:%08x/%08x/%02x clock=step:%08x%08x now:%08x%08x pause:%08x%08x paused:%u game=t:%08x dt:%08x mode:%u rng=%08x/%u/%08x world=w:%d r:%d f:%d live:%d brains=w:%d r:%d f:%d bits:%08x cam=yaw:%08x mode:%u hist:%08x/%08x flags:%08x pause=%u/%u zone=%s/%02x ai=%08x/%08x/%08x/%08x smart=n:%d cursor:%u held:%u fpu=%04x/%08x",
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
			smart_count, smart_cursor, unsigned(smart_hold), unsigned(x87_control_word()), unsigned(_mm_getcsr()));
		// Follow-up 9: the party list through game vt+0x11c (what the anchor and the checksum walk); round 6's lines
		// read a stale table (every hero `actor:none`), so the frame-0 hero positions were never logged.
		alignas(16) std::uint32_t party[16]{}; std::uint32_t party_count = 0;
		if (void* game = fn<getter_t>(game_getter)())
		{
			fn<void(__thiscall*)(void*, void*)>(at<std::uint32_t>(at<void*>(game, 0), game_party_list_slot))(game, party);
			party_count = party[party_list_count_at / 4];
		}
		for (std::uint32_t k = 0; k < 4 && k < party_count; ++k)
		{
			const std::uint32_t handle = party[k];
			if (!handle) continue;
			auto* actor = static_cast<std::uint8_t*>(fn<entity_of_t>(entity_of_handle)(&handle));
			if (!actor) { logger::write("jip snapshot: where=%s hero[%d]=handle:%08x actor:none", where, k, handle); continue; }
			const auto a = reinterpret_cast<std::uintptr_t>(actor);
			// Follow-up 12: the moved-check memory (last-known position/orientation), its pending bit (+0x258) and the two
			// extra deciders of 0x470b30 (+0x23c, +0x240), so the next round names the asymmetry's source.
			// Follow-up 14: `gnd:` = the ground entity +0xbc and the ground height +0x200 (FindGround's results).
			logger::write("jip snapshot: where=%s hero[%d]=handle:%08x own:%08x brain:%08x ctl:%d ai:%08x xyz:%08x,%08x,%08x ang:%08x,%08x,%08x hp:%08x lk:%08x,%08x,%08x/%08x,%08x,%08x mv:%02x/%08x/%08x gnd:%08x/%08x",
				where, k, handle, probe_at<std::uint32_t>(a + 0x1c), probe_at<std::uint32_t>(a + 0x7dc), probe_at<std::int32_t>(a + actor_controller), probe_at<std::uint32_t>(a + hero_ai_flags),
				probe_at<std::uint32_t>(a + 0x20), probe_at<std::uint32_t>(a + 0x24), probe_at<std::uint32_t>(a + 0x28), probe_at<std::uint32_t>(a + 0x2c), probe_at<std::uint32_t>(a + 0x30), probe_at<std::uint32_t>(a + 0x34), probe_at<std::uint32_t>(a + 0x27c),
				probe_at<std::uint32_t>(a + actor_last_position_at), probe_at<std::uint32_t>(a + actor_last_position_at + 4), probe_at<std::uint32_t>(a + actor_last_position_at + 8),
				probe_at<std::uint32_t>(a + actor_last_orientation_at), probe_at<std::uint32_t>(a + actor_last_orientation_at + 4), probe_at<std::uint32_t>(a + actor_last_orientation_at + 8),
				unsigned(probe_at<std::uint8_t>(a + actor_moved_flags_at)), probe_at<std::uint32_t>(a + actor_moved_extra_a), probe_at<std::uint32_t>(a + actor_moved_extra_b),
				probe_at<std::uint32_t>(a + actor_ground_at), probe_at<std::uint32_t>(a + actor_ground_height_at));
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { logger::write("jip snapshot: where=%s (read fault)", where); }
}
void __cdecl note_draw(const std::uint32_t* sp)
{
	if (!draws || draws->phase == 0) return; // the probe round: phase 0 (the warm host before its zone load) overflowed the ring
	const auto caller = join_in_progress_core::normalise_draw_caller(sp, draw_wrapper_float_return, draw_wrapper_int_return_a, draw_wrapper_int_return_b);
	// Follow-up 6: for the two per-actor deciders, the actor's identity (reads guarded; the ring is written either way).
	std::uint32_t a = 0, b = 0, c = 0, d = 0;
	switch (join_in_progress_core::draw_actor_of(caller))
	{
	case join_in_progress_core::draw_actor_register::edi:
		__try
		{
			const auto actor = sp[-9]; a = *reinterpret_cast<const std::uint32_t*>(actor + 0x1c); b = *reinterpret_cast<const std::uint32_t*>(actor + actor_controller);
			// AE7b: ESI = the think state whose +4 is the brain; brain +0x1c = the next think time compared at 0x51e3ab.
			const auto brain = *reinterpret_cast<const std::uint32_t*>(sp[-8] + 4); c = *reinterpret_cast<const std::uint32_t*>(brain + 0x1c);
			const float* p = reinterpret_cast<const float*>(actor + 0x20); const float* e = reinterpret_cast<const float*>(camera_object + 0x104);
			const float dx = p[0] - e[0], dy = p[1] - e[1], dz = p[2] - e[2]; const float dist2 = dx * dx + dy * dy + dz * dz;
			d = std::uint32_t(dist2 > 0.0f ? std::sqrt(dist2) : 0.0f);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { a = b = c = d = 0xffffffffu; }
		break;
	case join_in_progress_core::draw_actor_register::esi:
		__try
		{
			const auto entity = sp[-8]; a = *reinterpret_cast<const std::uint32_t*>(entity + 0x1c); b = (std::uint32_t(std::int32_t(*reinterpret_cast<const float*>(entity + 0x20))) << 16) | (std::uint32_t(std::int32_t(*reinterpret_cast<const float*>(entity + 0x24))) & 0xffffu);
			// Follow-up 13 (round 9 E2): the touch-nudge draws happen on the joiner alone during placement; one line each
			// (bounded) with the touched entity's moved-check state and the return addresses above the draw (every
			// code-looking dword in the next 120 stack slots: the toucher / settle chain, outermost last).
			if (touch_lines < touch_line_limit)
			{
				++touch_lines;
				char line[480]; int n = std::snprintf(line, sizeof line, "jip touch: phase=%u frame=%u count=%u caller=%08x entity=%08x parent=%08x xy=%08x lk=%08x,%08x,%08x mv=%02x/%08x/%08x chain=",
					unsigned(draws->phase), unsigned(*reinterpret_cast<const std::uint16_t*>(native_frame)), *reinterpret_cast<const std::uint32_t*>(rng_count) + 1, caller, a,
					last_attach_child == entity ? probe_at<std::uint32_t>(last_attach_parent + 0x1c) : 0u, b, // follow-up 14: the entity the child stands on (0x429710's this, noted by the entry bridge)
					*reinterpret_cast<const std::uint32_t*>(entity + actor_last_position_at), *reinterpret_cast<const std::uint32_t*>(entity + actor_last_position_at + 4), *reinterpret_cast<const std::uint32_t*>(entity + actor_last_position_at + 8),
					unsigned(*reinterpret_cast<const std::uint8_t*>(entity + actor_moved_flags_at)), *reinterpret_cast<const std::uint32_t*>(entity + actor_moved_extra_a), *reinterpret_cast<const std::uint32_t*>(entity + actor_moved_extra_b));
				unsigned found = 0;
				for (unsigned k = 1; k < 120 && found < 10 && n > 0 && n < int(sizeof line) - 12; ++k)
				{
					const auto v = sp[k];
					if (v >= 0x401000u && v < 0x680000u) { n += std::snprintf(line + n, sizeof line - n, "%s%08x", found ? "," : "", v); ++found; }
				}
				logger::write("%s", line);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { a = b = 0xffffffffu; }
		break;
	default: break;
	}
	draws->note(caller, *reinterpret_cast<const std::uint32_t*>(rng_count) + 1, *reinterpret_cast<const std::uint16_t*>(native_frame), a, b, c, d);
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
		if (e.a || e.b) logger::write("jip rng: phase=%u frame=%u count=%u caller=%08x actor=%08x/%08x think=%08x dist=%u", unsigned(e.phase), unsigned(e.frame), e.count, e.caller, e.a, e.b, e.c, e.d);
		else logger::write("jip rng: phase=%u frame=%u count=%u caller=%08x", unsigned(e.phase), unsigned(e.frame), e.count, e.caller);
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
// Follow-up 6: one compact camera line per shared frame 1..60 after the frame-0 epoch (yaw, history, mode, flags, eye
// +0x104.., target +0xec.., the controller hero table's four handles in this PC's index order), so the next run names
// the first frame where the camera histories part (round 3: equal at frame 0, a few ULPs apart by frame 30).
// AE7b: the 32 brains' next think times (brain +0x1c, the value the think step compares with game time at 0x51e3ab)
// for every held slot of the stock pool (slot i at pool + i * 0x61c; AI brain alloc 0x522e90 / layout actor_ai_layout.hpp),
// and each controller-table hero's brain slot (actor +0x7dc handle & 31), one line per frame and at the frame-0 epoch.
void probe_think(unsigned frame)
{
	if (!probe_enabled) return;
	__try
	{
		const auto& dsc = actor_ai::installed();
		const auto pool = dsc.primary_base ? std::uintptr_t(dsc.primary_base) : std::uintptr_t(brain_pool_stock);
		const auto bits = probe_at<std::uint32_t>(pool + brain_ring_bitmap);
		char line[720]; int n = std::snprintf(line, sizeof line, "jip think: frame=%u t=%08x bits=%08x", frame, probe_at<std::uint32_t>(game_static + 0x3e8), bits);
		for (unsigned i = 0; i < 32 && n > 0 && n < int(sizeof line) - 24; ++i)
			if (bits & (1u << i)) n += std::snprintf(line + n, sizeof line - n, " s%u:%08x", i, probe_at<std::uint32_t>(pool + i * 0x61c + 0x1c));
		for (unsigned k = 0; k < 4 && n > 0 && n < int(sizeof line) - 32; ++k)
		{
			const std::uint32_t handle = probe_at<std::uint32_t>(hero_table + 4 * k);
			auto* actor = handle ? static_cast<std::uint8_t*>(fn<entity_of_t>(entity_of_handle)(&handle)) : nullptr;
			if (!actor) continue;
			const auto brain = probe_at<std::uint32_t>(reinterpret_cast<std::uintptr_t>(actor) + 0x7dc);
			n += std::snprintf(line + n, sizeof line - n, " h%u=%08x:b%08x/s%u/c%d", k, handle, brain, brain & 31u, probe_at<std::int32_t>(reinterpret_cast<std::uintptr_t>(actor) + actor_controller));
		}
		logger::write("%s", line);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { logger::write_once("jip:think-line-fault", "jip think: a line faulted"); }
}
// AE15b (round 10 A/C: a drop within the first period of MOVEMENT of either hero in a late-joined session, term hero_sum
// only - the moving hero's last-bit x/y and a following AI hero's heading by 1-6 ULPs; lk:/mv: equal): one `jip hero:` line
// per party hero per gated frame for 60 frames after the frame-0 epoch, again for 60 frames from the first frame a party
// hero's position changes after that window (`jip move:`), and after a device switch. Each line is the raw dwords of the
// actor ranges the physics steps read (my listings of 0x496f20 / 0x4762c0 / 0x472cf0 touch +0x20, +0x164, +0x238..+0x244,
// +0x259, +0x265, +0x2b4, +0x2f8, +0x30c/d, +0x31a; FindGround +0x200/+0x204/+0x266; 0x473640 +0x120/+0x138; 0x476220 +0x150):
// d20 = +0x20..+0x38 (position, orientation), d120 = +0x120..+0x168 (move target, step time, speed fields), d1e8 =
// +0x1e8..+0x270 (last-known vectors, ground entity/height/normal, the settle fields, flags, surface), d2b0 = +0x2b0..+0x320
// (the physics step's state); plus the game's t/dt. `jip_components.py --mode hero` pairs them by (frame, slot) and names
// the first differing dword offset.
struct hero_span { std::uint32_t at, bytes; };
// AE16: one contiguous +0x1e8..+0x320 (closes the +0x270..+0x2af gap: gravity scale +0x278, last fall +0x28c, gravity hold
// +0x2f0), +0x178 (collision filter), +0x190..+0x1a4 (scripted interpolator), +0x1d0..+0x1e8 (animation root motion), CHero
// +0x7e4..+0x81c (auto-move target/flag/distances read by 0x42dbf0).
constexpr hero_span hero_dump_spans[7] = {{0x20, 0x18}, {0x120, 0x48}, {0x178, 0x04}, {0x190, 0x14}, {0x1d0, 0x18}, {0x1e8, 0x138}, {0x7e4, 0x38}};
unsigned hero_lines = 0; constexpr unsigned hero_line_limit = 60; bool hero_lines_armed = false, hero_move_rearmed = false;
std::uint32_t hero_last_pos[4][3]{}; bool hero_last_valid[4]{};
void hero_lines_arm(const char* why, unsigned frame)
{
	if (!probe_enabled) return;
	hero_lines_armed = true; hero_lines = 0;
	logger::write("jip hero: per-hero field lines armed for %u frames at %s (frame %u)", hero_line_limit, why, frame);
}
void probe_hero_frame(unsigned frame)
{
	if (!probe_enabled || !probe_epoch_late) return;
	// Let movement after a completed window arm its one fresh capture.
	if (hero_lines >= hero_line_limit) hero_lines_armed = false;
	__try
	{
		alignas(16) std::uint32_t party[16]{}; std::uint32_t count = 0;
		if (void* game = fn<getter_t>(game_getter)())
		{
			fn<void(__thiscall*)(void*, void*)>(at<std::uint32_t>(at<void*>(game, 0), game_party_list_slot))(game, party);
			count = party[party_list_count_at / 4];
		}
		for (std::uint32_t k = 0; k < 4 && k < count; ++k)
		{
			const std::uint32_t handle = party[k];
			auto* actor = handle ? static_cast<std::uint8_t*>(fn<entity_of_t>(entity_of_handle)(&handle)) : nullptr;
			if (!actor) { hero_last_valid[k] = false; continue; }
			std::uint32_t pos[3]; probe_read(reinterpret_cast<std::uintptr_t>(actor) + 0x20, pos, sizeof pos);
			const bool moved = hero_last_valid[k] && std::memcmp(pos, hero_last_pos[k], sizeof pos) != 0;
			std::memcpy(hero_last_pos[k], pos, sizeof pos); hero_last_valid[k] = true;
			if (moved && !hero_lines_armed && !hero_move_rearmed)
			{
				hero_move_rearmed = true;
				logger::write("jip move: party slot %u (handle %08x) first moved at frame %u (xyz %08x,%08x,%08x)", k, handle, frame, pos[0], pos[1], pos[2]);
				hero_lines_arm("the first movement", frame);
			}
		}
		if (!hero_lines_armed || hero_lines >= hero_line_limit) return;
		++hero_lines;
		for (std::uint32_t k = 0; k < 4 && k < count; ++k)
		{
			const std::uint32_t handle = party[k];
			auto* actor = handle ? static_cast<std::uint8_t*>(fn<entity_of_t>(entity_of_handle)(&handle)) : nullptr;
			if (!actor) continue;
			const auto a = reinterpret_cast<std::uintptr_t>(actor);
			char line[1500]; int n = std::snprintf(line, sizeof line, "jip hero: frame=%u slot=%u handle=%08x ctl=%d t=%08x dt=%08x", frame, k, handle,
				probe_at<std::int32_t>(a + actor_controller), probe_at<std::uint32_t>(game_static + 0x3e8), probe_at<std::uint32_t>(game_static + 0x3ec));
			for (const auto& sp : hero_dump_spans)
			{
				n += std::snprintf(line + n, sizeof line - n, " d%x=", sp.at);
				for (std::uint32_t off = 0; off < sp.bytes && n > 0 && n < int(sizeof line) - 12; off += 4)
					n += std::snprintf(line + n, sizeof line - n, "%s%08x", off ? "," : "", probe_at<std::uint32_t>(a + sp.at + off));
			}
			logger::write("%s", line);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { logger::write_once("jip:hero-line-fault", "jip hero: a line faulted"); }
}
unsigned cam_lines = 0; constexpr unsigned cam_line_limit = 60;
bool cam_lines_armed = false; // set at the frame-0 epoch
void camera_lines(bool arm) { cam_lines_armed = arm && probe_enabled; cam_lines = 0; }
void probe_frame_camera()
{
	if (!probe_enabled) return;
	{ const auto f = probe_at<std::uint16_t>(native_frame); if (f) probe_hero_frame(unsigned(f)); } // AE15b: every gated frame inside a late epoch
	if (!cam_lines_armed || cam_lines >= cam_line_limit) return;
	__try
	{
		const auto frame = probe_at<std::uint16_t>(native_frame);
		if (frame == 0) return;
		++cam_lines;
		const auto c = std::uintptr_t(camera_object);
		// AE16: the basis vectors the move direction is built from (+0x2cc/+0x2d8/+0x2e4) and the decoded stick floats
		// [input+0xc610..] every peer feeds the controllers from (0x558cc0).
		const auto input = probe_at<std::uintptr_t>(input_object_pointer);
		logger::write("jip cam: frame=%u count=%u t=%08x yaw=%08x hist=%08x/%08x mode=%u flags=%08x eye=%08x,%08x,%08x target=%08x,%08x,%08x basis=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x stick=%08x,%08x,%08x,%08x table=%08x,%08x,%08x,%08x fpu=%04x/%08x",
			unsigned(frame), probe_at<std::uint32_t>(rng_count), probe_at<std::uint32_t>(game_static + 0x3e8), probe_at<std::uint32_t>(c + camera_yaw_at), probe_at<std::uint32_t>(c + camera_yaw_history_at), probe_at<std::uint32_t>(c + camera_yaw_history_at + 4),
			probe_at<std::uint32_t>(c + camera_mode_at), probe_at<std::uint32_t>(c + camera_flags_at),
			probe_at<std::uint32_t>(c + 0x104), probe_at<std::uint32_t>(c + 0x108), probe_at<std::uint32_t>(c + 0x10c), probe_at<std::uint32_t>(c + 0xec), probe_at<std::uint32_t>(c + 0xf0), probe_at<std::uint32_t>(c + 0xf4),
			probe_at<std::uint32_t>(c + 0x2cc), probe_at<std::uint32_t>(c + 0x2d0), probe_at<std::uint32_t>(c + 0x2d4), probe_at<std::uint32_t>(c + 0x2d8), probe_at<std::uint32_t>(c + 0x2dc), probe_at<std::uint32_t>(c + 0x2e0), probe_at<std::uint32_t>(c + 0x2e4), probe_at<std::uint32_t>(c + 0x2e8), probe_at<std::uint32_t>(c + 0x2ec),
			input ? probe_at<std::uint32_t>(input + input_stick_floats_at) : 0u, input ? probe_at<std::uint32_t>(input + input_stick_floats_at + 4) : 0u, input ? probe_at<std::uint32_t>(input + input_stick_floats_at + 8) : 0u, input ? probe_at<std::uint32_t>(input + input_stick_floats_at + 12) : 0u,
			probe_at<std::uint32_t>(hero_table), probe_at<std::uint32_t>(hero_table + 4), probe_at<std::uint32_t>(hero_table + 8), probe_at<std::uint32_t>(hero_table + 12),
			unsigned(x87_control_word()), unsigned(_mm_getcsr()));
		probe_think(unsigned(frame));
		if (frame <= 3) { char where[24]; std::snprintf(where, sizeof where, "frame %u", unsigned(frame)); probe_events(where); }
		if (frame <= 2) probe_camera_dump(unsigned(frame));
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { logger::write_once("jip:cam-line-fault", "jip cam: a line faulted"); }
}
void probe_period(std::uint16_t frame)
{
	if (!probe_enabled || !probe_epoch_late) return;
	char where[32];
	std::snprintf(where, sizeof where, "frame %u", unsigned(frame));
	probe_milestone(where, 4);
	if (frame && frame % 60 == 0) probe_events(where); // follow-up 13: the queue every 60 frames through 900 (round 9 parted at 720/840)
}
// ---- the two zone-load bridges (CALL replacements of argument-less cdecl getters; the site's return address stays)
void hold_smart_update(); // follow-up 5, below
void __cdecl zone_load_entry_notice()
{
	if (late_restart && restart.armed) { probe_epoch_late = true; spawn_notes = 0; spawn_notes_dropped = 0; touch_lines = 0; probe_milestone("zone load entry", 1); }
	segment_reseed("the zone load (CMap::LoadMap or CMap::LoadZone)");
	canonical_brain_ring();
	hold_smart_update();
	if (late_restart && restart.armed) probe_events("zone load before rebuild");
	canonical_event_queue("the zone load");
	if (late_restart && restart.armed) { snapshot("zone load epoch"); probe_events("zone load epoch"); }
}
// Follow-up 10 (live round 7): AE10 resumed an existing member's native controls here (retail 0x550d10 = controllers
// +0x24 := 1) and marked the pump's resume spent; the host then never produced its frame-0 input and both peers froze
// at the barrier for 120 s (the pump's resume after the world restore is what the frame gate needs). The AE10 logs also
// show the four hero transforms bit-identical on both peers at `zone started` (after the controls question is moot), so
// the settle that parts them runs in the barrier-wait ticks, whose count differs per peer; it was never a controller
// asymmetry. Reverted: the controls resume is the pump's, after the world restore, as in AE9.
void __cdecl zone_start_entry_notice()
{
	if (late_restart && restart.armed) { probe_epoch_late = true; probe_milestone("zone start", 2); }
	segment_reseed("the zone start (before the players are placed)");
	// Follow-up 13's cold moved pass used to run here - on an EMPTY party list (round 10: `0 hero(s)` on both peers;
	// the heroes are placed and bound later in the finish). Follow-up 14 runs it from party_bound_notice (0x4868ad).
	hold_smart_update();
	if (late_restart && restart.armed) probe_events("zone start before rebuild");
	canonical_event_queue("the zone start");
	if (late_restart && restart.armed) { snapshot("zone start epoch"); probe_events("zone start epoch"); }
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
// Follow-up 14: the camera's derived spans back to the pristine image (the pure core helper over the two images);
// nothing when there is no pristine image or the singleton's vtable is not the camera's.
unsigned restore_camera_derived(const char* where)
{
	if (!restart.snapshot) { logger::write_once("jip:camera-derived-none", "join in progress: no pristine camera image - the derived camera spans keep this PC's values at %s", where); return 0; }
	auto* camera = reinterpret_cast<std::uint8_t*>(camera_object);
	if (at<std::uint32_t>(camera, 0) != camera_vtable_value) return 0;
	return join_in_progress_core::restore_camera_spans(camera, camera_pristine, camera_derived_spans, camera_bytes);
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
		// Follow-up 14: the init's inputs first (rules camera_derived_spans: eye/target/history/scratch back to the
		// pristine image), so vt+0x74 runs from identical state on every peer; the rest of the object was equal on both
		// peers in every dumped admission.
		const unsigned restored = restore_camera_derived("frame 0");
		fn<void(__thiscall*)(void*, void*)>(at<std::uint32_t>(at<void*>(camera, 0), 0x74))(camera, actor); // the zone-start camera init, again
		logger::write("join in progress: canonical restart - the camera is re-initialised from party slot 0 at frame 0 (was yaw %08x history %08x; %u derived dword(s) back to the pristine image first; now yaw %08x history %08x)",
			yaw, hist, restored, at<std::uint32_t>(camera, camera_yaw_at), at<std::uint32_t>(camera, camera_yaw_history_at));
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
// ---- follow-up 7: the timed-event queue in canonical order, the FPU/SSE control state, the camera image --------
// Round 4 (ae7/evidence/runs): at frames 12-16 the AI brains sit in the SAME slots with the SAME timers on both peers,
// and at frame 16/17 the same three brains re-arm with the same three draws in a different order (the host 462, 478,
// 486; the joiner 486, 462, 478; a different rotation in every run and process). Every actor update is a timed
// event of the manager 0x75d340 (rules: event_manager_*): the heap at body+0x7c40 compares times only, so the order
// of a frame's same-time updates is the heap's shape, which each process's push/pop history decides (a warm host
// carries the old world's entries). At the late restart's epochs (zone load, zone start, frame 0) every peer rebuilds
// the queue in a canonical order: the entries whose owner is a dead entity token are dropped (their fire is a no-op),
// the rest are sorted by (time, owner's dense index, event offset, payload), the record pool is re-initialised by
// the retail pool init and the records and heap pairs are re-added through the retail alloc / store / push, so both
// peers hold byte-identical queues for the same live set.
std::uint32_t event_manager_at = event_manager_object, entity_of_handle_at = entity_of_handle; // mutable for the owned native fixture only
// Follow-up 8: the pump slot (vt+0xc) is accepted when it holds retail 0x4b2d60 or the dispatch hook xml1_turrets
// installed there before this module. The rebuild and the probe never call the pump (alloc / store / push / reads).
bool event_pump_slot_ok()
{
	__try { return join_in_progress_core::event_pump_slot_accepted(at<std::uint32_t>(reinterpret_cast<const void*>(event_manager_pump_slot), 0), event_manager_pump, xml1_turrets::installed_process_hook()); }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
unsigned event_rebuilds = 0, event_last_live = 0, event_last_dead = 0;
std::uint32_t event_index_mask()
{
	std::uint32_t bits = 8;
	__try { bits = entity_capacity::installed().tokens.index_bits; } __except (EXCEPTION_EXECUTE_HANDLER) { bits = 8; }
	if (bits < 1 || bits > 16) bits = 8;
	return (1u << bits) - 1;
}
// Reads the heap pairs and their records; false when the counts are implausible (nothing is touched then).
bool read_event_entries(std::uint8_t* body, std::vector<join_in_progress_core::event_entry>& out, unsigned& dead, unsigned& stale)
{
	const auto heap_count = at<std::uint32_t>(body, event_heap_count_at);
	if (heap_count > event_records) return false;
	out.clear(); out.reserve(heap_count); dead = stale = 0;
	for (std::uint32_t i = 0; i < heap_count; ++i)
	{
		join_in_progress_core::event_entry e{};
		const auto pair = body + event_heap_at + i * 8;
		std::memcpy(&e.time, pair, 4); std::memcpy(&e.index, pair + 4, 4);
		if (e.index >= event_records) return false;
		const auto record = body + e.index * event_record_bytes;
		e.handle = at<std::uint32_t>(record, 0); e.event = at<std::uint32_t>(record, 4);
		std::memcpy(e.payload, record + 8, sizeof e.payload);
		e.token = join_in_progress_core::positive_entity_token(e.handle);
		// Follow-up 14 (S2): a heap-allocated kind object's vtable (guarded read: the object may be gone) keys it.
		if (!join_in_progress_core::event_kind_in_image(e.event)) { std::uint32_t vt = 0; if (probe_read(e.event, &vt, 4)) e.kind_vtable = vt; }
		if (e.token)
		{
			const std::uint32_t handle = e.handle;
			e.owner = reinterpret_cast<std::uint32_t>(fn<entity_of_t>(entity_of_handle_at)(&handle));
			e.live = e.owner != 0;
			// the event's stored time (+4) no longer equals the queued one: a cancelled entry (kept; counted)
			if (e.live && e.event >= e.owner && e.event < e.owner + 0x2000 && at<float>(reinterpret_cast<void*>(e.event), 4) != e.time) ++stale;
		}
		if (join_in_progress_core::event_entry_dropped(e)) ++dead;
		out.push_back(e);
	}
	return true;
}
// Follow-up 9: EVERY entry (round 6's 24-entry head could not show the joiner's extra event at 47 of 47), 16 per
// line (`part=k/n`), each `time:index:handle:kind:payload` where kind is the event's offset inside its owner or the
// static kind object's address (process-independent: round 6's 4th field was `event - owner`, a per-process number
// for every static kind, so every pair DIFFERED), and the record's 16 payload bytes.
constexpr unsigned probe_event_limit = 256, probe_events_per_line = 16;
void probe_events_body(const char* where)
{
	{
		auto* object = reinterpret_cast<std::uint8_t*>(event_manager_at);
		if (at<std::uint32_t>(object, 0) != event_manager_vtable) { logger::write("jip events: where=%s (unexpected manager vtable %08x)", where, at<std::uint32_t>(object, 0)); return; }
		auto* body = object + event_body_at;
		std::vector<join_in_progress_core::event_entry> entries; unsigned dead = 0, stale = 0;
		const bool ok = read_event_entries(body, entries, dead, stale);
		const unsigned total = ok ? unsigned(entries.size()) : 0, shown = total < probe_event_limit ? total : probe_event_limit;
		const unsigned parts = shown ? (shown + probe_events_per_line - 1) / probe_events_per_line : 1;
		for (unsigned part = 0; part < parts; ++part)
		{
			char line[1500]; int n = std::snprintf(line, sizeof line, "jip events: where=%s heap=%u records=%u ring=r:%u/w:%u/n:%u live=%u dead=%u stale=%u rebuilds=%u part=%u/%u head=",
				where, at<std::uint32_t>(body, event_heap_count_at), at<std::uint32_t>(body, event_record_count_at), at<std::uint32_t>(body, event_ring_read_at), at<std::uint32_t>(body, event_ring_write_at), at<std::uint32_t>(body, event_ring_count_at),
				ok ? unsigned(entries.size()) - dead : 0, dead, stale, event_rebuilds, part + 1, parts);
			const unsigned first = part * probe_events_per_line, last = (first + probe_events_per_line < shown) ? first + probe_events_per_line : shown;
			for (unsigned i = first; i < last && n > 0 && n < int(sizeof line) - 80; ++i)
			{
				const auto& e = entries[i]; std::uint32_t t; std::memcpy(&t, &e.time, 4);
				char payload[33]; for (unsigned k = 0; k < 16; ++k) std::snprintf(payload + 2 * k, 3, "%02x", e.payload[k]);
				n += std::snprintf(line + n, sizeof line - n, "%s%08x:%u:%08x:%x:%s", i > first ? "," : "", t, e.index, e.handle, join_in_progress_core::event_kind_key(e), payload);
			}
			logger::write("%s", line);
		}
	}
}
// The bodies hold std::vector (C2712: no __try beside objects that unwind); the wrappers guard them.
void probe_events(const char* where)
{
	if (!probe_enabled) return;
	__try { probe_events_body(where); }
	__except (EXCEPTION_EXECUTE_HANDLER) { logger::write_once("jip:events-line-fault", "jip events: a line faulted"); }
}
// Follow-up 12: the party handles whose queued "moved" events the NEXT rebuild drops (set by the frame-0 canonicalisation).
std::array<std::uint32_t, 4> moved_drop_handles{}; unsigned moved_drop_count = 0; unsigned moved_dropped_last = 0;
void canonical_event_queue_body(const char* where)
{
	{
		auto* object = reinterpret_cast<std::uint8_t*>(event_manager_at);
		if (at<std::uint32_t>(object, 0) != event_manager_vtable)
		{
			logger::write_once("jip:events-vtable", "join in progress: the timed-event manager has an unexpected vtable %08x - its queue is left as it is", at<std::uint32_t>(object, 0));
			return;
		}
		auto* body = object + event_body_at;
		const auto heap_count = at<std::uint32_t>(body, event_heap_count_at), records = at<std::uint32_t>(body, event_record_count_at);
		const auto ring_read = at<std::uint32_t>(body, event_ring_read_at), ring_write = at<std::uint32_t>(body, event_ring_write_at), ring_count = at<std::uint32_t>(body, event_ring_count_at);
		std::vector<join_in_progress_core::event_entry> entries; unsigned dead = 0, stale = 0;
		if (!read_event_entries(body, entries, dead, stale))
		{
			logger::write_once("jip:events-implausible", "join in progress: the timed-event queue is implausible (heap %u, records %u) - left as it is", heap_count, records);
			return;
		}
		const auto mask = event_index_mask();
		entries.erase(std::remove_if(entries.begin(), entries.end(), join_in_progress_core::event_entry_dropped), entries.end());
		// Follow-up 12: the party heroes' queued moved events go with their canonical moved-check state (frame 0 only).
		moved_dropped_last = 0;
		if (moved_drop_count)
		{
			entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const auto& e) {
				const bool d = join_in_progress_core::moved_event_dropped(e, moved_event_kind, moved_drop_handles.data(), moved_drop_count, mask);
				moved_dropped_last += d ? 1u : 0u; return d; }), entries.end());
			moved_drop_count = 0;
		}
		// Follow-up 14 (S2): a kept entry whose kind object lies outside the image is keyed by its vtable; say so once.
		for (const auto& e : entries)
			if (e.owner && e.event - e.owner >= join_in_progress_core::event_member_span && !join_in_progress_core::event_kind_in_image(e.event))
			{
				logger::write_once("jip:events-heap-kind", "join in progress: the timed-event queue keeps an entry whose kind object is outside the image (event %08x, vtable %08x, owner index %u) - ordered by its vtable", e.event, e.kind_vtable, e.handle & mask);
				break;
			}
		std::stable_sort(entries.begin(), entries.end(), [mask](const auto& a, const auto& b) { return join_in_progress_core::canonical_event_less(a, b, mask); });
		// The pool back to its constructed state (retail pool init: the free ring 0..1124 ascending, read 0), both
		// bitmaps and both counts cleared, then every kept entry re-added through the retail alloc / store / heap push.
		fn<void(__thiscall*)(void*)>(event_pool_init)(body);
		std::memset(body + event_used_bitmap_at, 0, 0x24 * 4);
		std::memset(body + event_alloc_bitmap_at, 0, 0x24 * 4);
		at<std::uint32_t>(body, event_record_count_at) = 0;
		at<std::uint32_t>(body, event_heap_count_at) = 0;
		for (const auto& e : entries)
		{
			const auto index = fn<std::uint32_t(__thiscall*)(void*)>(event_record_alloc)(body);
			alignas(4) std::uint8_t record[event_record_bytes];
			std::memcpy(record, &e.handle, 4); std::memcpy(record + 4, &e.event, 4); std::memcpy(record + 8, e.payload, sizeof e.payload);
			fn<void(__thiscall*)(void*, std::uint32_t, const void*)>(event_record_store)(body, index, record);
			struct { float time; std::uint32_t index; } pair{e.time, index};
			fn<void(__thiscall*)(void*, const void*)>(event_heap_push)(body + event_heap_at, &pair);
		}
		++event_rebuilds; event_last_live = unsigned(entries.size()); event_last_dead = dead;
		logger::write("join in progress: canonical restart - the timed-event queue (every actor update) is rebuilt in canonical order at %s on every peer: %u entries kept (%u whose owner is gone dropped, %u cancelled kept, %u party-hero moved event(s) dropped), heap %u -> %u, records %u -> %u, free ring r:%u/w:%u/n:%u -> r:%u/w:%u/n:%u, index mask %03x (rebuild %u)",
			where, unsigned(entries.size()), dead, stale, moved_dropped_last, heap_count, at<std::uint32_t>(body, event_heap_count_at), records, at<std::uint32_t>(body, event_record_count_at),
			ring_read, ring_write, ring_count, at<std::uint32_t>(body, event_ring_read_at), at<std::uint32_t>(body, event_ring_write_at), at<std::uint32_t>(body, event_ring_count_at), mask, event_rebuilds);
	}
}
void canonical_event_queue(const char* where, bool wanted)
{
	if (!wanted) return;
	__try { canonical_event_queue_body(where); }
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		logger::write_once("jip:events-rebuild-fault", "join in progress: the timed-event queue rebuild faulted - the queue keeps its order");
	}
}
void canonical_event_queue(const char* where) { canonical_event_queue(where, join_in_progress_core::event_queue_rebuild_wanted(restart_reset_enabled, late_restart, restart.armed)); }
// The game thread's x87 control word, for the probe's `fpu=` field only (follow-up 14 / review S4: the AE8 "correction"
// cleared MXCSR bit 5, the sticky precision FLAG - no effect on any result - and is gone).
std::uint16_t x87_control_word() { std::uint16_t cw = 0; __asm { fnstcw cw } return cw; }
// The whole camera object as dwords (the frame-0 epoch, frames 1 and 2) so the next pair of logs names the first
// field that parts (round 4: equal yaw/history/eye/target at frame 0, 22 ULPs apart in eye/target y at frame 1).
void probe_camera_dump(unsigned frame)
{
	if (!probe_enabled) return;
	__try
	{
		const auto c = std::uintptr_t(camera_object);
		for (std::uint32_t off = 0; off < camera_bytes; off += 128)
		{
			char line[420]; int n = std::snprintf(line, sizeof line, "jip camdump: frame=%u off=%03x", frame, off);
			for (std::uint32_t k = 0; k < 32 && off + k * 4 + 4 <= camera_bytes && n > 0 && n < int(sizeof line) - 12; ++k)
				n += std::snprintf(line + n, sizeof line - n, " %08x", probe_at<std::uint32_t>(c + off + k * 4));
			logger::write("%s", line);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { logger::write_once("jip:camdump-fault", "jip camdump: a line faulted"); }
}
// ---- follow-up 14 (AE15): the party heroes' ground attach and FindGround canonical; the cold moved pass after the binding
// (rules: attach_override / find_ground / party_bound_*; O:/Projects/agent-work/e2-settle-re/RESULT.md). The AE12 world
// hold that stood here is gone: rounds 8-10 logged `0 world update(s) skipped` on every peer in every admission.
unsigned attach_suppressions = 0, attach_calls_during_probe = 0; constexpr unsigned attach_log_limit = 8;
bool ground_probe_running = false;
std::uint32_t find_ground_at = find_ground, attach_override_resume_at = attach_override_resume; // mutable for the owned native fixture only
// Decides for one CActor OnAttached(child) call (ecx = the entity stood on, [esp+4] = the child): 1 = answer `ret 4`
// (a party hero during an armed late restart: no settle nudge, no draws; the caller 0x419f20 still stores +0xbc),
// 0 = run the override. Reads the party list through game vt+0x11c only while the suppression is wanted.
std::uint32_t __cdecl attach_decide(std::uint32_t child, std::uint32_t parent)
{
	last_attach_parent = parent; last_attach_child = child;
	if (!join_in_progress_core::attach_suppressed(restart_reset_enabled, late_restart, restart.armed) && !ground_probe_running) return 0;
	__try
	{
		alignas(16) std::uint32_t party[16]{};
		void* game = fn<getter_t>(game_getter)();
		fn<void(__thiscall*)(void*, void*)>(at<std::uint32_t>(at<void*>(game, 0), game_party_list_slot))(game, party);
		const std::uint32_t count = party[party_list_count_at / 4];
		if (!child || !join_in_progress_core::party_member(at<std::uint32_t>(reinterpret_cast<void*>(child), 0x1c), party, count < 4 ? count : 4, event_index_mask())) return 0;
		if (ground_probe_running) { ++attach_calls_during_probe; return 0; } // the frame-0 re-probe: every peer runs it alike
		if (++attach_suppressions <= attach_log_limit)
			logger::write("join in progress: canonical restart - a party hero's ground attach callback (CActor OnAttached 0x429710: the settle nudge and its three draws) is suppressed before the first shared frame on this PC (hero %08x on entity %08x; suppression %u)",
				at<std::uint32_t>(reinterpret_cast<void*>(child), 0x1c), parent ? at<std::uint32_t>(reinterpret_cast<void*>(parent), 0x1c) : 0u, attach_suppressions);
		return 1;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// In place of the first eight bytes of 0x429710 (entered by JUMP: [esp] = the caller's return, [esp+4] = the child;
// ecx = this). Suppressed: `ret 4` with every other register as the caller left it. Otherwise the displaced prologue
// (`sub esp,0x18; push esi; mov esi,[esp+0x20]`: position-independent) and the rest of the override.
__declspec(naked) void attach_override_bridge()
{
	__asm {
		pushfd
		pushad
		push ecx
		push dword ptr [esp+2Ch]
		call attach_decide
		add esp, 8
		test eax, eax
		jnz suppressed
		popad
		popfd
		sub esp, 18h
		push esi
		mov esi, dword ptr [esp+20h]
		jmp dword ptr [attach_override_resume_at]
	suppressed:
		popad
		popfd
		ret 4
	}
}
// At the frame-0 epoch (after the moved-state pass, before the queue rebuild): every party hero's ground entity back
// to 0 and FindGround run in party order on every peer - the same ray from the same position, the same +0xbc /
// +0x200 / +0x204 / +0x266, and the same attach callback (nudge draws) if an actor is below.
unsigned ground_probes = 0;
void canonical_party_ground(const char* where, bool wanted)
{
	if (!wanted) return;
	__try
	{
		alignas(16) std::uint32_t party[16]{};
		void* game = fn<getter_t>(game_getter)();
		fn<void(__thiscall*)(void*, void*)>(at<std::uint32_t>(at<void*>(game, 0), game_party_list_slot))(game, party);
		const std::uint32_t count = party[party_list_count_at / 4];
		unsigned heroes = 0, hits = 0, were_set = 0;
		attach_calls_during_probe = 0; ground_probe_running = true;
		for (std::uint32_t k = 0; k < 4 && k < count; ++k)
		{
			const std::uint32_t handle = party[k];
			if (!handle) continue;
			auto* actor = static_cast<std::uint8_t*>(fn<entity_of_t>(entity_of_handle_at)(&handle));
			if (!actor) continue;
			++heroes;
			if (at<std::uint32_t>(actor, actor_ground_at)) ++were_set;
			at<std::uint32_t>(actor, actor_ground_at) = 0;
			if (fn<char(__thiscall*)(void*)>(find_ground_at)(actor)) ++hits;
		}
		ground_probe_running = false;
		++ground_probes;
		logger::write("join in progress: canonical restart - the party heroes' ground is re-probed at %s on every peer: %u hero(s), %u had a ground entity before, %u ray hit(s), %u attach callback(s) (probe %u; %u callback(s) suppressed earlier in this restart)",
			where, heroes, were_set, hits, attach_calls_during_probe, ground_probes, attach_suppressions);
		attach_suppressions = 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		ground_probe_running = false;
		logger::write_once("jip:ground-probe-fault", "join in progress: the party heroes' ground re-probe faulted - their ground stays as each PC left it");
	}
}
// AE16: the party heroes' motion fields canonical at the frame-0 epoch (after the ground re-probe, before the queue
// rebuild) on every peer: gravity hold +0x2f0 := 0 (expired), last fall +0x28c := t, last update +0x150 := t (the first
// step after frame 0 then sees dt = 0 on every peer alike), the velocity and angular pairs := 0 (they are zero at idle on
// both peers anyway). CHero +0x808 (auto-move flag) is read into the line only.
unsigned motion_canonical_runs = 0;
void canonical_party_motion(const char* where, bool wanted)
{
	if (!wanted) return;
	__try
	{
		alignas(16) std::uint32_t party[16]{};
		void* game = fn<getter_t>(game_getter)();
		fn<void(__thiscall*)(void*, void*)>(at<std::uint32_t>(at<void*>(game, 0), game_party_list_slot))(game, party);
		const std::uint32_t count = party[party_list_count_at / 4];
		const std::uint32_t t = at<std::uint32_t>(reinterpret_cast<const void*>(game_static), game_time_at);
		unsigned heroes = 0, holds = 0, falls = 0, updates = 0, velocities = 0, auto_moves = 0;
		char detail[200]{}; int n = 0;
		for (std::uint32_t k = 0; k < 4 && k < count; ++k)
		{
			const std::uint32_t handle = party[k];
			if (!handle) continue;
			auto* actor = static_cast<std::uint8_t*>(fn<entity_of_t>(entity_of_handle_at)(&handle));
			if (!actor) continue;
			++heroes;
			if (at<std::uint32_t>(actor, actor_gravity_hold_at)) ++holds;
			if (at<std::uint32_t>(actor, actor_last_fall_at) != t) ++falls;
			if (at<std::uint32_t>(actor, actor_last_update_at) != t) ++updates;
			bool moving = false;
			for (const auto off : {actor_velocity_at, actor_angular_at, actor_velocity_prev_at, actor_angular_prev_at})
				for (std::uint32_t i = 0; i < actor_vector_bytes; i += 4) moving |= at<std::uint32_t>(actor, off + i) != 0;
			if (moving) ++velocities;
			const auto auto_move = at<std::uint32_t>(actor, hero_auto_move_flag_at);
			if (auto_move) ++auto_moves;
			if (n >= 0 && n < int(sizeof detail) - 40) n += std::snprintf(detail + n, sizeof detail - n, "%s%u:%08x/%08x/%08x/%08x", n ? " " : "", k, at<std::uint32_t>(actor, actor_gravity_hold_at), at<std::uint32_t>(actor, actor_last_fall_at), at<std::uint32_t>(actor, actor_last_update_at), auto_move);
			at<std::uint32_t>(actor, actor_gravity_hold_at) = 0;
			at<std::uint32_t>(actor, actor_last_fall_at) = t;
			at<std::uint32_t>(actor, actor_last_update_at) = t;
			for (const auto off : {actor_velocity_at, actor_angular_at, actor_velocity_prev_at, actor_angular_prev_at}) std::memset(actor + off, 0, actor_vector_bytes);
		}
		++motion_canonical_runs;
		logger::write("join in progress: canonical restart - the party heroes' motion fields are canonical at %s on every peer (t %08x): %u hero(s); gravity hold cleared on %u, last fall set on %u, last update set on %u, velocity zeroed on %u, auto-move flag set on %u (not written); before (slot:hold/fall/update/auto) %s (run %u)",
			where, t, heroes, holds, falls, updates, velocities, auto_moves, detail, motion_canonical_runs);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		logger::write_once("jip:motion-canonical-fault", "join in progress: the party heroes' motion fields could not be made canonical (read fault) - left as they are");
	}
}
// In place of `call 0x495350` at 0x4868ad (the zone-loaded finish, right after 0x422520 bound the party; an
// argument-less cdecl getter whose result the site reads): the cold moved pass on the party that now exists, then the
// real getter (the site's return address stays).
std::uint32_t party_bound_target_at = party_bound_target; // mutable for the owned native fixture only
void __cdecl party_bound_notice()
{
	canonical_party_moved_state("the zone start (after the party is bound, before the first physics step)", restart_reset_enabled && moved_canonical_enabled && late_restart && restart.armed);
}
__declspec(naked) void party_bound_bridge()
{
	__asm {
		pushfd
		pushad
		call party_bound_notice
		popad
		popfd
		jmp dword ptr [party_bound_target_at]
	}
}
// ---- AE15: the probe re-arms at a prompt device change ------------------------------------------------------------
// Round 10: every late drop (rounds 9-10, frames 720..4200, deltas 1..8) came 1.2-1.7 s after the JOINER logged
// `prompts: player 1's prompts show the pad` - its first pad input device - before any stick deflection, while a
// lobby START survives the same switch. The retail input record (0x558ec0: four scaled axis bytes, the buttons, a
// frame time; no device flag) carries nothing about the device, so the switch's effect is local to that PC. The
// probe's windows had all ended by then; on a change (pad_prompts' counter, polled from the pump) the switching PC
// re-arms the checksum window (same epoch number: the tool still pairs with the other peer, whose every-period
// sample lines run throughout), flushes and restarts the draw ring in phase 5, dumps the snapshot / queue / camera
// and the 60 camera lines again, so the next pair of logs names the term and the first differing draw.
unsigned device_switches_seen = 0;
void device_switch_probe()
{
	const auto n = pad_prompts::device_switches();
	if (n == device_switches_seen) return;
	device_switches_seen = n;
	if (!probe_enabled || !machine.started) return;
	__try
	{
		const auto frame = probe_at<std::uint16_t>(native_frame);
		logger::write("jip switch: a player's prompts changed device (switch %u) at native frame %u, draw count %u - the checksum window (%u periods), the draw ring (phase 5), the queue/camera dumps and the camera lines are re-armed on this PC",
			n, unsigned(frame), probe_at<std::uint32_t>(rng_count), checksum_period_budget);
		rearm_checksums(checksum_period_budget, "a prompt device change");
		probe_epoch_late = true;
		if (draws) { flush_draws("device switch"); draws->phase = 5; }
		snapshot("device switch");
		probe_events("device switch");
		probe_camera_dump(unsigned(frame));
		camera_lines(true);
		hero_lines_arm("a prompt device change", unsigned(frame));
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { logger::write_once("jip:switch-probe-fault", "jip switch: the device-switch probe faulted"); }
}
// ---- follow-up 12: the party heroes' moved-check state canonical at the first shared frame ---------------------
// (rules: moved_* / actor_*). Round 8: the joiner alone held a queued "moved" event for its own hero at frame 0 (the
// rebuild kept it: 46/47) and the host alone registered them for the AI heroes at frames 1-3, with every hero's
// transform identical on both peers. The source of the joiner's registration is not traced (the moved check is
// vt+0xf4, a slot ~80 call sites share across classes). At the frame-0 epoch, on every peer and before the queue
// rebuild, every party hero gets the state of an entity that has not moved since it was placed: last-known position
// +0x1e8 = +0x20, last-known orientation +0x1f4 = +0x2c, pending bit 2 of +0x258 clear; the rebuild then drops the
// party heroes' queued moved events. From frame 1 on, a hero that moves registers alike on every peer.
bool moved_canonical_enabled = true; // [Dev] LateJoinMovedCanonical=0: the A/B control
unsigned moved_canonical_runs = 0;
void canonical_party_moved_state(const char* where, bool wanted)
{
	moved_drop_count = 0;
	if (!wanted) return;
	__try
	{
		alignas(16) std::uint32_t party[16]{};
		void* game = fn<getter_t>(game_getter)();
		fn<void(__thiscall*)(void*, void*)>(at<std::uint32_t>(at<void*>(game, 0), game_party_list_slot))(game, party);
		const std::uint32_t count = party[party_list_count_at / 4];
		unsigned heroes = 0, pending = 0, differed = 0;
		for (std::uint32_t k = 0; k < 4 && k < count; ++k)
		{
			const std::uint32_t handle = party[k];
			if (!handle) continue;
			auto* actor = static_cast<std::uint8_t*>(fn<entity_of_t>(entity_of_handle_at)(&handle));
			if (!actor) continue;
			moved_drop_handles[moved_drop_count++] = handle;
			++heroes;
			if (std::memcmp(actor + actor_last_position_at, actor + actor_position_at, actor_vector_bytes) != 0 || std::memcmp(actor + actor_last_orientation_at, actor + actor_orientation_at, actor_vector_bytes) != 0) ++differed;
			std::memcpy(actor + actor_last_position_at, actor + actor_position_at, actor_vector_bytes);
			std::memcpy(actor + actor_last_orientation_at, actor + actor_orientation_at, actor_vector_bytes);
			if (at<std::uint8_t>(actor, actor_moved_flags_at) & actor_moved_pending_bit) { ++pending; at<std::uint8_t>(actor, actor_moved_flags_at) &= std::uint8_t(~actor_moved_pending_bit); }
		}
		++moved_canonical_runs;
		logger::write("join in progress: canonical restart - the party heroes' moved-check state is canonical at %s on every peer: %u hero(s), last-known = current (%u differed), %u pending flag(s) cleared; their queued moved events go in the rebuild (run %u)",
			where, heroes, differed, pending, moved_canonical_runs);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		moved_drop_count = 0;
		logger::write_once("jip:moved-canonical-fault", "join in progress: the party heroes' moved-check state could not be made canonical (read fault) - left as it is");
	}
}
