// ---- p060fix/AQ3: shared admission hold ([Online] JoinHoldSeconds, default 10, 0..15, 0 = off) -------------------
// Before the native add/load at 0x484def, the host sends a reliable GNS boundary decision (native frame + newcomer
// seat mask). Every existing guest waits for the same-frame decision at its own area boundary, servicing GNS only:
// gameplay packets queue without native dispatch or a world step. This is necessary because native 0x1f is sent
// only AFTER the load (live ZJ-2a: queued 16.064, sent 17.505, guest received 17.567 after its load ended 17.525).
// Positive decisions arm the same world-section hold on all old peers before loading. Initial/restart loads never
// wait; ordinary decisions skip no world section. The cap/message open at the first pump after loading. The world
// gate 0x46f0a9 is skipped and 0x402345 retains the loader's clock pause until restart, cap, gone, or session end.
// A native stalled guest renders only with a nonnull message at 0x4029f0: hold_stall_message selects that render
// path, and the existing Present overlay draws the panel. No extra render loop or game handler runs in the boundary
// wait. The newcomer never holds. A late 0x47 cannot renew an admission hold after its cap. Missing decisions and
// member armed holds are bounded by cap + 4000 ms; host armed loads retain their 30000 ms safety bound.
volatile std::uint8_t world_hold = 0;   // read by the bridge on the game thread; written on the game thread only
unsigned world_hold_skips = 0;          // game updates whose world section was skipped on this PC in the current hold
join_in_progress_core::join_hold hold;  // [Online] JoinHoldSeconds (configure at install)
DWORD (WINAPI* hold_clock)() = &GetTickCount; // mutable for the owned native fixture only
const char* volatile hold_message = nullptr; // the text the overlay draws while a hold is open (null: nothing)
bool hold_message_logged = false, hold_message_failed = false; // per hold: `message shown` / the draw failure, once
bool (*hold_zone_loading)() = &zone_load_pending; // mutable for the owned native fixture only
int mesh_newcomers = 0;                 // a member: participants that completed the mesh with this PC since its last restart
struct hold_point { std::uint16_t frame = 0; std::uint32_t draws = 0, game_time = 0; bool valid = false; };
hold_point hold_armed_at{}, hold_opened{}, zone_loaded_at{};
unsigned hold_skips_armed = 0;          // world updates skipped between the admission and the zone-load end (host)
bool zone_was_loading = false;          // this PC's zone-load edge (every role), for the measurement
bool hold_playing = false, hold_member_notified = false;
unsigned hold_clock_skips = 0, hold_armed_limit = join_in_progress_core::join_hold_armed_limit_ms;

// AQ3: LoadZone/LoadMap pause the clock (55b3d0, vt+1c). 402345..402350 unpauses it (55b410, vt+20), BEFORE
// 46f079 samples effective time into game+3e8. Retain the load pause while held: raw ticks (55b240, vt+14) and
// real/network time stay live, but 55b590 (vt+34) reads the saved effective time. Retail unpause later compensates
// the raw ticks accrued while paused; there is no time jump on release.
void __cdecl hold_game_clock()
{
	if (world_hold) { ++hold_clock_skips; return; }
	void* clock = fn<getter_t>(clock_getter)();
	fn<init_t>(at<std::uint32_t>(at<void*>(clock, 0), 0x20))(clock);
}
// AQ3: 0x402920's stalled loop calls this at 0x4029f0. A null result skips ALL rendering via 0x402b50, explaining
// the guest's zero Presents. A held message selects retail's existing render path; Present draws the fix's panel.
const char* __fastcall hold_stall_message(void* scheduler, void*)
{
	if (const char* text = hold_message) return text;
	return fn<const char*(__thiscall*)(void*)>(native_stall_message)(scheduler);
}

// In place of `test byte ptr [esi+0x728], 2` at 0x46f0a9 (7 bytes: call rel32 + 2 nops; the site's `je 0x46f60d`
// follows and reads ZF only - the instruction after the je is a call, so CF/OF/SF/PF/AF are dead), entered by CALL
// with ESI = the game object exactly as at the site. Hold off: retail's own instruction, every flag as it leaves it.
// Hold on: the flags of that test with the bit clear (ZF=1), no register changed, the skip counted.
__declspec(naked) void world_gate_bridge()
{
	__asm {
		cmp byte ptr [world_hold], 0
		jne held
		test byte ptr [esi+728h], 2
		ret
	held:
		inc dword ptr [world_hold_skips]
		test esp, 0
		ret
	}
}

hold_point hold_now()
{
	hold_point p;
	p.frame = probe_at<std::uint16_t>(native_frame);
	p.draws = probe_at<std::uint32_t>(rng_count);
	p.game_time = probe_at<std::uint32_t>(game_static + game_time_at);
	p.valid = true;
	return p;
}
const char* hold_role_name(join_in_progress_core::join_hold::role r)
{
	return r == join_in_progress_core::join_hold::role::host ? "host" : "existing participant";
}
// The overlay's text follows the hold's newcomer count (the draw reads it at the next Present).
void hold_show()
{
	if (!hold.active() || hold.armed) return; // an armed hold is inside the zone load: the load screen shows
	const char* text = join_in_progress_core::join_hold_text(hold.newcomers);
	if (hold_message != text) hold_message = text;
}
void hold_unshow()
{
	if (!hold_message) return;
	hold_message = nullptr;
	if (hold_message_logged) logger::write("join hold: message removed (the next frame is drawn without it)");
}
// display::set_overlay: before every Present, on the game's render thread.
void __cdecl hold_overlay_draw(void* device)
{
	const char* text = hold_message;
	if (!text) return;
	const auto o = hold_overlay::draw(device, text);
	if (o.drawn != hold_overlay::path::none)
	{
		if (!hold_message_logged)
		{
			hold_message_logged = true;
			logger::write("join hold: message shown (\"%s\", %s path, panel %ldx%ld at %ld,%ld)", text,
				o.drawn == hold_overlay::path::blend ? "blend" : "clear", o.where.right - o.where.left, o.where.bottom - o.where.top, o.where.left, o.where.top);
		}
	}
	else if (o.step && !hold_message_failed)
	{
		hold_message_failed = true;
		logger::write("join hold: the on-screen message could not be drawn (%s: %08lx) - the hold itself goes on", o.step, static_cast<unsigned long>(o.result));
	}
}
// "since this PC's zone load ended: ..." (the measurement of the pre-hold window, on every role).
void hold_since_zone_load(char* out, std::size_t size, const hold_point& now_point)
{
	if (!zone_loaded_at.valid) { std::snprintf(out, size, "no zone-load end seen on this PC yet"); return; }
	std::snprintf(out, size, "since this PC's zone load ended: frame %u -> %u, synchronized draws %u -> %u (%+d), game time %08x -> %08x",
		unsigned(zone_loaded_at.frame), unsigned(now_point.frame), zone_loaded_at.draws, now_point.draws, int(now_point.draws - zone_loaded_at.draws),
		zone_loaded_at.game_time, now_point.game_time);
}
void hold_started(const char* where)
{
	hold_opened = hold_now();
	hold_message_logged = hold_message_failed = false;
	char since[200];
	hold_since_zone_load(since, sizeof since, hold_opened);
	logger::write("join hold: the game simulation is held on this PC (%s) from %s: %d newcomer(s), cap %u ms ([Online] JoinHoldSeconds=%u); frame %u, synchronized draws %u, game time %08x (%s); the window keeps running and shows \"%s\" (hold %u)",
		hold_role_name(hold.who), where, hold.newcomers, hold.cap_ms, hold.cap_ms / 1000u, unsigned(hold_opened.frame), hold_opened.draws,
		hold_opened.game_time, since, join_in_progress_core::join_hold_text(hold.newcomers), hold.holds);
	hold_show();
}
// An existing participant at the late-restart 0x47: held and timed at once.
void hold_open(join_in_progress_core::join_hold::role who, int newcomers, const char* where)
{
	if (!hold.begin(who, newcomers, hold_clock())) return;
	world_hold_skips = 0;
	world_hold = 1;
	hold_skips_armed = 0;
	hold_started(where);
}
// AQ2: the host at the admission of a newcomer (run_add, inside the zone loader before the zone loads): the world
// section is skipped from here; the hold opens at the first pump after this zone load (hold_tick). A second newcomer
// admitted at the same zone load joins the armed hold.
void hold_arm_on(join_in_progress_core::join_hold::role who, int index)
{
	if (hold.active())
	{
		hold.add_newcomer();
		logger::write("join hold: player %d joins the hold (%d newcomer(s))", index, hold.newcomers);
		hold_show();
		return;
	}
	if (!hold.arm(who, 1, hold_clock())) return;
	world_hold_skips = 0;
	hold_clock_skips = 0;
	hold_armed_limit = who == join_in_progress_core::join_hold::role::member ? hold.cap_ms + join_in_progress_core::join_hold_restart_limit_ms : join_in_progress_core::join_hold_armed_limit_ms;
	world_hold = 1;
	hold_armed_at = hold_now();
	logger::write("join hold: armed on this PC (%s) at the admission of player %d (the native add at the area-load boundary, before the zone loads): no world update runs on this PC from here; the hold opens when this zone load ends (frame %u, synchronized draws %u, game time %08x)",
		hold_role_name(who), index, unsigned(hold_armed_at.frame), hold_armed_at.draws, hold_armed_at.game_time);
}
void hold_arm(int index) { hold_arm_on(join_in_progress_core::join_hold::role::host, index); }

void hold_area_boundary(void* s)
{
	if (!hold_playing || !machine.started || epoch.started) return;
	const auto frame = probe_at<std::uint16_t>(native_frame);
	std::uint8_t newcomers = 0;
	if (host(s))
	{
		if (hold.enabled()) for (const auto& seat : machine.seats)
			if (seat.at == stage::pending && seat.index > 0 && seat.index < 4 && listed(s, seat.record)) newcomers |= std::uint8_t(1u << seat.index);
		// Unlike native 0x1f, this leaves immediately, before disk loading and outside the native parser's queue.
		gns_transport::announce_join_boundary(frame, newcomers);
		if (newcomers) logger::write("join hold: reliable admission notice sent before the zone load (frame %u, newcomer seats 0x%x); every existing participant waits for this decision at its own same-frame area boundary", unsigned(frame), unsigned(newcomers));
		return;
	}
	if (!hold.enabled()) return; // a disabled guest does not wait; a disabled host still published mask 0 above
	const auto began = hold_clock();
	hold_member_notified = true; // even a missing decision must not create a fresh deadline on a later 0x47
	const unsigned limit = hold.cap_ms + join_in_progress_core::join_hold_restart_limit_ms;
	while (!gns_transport::poll_join_boundary(frame, newcomers))
	{
		gns_transport::service_join_boundary(); // receives the control; queues game packets without dispatching them
		if (hold_clock() - began >= limit)
		{
			logger::write("join hold: no host admission decision for frame %u within %u ms (cap + safety); boundary wait released; native host-loss handling remains live", unsigned(frame), limit);
			return;
		}
		Sleep(1);
	}
	if (!newcomers) return; // ordinary zone load: no world section skipped, no hold, no panel
	for (int index = 1; index < 4; ++index) if (newcomers & (1u << index)) hold_arm_on(join_in_progress_core::join_hold::role::member, index);
	if (hold.armed) hold.admitted_at = hold.since = began; // one absolute member deadline across wait/load/open
	logger::write("join hold: reliable host admission notice accepted at this PC's area boundary (frame %u, newcomer seats 0x%x, waited %u ms); no world step before this PC's restart", unsigned(frame), unsigned(newcomers), unsigned(hold_clock() - began));
}
void hold_add_newcomer()
{
	if (!hold.active()) return;
	hold.add_newcomer();
	logger::write("join hold: %d newcomer(s) now in this hold", hold.newcomers);
	hold_show();
}
// AQ2: at the host's image capture (start_image, right after the native campaign start took the image): the proof
// line - nothing simulated on this PC between the hold's opening and the capture.
// The host's armed hold opens: at the first pump after the zone load (hold_tick), or at the capture if a capture
// came first (start_image needs the host's zone loaded, so the load has ended either way).
void hold_open_armed()
{
	hold_skips_armed = world_hold_skips;
	if (!hold.open(hold_clock())) return;
	char armed[200];
	std::snprintf(armed, sizeof armed, "the end of the zone load that admitted the newcomer (armed %u ms earlier at the admission; %u world update(s) skipped since)",
		hold.armed_ms, hold_skips_armed);
	hold_started(armed);
}
void hold_capture()
{
	if (!hold.active() || hold.who != join_in_progress_core::join_hold::role::host) return;
	if (hold.armed) hold_open_armed();
	const auto p = hold_now();
	// The input scheduler copies its NEXT buffered input frame into network+0x1c at 0x606ada, even with no world
	// update. That bookkeeping can move +1 while held; it is not a simulation step. Draws and world time must stay.
	const bool same = p.draws == hold_opened.draws && p.game_time == hold_opened.game_time;
	logger::write("join hold: image captured while held, %u ms after the opening (%u world update(s) skipped since the admission); frame %u -> %u, synchronized draws %u -> %u, game time %08x -> %08x - %s",
		hold.elapsed(hold_clock()), world_hold_skips, unsigned(hold_opened.frame), unsigned(p.frame), hold_opened.draws, p.draws, hold_opened.game_time, p.game_time,
		same ? "no simulation step between the zone load and the capture" : "the draws or the game time MOVED between the opening and the capture");
	logger::write("join hold: capture clock proof - %u native unpause(s) deferred (effective game time held; raw/network clock live); native input frame %u -> %u is transport bookkeeping (0x606ada), not a world update", hold_clock_skips, unsigned(hold_opened.frame), unsigned(p.frame));
}
// After the core closed the hold (poll or release): the bridge is retail's test again, the message goes, one line.
void hold_closed(join_in_progress_core::join_hold::event why, bool armed)
{
	using ev = join_in_progress_core::join_hold::event;
	world_hold = 0;
	hold_unshow();
	const auto now_point = hold_now();
	const hold_point& from = hold_opened.valid ? hold_opened : hold_armed_at;
	const unsigned ms = hold.ended_ms;
	const int ready = hold.ready < 0 ? 0 : hold.ready;
	char tail[224];
	std::snprintf(tail, sizeof tail, "%u world update(s) skipped while held; frame %u -> %u, synchronized draws %u -> %u, game time %08x -> %08x",
		world_hold_skips, unsigned(from.frame), unsigned(now_point.frame), from.draws, now_point.draws, from.game_time, now_point.game_time);
	switch (why)
	{
	case ev::restart:
		logger::write("join hold: %u ms, %d newcomer(s) ready - this PC's cohort restart begins (%s)", ms, ready, tail);
		break;
	case ev::cap:
		if (armed)
			logger::write("join hold: ended after %u ms - the zone load that admitted the newcomer did not end within %u ms; this PC simulates again (%s)", ms, hold_armed_limit, tail);
		else
			logger::write("join hold: cap reached after %u ms (%d of %d newcomer(s) ready) - this PC simulates again until the restart, as without the hold; the newcomer is not refused (%s)",
				ms, ready, hold.newcomers, tail);
		break;
	case ev::gone:
		logger::write("join hold: ended after %u ms - every newcomer of this hold left (seat freed); the session plays on (%s)", ms, tail);
		break;
	case ev::restart_late:
		logger::write("join hold: ended after %u ms - every newcomer was ready but this PC's restart did not begin within %u ms; the session plays on until it does (%s)",
			ms, join_in_progress_core::join_hold_restart_limit_ms, tail);
		break;
	case ev::session:
		logger::write("join hold: ended after %u ms by the session's end (%s)", ms, tail);
		break;
	default: break;
	}
	hold_opened = {};
	hold_armed_at = {};
}
// This PC's restart begins / the session ended: ends an open or armed hold (nothing when none is open).
void hold_end(join_in_progress_core::join_hold::event why)
{
	const bool armed = hold.armed;
	if (hold.release(why, hold_clock()) != join_in_progress_core::join_hold::event::none) hold_closed(why, armed);
	if (why == join_in_progress_core::join_hold::event::restart || why == join_in_progress_core::join_hold::event::session)
	{
		zone_loaded_at = {};
		hold_playing = hold_member_notified = false;
		gns_transport::clear_join_boundaries();
	}
}
// Every pump (tick), on every role: the zone-load edge; the host opens an armed hold once the zone load ended and
// counts its admitted seats; a member only has the cap.
void hold_tick()
{
	using ev = join_in_progress_core::join_hold::event;
	const bool loading = hold_zone_loading && hold_zone_loading();
	if (zone_was_loading && !loading) zone_loaded_at = hold_now();
	zone_was_loading = loading;
	if (!hold.active()) return;
	if (hold.armed && !loading) hold_open_armed();
	int waiting = -1, done = -1;
	if (hold.who == join_in_progress_core::join_hold::role::host)
	{
		waiting = done = 0;
		for (const auto& seat : machine.seats)
		{
			if (seat.at == stage::adding || seat.at == stage::streaming) ++waiting;
			else if (seat.at == stage::starting || seat.at == stage::joined) ++done;
		}
	}
	const bool armed = hold.armed;
	const auto now = hold_clock();
	const auto e = hold.poll(now, waiting, done);
	if (e == ev::ready)
		logger::write("join hold: every newcomer is ready after %u ms (%d newcomer(s) have the campaign image); held until this PC's restart begins",
			hold.elapsed(now), hold.ready);
	else if (e != ev::none) { hold_closed(e, armed); return; }
	hold_show();
}
