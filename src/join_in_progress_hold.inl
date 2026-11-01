// ---- p060fix/AQ: the join hold ([Online] JoinHoldSeconds; rules: world_gate_*, message_*; core: join_hold) ---------
// From the host's image capture (start_image) - an existing participant: the late-restart 0x47 (campaign_notice) -
// until this PC's own cohort restart begins (deferred_campaign_start), the game update's world section is skipped, so
// no peer runs game frames, draws, AI or timed events the image does not contain. The main loop keeps running: the
// scheduler (network, image transfer, this module's tick), the input manager update and rendering, with the native
// timed network message showing "A player is joining..." on the host and the existing participants (a newcomer
// never holds: it is loading the image). Ends: this PC's restart (the normal end), the cap while a newcomer is not
// ready (the PC then simulates as before the hold: today's path), every newcomer gone, the session's end, and a
// restart that does not begin within join_hold_restart_limit_ms of every newcomer being ready.
volatile std::uint8_t world_hold = 0;   // read by the bridge on the game thread; written on the game thread only
unsigned world_hold_skips = 0;          // game updates whose world section was skipped on this PC in the current hold
join_in_progress_core::join_hold hold;  // [Online] JoinHoldSeconds (configure at install)
DWORD (WINAPI* hold_clock)() = &GetTickCount; // mutable for the owned native fixture only
const char* hold_text_shown = nullptr;  // the text pointer this hold put into the native message slots
int mesh_newcomers = 0;                 // a member: participants that completed the mesh with this PC since its last restart
struct hold_point { std::uint16_t frame = 0; std::uint32_t draws = 0, game_time = 0; };
hold_point hold_opened{};
std::uint32_t message_clear_at = message_clear, message_show_at = message_show; // mutable for the owned native fixture only

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
	return p;
}
const char* hold_role_name(join_in_progress_core::join_hold::role r)
{
	return r == join_in_progress_core::join_hold::role::host ? "host" : "existing participant";
}
// The native message slot that is current, or null when the scheduler is unreadable.
std::uint8_t* message_current_slot()
{
	auto* scheduler = static_cast<std::uint8_t*>(fn<getter_t>(scheduler_getter)());
	if (!scheduler) return nullptr;
	const auto index = at<std::uint32_t>(scheduler, message_current_at);
	if (index >= message_slot_count) return nullptr;
	return scheduler + message_slots_at + index * message_slot_bytes;
}
void hold_show()
{
	if (!hold.active()) return;
	const char* text = join_in_progress_core::join_hold_text(hold.newcomers);
	__try
	{
		if (hold_text_shown && hold_text_shown != text)
		{
			// A second newcomer joined the hold after the first text went up: point the shown slot at the new count.
			if (auto* slot = message_current_slot(); slot && at<const char*>(slot, 0) == hold_text_shown) { at<const char*>(slot, 0) = text; hold_text_shown = text; return; }
		}
		if (hold_text_shown == text) return;
		// Seconds: the cap plus the restart allowance, so the native timer never drops it while the hold lasts; the
		// release expires it at once.
		const float seconds = float(hold.cap_ms + join_in_progress_core::join_hold_restart_limit_ms) / 1000.0f;
		fn<message_t>(message_show_at)(fn<getter_t>(scheduler_getter)(), text, join_hold_priority, seconds);
		hold_text_shown = text;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		logger::write_once("jip:hold-show", "join hold: the on-screen message could not be shown (the hold itself goes on)");
	}
}
void hold_unshow()
{
	if (!hold_text_shown) return;
	const char* shown = hold_text_shown;
	hold_text_shown = nullptr;
	__try
	{
		auto* scheduler = fn<getter_t>(scheduler_getter)();
		auto* slot = message_current_slot();
		if (scheduler && slot && at<const char*>(slot, 0) == shown) fn<init_t>(message_clear_at)(scheduler);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		logger::write_once("jip:hold-unshow", "join hold: the on-screen message could not be cleared (its native timer ends it)");
	}
}
// Opens the hold on this PC (the host at its image capture, a member at the late-restart 0x47).
void hold_open(join_in_progress_core::join_hold::role who, int newcomers, const char* where)
{
	if (!hold.begin(who, newcomers, hold_clock())) return;
	world_hold_skips = 0;
	world_hold = 1;
	hold_opened = hold_now();
	logger::write("join hold: the game simulation is held on this PC (%s) from %s: %d newcomer(s), cap %u ms ([Online] JoinHoldSeconds=%u); frame %u, synchronized draws %u, game time %08x; the window keeps running and shows \"%s\" (hold %u)",
		hold_role_name(who), where, hold.newcomers, hold.cap_ms, hold.cap_ms / 1000u, unsigned(hold_opened.frame), hold_opened.draws,
		hold_opened.game_time, join_in_progress_core::join_hold_text(hold.newcomers), hold.holds);
	hold_show();
}
void hold_add_newcomer()
{
	if (!hold.active()) return;
	hold.add_newcomer();
	logger::write("join hold: %d newcomer(s) now in this hold", hold.newcomers);
	hold_show();
}
// After the core closed the hold (poll or release): the bridge is retail's test again, the message goes, one line.
void hold_closed(join_in_progress_core::join_hold::event why)
{
	using ev = join_in_progress_core::join_hold::event;
	world_hold = 0;
	hold_unshow();
	const auto now_point = hold_now();
	const unsigned ms = hold.ended_ms;
	const int ready = hold.ready < 0 ? 0 : hold.ready;
	char tail[224];
	std::snprintf(tail, sizeof tail, "%u world update(s) skipped while held; frame %u -> %u, synchronized draws %u -> %u, game time %08x -> %08x",
		world_hold_skips, unsigned(hold_opened.frame), unsigned(now_point.frame), hold_opened.draws, now_point.draws, hold_opened.game_time, now_point.game_time);
	switch (why)
	{
	case ev::restart:
		logger::write("join hold: %u ms, %d newcomer(s) ready - this PC's cohort restart begins (%s)", ms, ready, tail);
		break;
	case ev::cap:
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
}
// This PC's restart begins / the session ended: ends an open hold (nothing when none is open).
void hold_end(join_in_progress_core::join_hold::event why)
{
	if (hold.release(why, hold_clock()) != join_in_progress_core::join_hold::event::none) hold_closed(why);
}
// Every pump (tick), on every role: the host counts its admitted seats; a member only has the cap.
void hold_tick()
{
	using ev = join_in_progress_core::join_hold::event;
	if (!hold.active()) return;
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
	const auto now = hold_clock();
	const auto e = hold.poll(now, waiting, done);
	if (e == ev::ready)
		logger::write("join hold: every newcomer is ready after %u ms (%d newcomer(s) have the campaign image); held until this PC's restart begins",
			hold.elapsed(now), hold.ready);
	else if (e != ev::none) { hold_closed(e); return; }
	hold_show();
}
