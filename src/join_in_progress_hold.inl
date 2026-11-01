// ---- p060fix/AQ: the join hold ([Online] JoinHoldSeconds; rules: world_gate_*; core: join_hold) ---------------------
// p060fix/AQ2: on the host the hold is ARMED at the admission - the native add at the synchronous area-load boundary
// (area_barrier_bridge -> advance -> run_add, inside the zone loader 0x484ce0 before the zone loads) - so no game
// update after that zone load runs its world section on the host, and it OPENS (clock, cap, line, message) at the
// first pump after that zone load, before the image capture (live-060zi ZI-1bp: the host ran ~30 world updates,
// +55 draws, +1.0 s game time, alone between its zone-load end and the capture). An existing participant holds from
// the late-restart 0x47 (campaign_notice) as before: its world does not run in that window (ZI-1bp: draws 3698 at its
// zone-load end and at its 0x47; no world update reached its gate while held); every hold opening logs this PC's
// draws / game time since its own zone-load end to keep measuring it. Until this PC's own cohort restart begins
// (deferred_campaign_start) the game update's world section is skipped. The main loop keeps running: the scheduler
// (network, image transfer, this module's tick), the input manager update and rendering; the fix draws "A player is
// joining..." itself into the back buffer before every Present (hold_overlay.cpp via display::set_overlay) on the
// host and the existing participants (a newcomer never holds: it is loading the image). Ends: this PC's restart (the
// normal end), the cap while a newcomer is not ready (the PC then simulates as before the hold: today's path), every
// newcomer gone, the session's end, and a restart that does not begin within join_hold_restart_limit_ms of every
// newcomer being ready.
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
void hold_arm(int index)
{
	if (hold.active())
	{
		hold.add_newcomer();
		logger::write("join hold: player %d joins the hold (%d newcomer(s))", index, hold.newcomers);
		hold_show();
		return;
	}
	if (!hold.arm(join_in_progress_core::join_hold::role::host, 1, hold_clock())) return;
	world_hold_skips = 0;
	world_hold = 1;
	hold_armed_at = hold_now();
	logger::write("join hold: armed on this PC (host) at the admission of player %d (the native add at the area-load boundary, before the zone loads): no world update runs on this PC from here; the hold opens when this zone load ends (frame %u, synchronized draws %u, game time %08x)",
		index, unsigned(hold_armed_at.frame), hold_armed_at.draws, hold_armed_at.game_time);
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
	const bool same = p.frame == hold_opened.frame && p.draws == hold_opened.draws && p.game_time == hold_opened.game_time;
	logger::write("join hold: image captured while held, %u ms after the opening (%u world update(s) skipped since the admission); frame %u -> %u, synchronized draws %u -> %u, game time %08x -> %08x - %s",
		hold.elapsed(hold_clock()), world_hold_skips, unsigned(hold_opened.frame), unsigned(p.frame), hold_opened.draws, p.draws, hold_opened.game_time, p.game_time,
		same ? "no simulation step between the zone load and the capture" : "the draws or the game time MOVED between the opening and the capture");
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
			logger::write("join hold: ended after %u ms - the zone load that admitted the newcomer did not end within %u ms; this PC simulates again (%s)", ms, join_in_progress_core::join_hold_armed_limit_ms, tail);
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
	if (why == join_in_progress_core::join_hold::event::restart || why == join_in_progress_core::join_hold::event::session) zone_loaded_at = {};
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
