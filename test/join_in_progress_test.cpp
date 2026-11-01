#include "join_in_progress_core.hpp"
#include "join_in_progress_rules.hpp"
#include "join_in_progress_diagnostics.hpp"
#include "gameplay_capabilities.hpp"
#include "actor_ai_layout.hpp" // p060fix/AI: the brain ring layout per stage, checked against E4's ledger rows
#include "e4_core_sites.hpp"
#include <cstdlib>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// [Online] JoinInProgress: the host's bring-in state machine without the game (src/join_in_progress_core.hpp), the
// rule-26 agreement entry, and - with a retail XMen2.exe as the optional argument, read only - every install guard.
namespace
{
	int failures = 0, passes = 0;
	void check(bool ok, const char* what)
	{
		std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
		ok ? ++passes : ++failures;
	}
	using namespace join_in_progress_core;
	bool image(const char* path, std::vector<std::uint8_t>& mapped)
	{
		std::ifstream f(path, std::ios::binary);
		std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(f)), {});
		if (file.size() < 0x400) return false;
		auto u32 = [&](std::size_t at) { std::uint32_t v; std::memcpy(&v, file.data() + at, 4); return v; };
		auto u16 = [&](std::size_t at) { std::uint16_t v; std::memcpy(&v, file.data() + at, 2); return v; };
		const auto pe = u32(0x3c);
		const auto sections = pe + 24 + u16(pe + 20);
		mapped.assign(u32(pe + 24 + 56), 0);
		std::memcpy(mapped.data(), file.data(), u32(pe + 24 + 60));
		for (unsigned i = 0; i < u16(pe + 6); ++i)
		{
			const auto s = sections + i * 40;
			const auto va = u32(s + 12), raw = u32(s + 20), size = std::min(u32(s + 16), u32(s + 8) ? u32(s + 8) : u32(s + 16));
			if (raw + size <= file.size() && va + size <= mapped.size()) std::memcpy(mapped.data() + va, file.data() + raw, size);
		}
		return true;
	}
	observation seen(bool load, bool waiting, bool image_done, bool loaded, bool host_loaded)
	{
		observation o;
		o.zone_load_pending = load; o.add_waiting = waiting; o.image_done = image_done; o.loaded = loaded; o.host_loaded = host_loaded;
		return o;
	}
}

int main(int argc, char** argv)
{
	check(admission_allowed(false), "AC: default allows lobby admission before START");
	check(!admission_allowed(true), "AC: default refuses admission after START");
	check(admission_allowed(true, true), "AC: explicit LateJoin=1 retains experimental restore");
	check(!admission_allowed(true, false), "AC: explicit LateJoin=0 refuses after START");
	check(is_started_refusal(started_refusal.data(), started_refusal.size()), "AC: exact started refusal marker recognized");
	check(!is_started_refusal(nullptr, 8) && !is_started_refusal(started_refusal.data(), 7), "AC: truncated or absent refusal cannot invent a started reason");
	{
		gameplay_capabilities::record lobby, late, old;
		lobby.add(gameplay_capabilities::join_in_progress_id, 7, {1,4,0,0}, true);
		late.add(gameplay_capabilities::join_in_progress_id, 7, agreement_parameters, true);
		old.add(gameplay_capabilities::join_in_progress_id, 6, {1,4,0,0}, true);
		lobby.finalize(); late.finalize(); old.finalize();
		check(lobby.compare(late.bytes()) == gameplay_capabilities::result::match, "AC: host admission choice does not alter the frozen gameplay record");
		check(lobby.compare(old.bytes()) != gameplay_capabilities::result::match, "AC: old revision cannot agree with the safe default");
	}
	{
		join_in_progress_diagnostics::window w;
		check(!w.capture(0), "AC: ordinary periods have no diagnostic budget");
		w.begin(0xfffffff0u);
		check(w.capture(10), "AC: admission observation survives clock wrap");
		check(!w.compare(10, false, 30), "AC: wait for all same-frame checksum heads");
		check(w.compare(10, true, 30) && !w.compare(10, true, 30), "AC: arbitration retries do not duplicate a period");
		for (unsigned i=1; i<4; ++i) check(w.compare(10, true, std::uint16_t(30*(i+1))), "AC: next checksum period is observed");
		check(!w.compare(10, true, 150), "AC: comparison log stops after four periods");
		w.produced=4; check(!w.capture(10), "AC: producer log stops after four periods");
		w.begin(20); check(w.capture(21) && !w.capture(180020), "AC: new admission rearms with a hard time bound");
	}
	{
		char out[128]{};
		auto format = [](char* dst, int size, const char* pattern, const char* hero) {
			return std::snprintf(dst, static_cast<std::size_t>(size), pattern, hero);
		};
		check(format_roster_hero(format, out, 128, " (%s)", "") == 13 && std::strcmp(out, " (Spectating)") == 0,
		      "B3 one-hero party: the joiner's empty hero label says Spectating");
		format_roster_hero(format, out, 128, " (%s)", "Wolverine");
		check(std::strcmp(out, " (Wolverine)") == 0, "host's native hero display name is preserved");
		format_roster_hero(format, out, 128, " (%s)", "Cyclops");
		check(std::strcmp(out, " (Cyclops)") == 0, "party widens: the current hero replaces Spectating immediately");
		format_roster_hero(format, out, 128, " (%s)", nullptr);
		check(std::strcmp(out, " (Spectating)") == 0, "a missing label is safe too");
		std::memset(out, 0x5a, sizeof(out));
		format_roster_hero(format, out, 8, " (%s)", "");
		check(out[7] == 0 && out[8] == 0x5a, "spectator suffix respects the native formatter's supplied capacity");
	}

	{
		std::array<std::uint8_t, 64> wrapper{};wrapper[0]=0x5a;
		auto* cursor=wrapper.data()+24;
		std::memcpy(wrapper.data()+32,&cursor,sizeof(cursor));
		rewind_campaign(wrapper.data(),32);
		std::memcpy(&cursor,wrapper.data()+32,sizeof(cursor));
		check(cursor==wrapper.data() && wrapper[0]==0x5a,"host replay rewinds the consumed serializer cursor without changing campaign bytes");
	}
	check(std::any_of(join_in_progress_rules::guards.begin(), join_in_progress_rules::guards.end(), [](const auto& g) {
		return g.va <= 0x484def && g.va + g.hex.size() / 2 >= 0x484df4;
	}), "the synchronous mid-game area boundary is guarded (pump polling misses the load)");
	check(std::any_of(join_in_progress_rules::guards.begin(), join_in_progress_rules::guards.end(), [](const auto& g) {
		return g.va <= 0x609757 && g.va + g.hex.size() / 2 >= 0x60975c;
	}), "the first start keeps the native campaign transfer and completion handlers for future bring-ins");
	check(std::any_of(join_in_progress_rules::guards.begin(), join_in_progress_rules::guards.end(), [](const auto& g) {
		return g.va <= 0x6051bb && g.va + g.hex.size() / 2 >= 0x6051c5;
	}), "the mesh shortcut preserves the native reliable 0x25 completion reply");
	int r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0;
	{
		std::array<std::uint8_t,5> entry{0xe9,0,0,0,0};
		const std::uint32_t target=0x12345678,relative=target-join_in_progress_rules::campaign_receive-5;
		std::memcpy(entry.data()+1,&relative,4);
		check(join_in_progress_rules::receiver_composed(entry.data(),target),"receiver composition: exact entity-capacity-owned target accepted");
		check(!join_in_progress_rules::receiver_composed(entry.data(),0) && !join_in_progress_rules::receiver_composed(entry.data(),target+1),"receiver composition: unknown or different owner refused");
		entry[0]=0x90;
		check(!join_in_progress_rules::receiver_composed(entry.data(),target),"receiver composition: a different entry instruction refused");
	}
	{
		// Solo story segment (live E7: wolverine/cyclops -> magma alone -> cyclops/iceman/storm/wolverine).
		auto nm = [](const char* s) { std::array<char, 32> a{}; std::strncpy(a.data(), s, 31); return a; };
		seat_binding before; before.name = {nm("wolverine"), nm("cyclops"), {}, {}}; before.controller = {0, 1, -1, -1}; before.human = {true, true, false, false};
		const std::array<bool, 4> two{true, true, false, false};
		owner_memory m{};
		check(wide(before, two), "both players hold a hero: a wide zone");
		remember(m, before);
		check(same_name(m[0], nm("wolverine")) && same_name(m[1], nm("cyclops")), "the wide zone's owners are remembered");
		seat_binding solo; solo.name = {nm("magma"), {}, {}, {}}; solo.controller = {0, -1, -1, -1}; solo.human = {true, false, false, false};
		check(!wide(solo, two) && restore_owners(solo, m) == 0, "Magma alone: the joiner holds nothing, nothing moves, the memory is kept");
		seat_binding back; back.name = {nm("cyclops"), nm("iceman"), nm("storm"), nm("wolverine")};
		back.controller = {0, 1, 0, 0}; back.human = {true, true, false, false}; // the native binding in slot order
		check(restore_owners(back, m) == 2, "two swaps when the party widens");
		check(back.controller[3] == 0 && back.human[3] && back.controller[0] == 1 && back.human[0] && !back.human[1] && !back.human[2],
		      "the host has Wolverine again and the joiner Cyclops; Iceman and Storm are AI");
		seat_binding again = back;
		check(restore_owners(again, m) == 0, "a second pass changes nothing (idempotent)");
		seat_binding missing; missing.name = {nm("phoenix"), nm("frost"), {}, {}}; missing.controller = {0, 1, -1, -1}; missing.human = {true, true, false, false};
		check(restore_owners(missing, m) == 0, "remembered heroes not in the party: the native binding stands");
		// Joiner-is-solo: the joiner had Magma; the story seats Magma alone -> the joiner plays it, the host watches.
		owner_memory j{};
		seat_binding wide2; wide2.name = {nm("phoenix"), nm("magma"), nm("iceman"), {}}; wide2.controller = {0, 1, -1, -1}; wide2.human = {true, true, false, false};
		remember(j, wide2);
		seat_binding solo2; solo2.name = {nm("magma"), {}, {}, {}}; solo2.controller = {0, -1, -1, -1}; solo2.human = {true, false, false, false};
		check(restore_owners(solo2, j) == 1 && solo2.controller[0] == 1 && solo2.human[0], "joiner-is-solo: Magma goes to the joiner who had it");
		check(!wide(solo2, two), "the host watches: memory kept");
		seat_binding host_solo; host_solo.name = {nm("phoenix"), {}, {}, {}}; host_solo.controller = {0, -1, -1, -1}; host_solo.human = {true, false, false, false};
		check(restore_owners(host_solo, j) == 0 && host_solo.controller[0] == 0, "host-is-solo: Phoenix stays with the host who had it");
	}
	{
		// 0.3.0d (owner's three-PC test, mess hall): a normal level change after hero switches keeps the switches; the
		// owners apply only inside a solo segment and in the zone where it ends.
		auto nm = [](const char* s) { std::array<char, 32> a{}; std::strncpy(a.data(), s, 31); return a; };
		const std::array<bool, 4> three{true, true, true, false};
		auto bind = [&](std::array<const char*, 4> n, std::array<std::int32_t, 4> c) {
			seat_binding b;
			for (int k = 0; k < 4; ++k) { b.name[k] = n[k] ? nm(n[k]) : std::array<char, 32>{}; b.controller[k] = c[k]; b.human[k] = n[k] && c[k] >= 0 && c[k] < 4; }
			return b;
		};
		segment_tracker t{};
		// Garage: Magma 0, Phoenix 1, Wolverine 2, Iceman AI.
		seat_binding garage = bind({"magma", "phoenix", "wolverine", "iceman"}, {0, 1, 2, 255});
		check(observe_segment(t, wide(garage, three), true) == segment_step::none, "first wide zone: no owners applied");
		settle_segment(t, garage, true, true, 0);
		check(observe_segment(t, true, false) == segment_step::none, "its refresh: nothing applied");
		settle_segment(t, garage, true, false, 0);
		// The players switch (host takes Wolverine, Michael Magma); the mess hall's native binding reads the heroes' own
		// controllers.
		seat_binding mess = bind({"magma", "phoenix", "wolverine", "iceman"}, {2, 1, 0, 255});
		check(observe_segment(t, wide(mess, three), true) == segment_step::none, "level change after switches: no hand-back (bug A)");
		settle_segment(t, mess, true, true, 0);
		check(observe_segment(t, true, false) == segment_step::none, "its refresh: no hand-back either");
		settle_segment(t, mess, true, false, 0);
		check(same_name(t.wide[0], nm("wolverine")) && same_name(t.wide[2], nm("magma")), "the current owners are what a segment would start from");
		// A segment starts: Magma alone. The heroes' own controllers at that binding (another switch in the mess hall:
		// the host took Iceman, Wolverine went AI).
		seat_binding solo = bind({"magma", nullptr, nullptr, nullptr}, {0, -1, -1, -1});
		check(observe_segment(t, wide(solo, three), true) == segment_step::started, "Magma alone: the segment starts");
		// The live heroes when seatParty changed the party (join_in_progress.cpp note_party_change).
		owner_memory live{}; live[0] = nm("iceman"); live[1] = nm("phoenix"); live[2] = nm("magma");
		t.owners = live;
		check(same_name(t.owners[0], nm("iceman")) && same_name(t.owners[2], nm("magma")) && same_name(t.owners[1], nm("phoenix")), "the segment's start: host Iceman, joiner 1 Phoenix, joiner 2 Magma");
		check(restore_owners(solo, t.owners) == 1 && solo.controller[0] == 2, "inside: Magma goes to joiner 2 who had it");
		settle_segment(t, solo, false, true, 1);
		check(observe_segment(t, false, false) == segment_step::inside, "the segment's refresh: still inside");
		settle_segment(t, solo, false, false, 0);
		// It ends: the native binding in slot order.
		seat_binding back = bind({"magma", "phoenix", "wolverine", "iceman"}, {0, 1, 2, 255});
		check(observe_segment(t, wide(back, three), true) == segment_step::ended, "the party widens: the segment ends");
		const int moved = restore_owners(back, t.owners);
		check(moved == 2 && back.controller[3] == 0 && back.human[3] && back.controller[0] == 2 && back.controller[1] == 1 && !back.human[2],
		      "hand-back: host Iceman, joiner 1 Phoenix, joiner 2 Magma, Wolverine AI (the heroes at the segment's start)");
		settle_segment(t, back, true, true, moved);
		seat_binding refresh = bind({"magma", "phoenix", "wolverine", "iceman"}, {0, 1, 2, 255});
		check(observe_segment(t, true, false) == segment_step::ended, "the zone start's refresh hands back too (heroes not yet applied)");
		check(restore_owners(refresh, t.owners) == 2, "the same hand-back at the refresh");
		settle_segment(t, refresh, true, false, 2);
		check(!t.handback && !t.in_segment, "applied to the heroes: the hand-back is done");
		// The next level change after more switches keeps them.
		seat_binding later = bind({"magma", "phoenix", "wolverine", "iceman"}, {1, 2, 255, 0});
		check(observe_segment(t, wide(later, three), true) == segment_step::none && restore_owners(later, owner_memory{}) == 0, "after the hand-back a level change keeps the switches");
		segment_tracker v{};
		settle_segment(v, mess, true, true, 0);
		check(observe_segment(v, false, true) == segment_step::started && same_name(v.owners[0], nm("wolverine")) && same_name(v.owners[2], nm("magma")),
		      "without a noted party change the segment starts from the last wide binding's owners");
		// A hand-back already in place at the load ends at once.
		segment_tracker u{};
		settle_segment(u, garage, true, true, 0);
		observe_segment(u, false, true);
		observe_segment(u, true, true);
		settle_segment(u, garage, true, true, 0);
		check(!u.handback && observe_segment(u, true, false) == segment_step::none, "nothing to move: the hand-back is done at the load");
	}
	check(campaign_transfer_mode(1) == 2,
		"a New Game host streams a full campaign restore (mode 2); mode 1 restores only stats");
	check(campaign_transfer_mode(2) == 2, "a Load Saved Campaign host keeps the full restore mode");
	check(campaign_restore_mode(0,true)==2,"cohort host uses the same full restore mode as the joiners");
	check(campaign_restore_mode(0,false)==0 && campaign_restore_mode(1,true)==1,"ordinary native load modes are unchanged");
	check(!participant_loaded(0,false,42),"old native frames cannot finish before a cohort load was observed");
	check(!participant_loaded(1,true,0),"ready-to-load grid=1 at reset frame zero cannot finish the cohort");
	check(participant_loaded(0,true,1),"first fresh native lockstep frame finishes the cohort outside the team menu");
	check(participant_loaded(2,false,0),"native team-menu loaded grid remains accepted");
	{
		cohort c;c.begin();
		check(!c.resume_controls(true),"cohort: old ready world cannot resume controls before native start");
		check(c.active && !c.observe(false,false), "cohort: old area ready does not resume checksums before transfer");
		check(c.announce() && !c.announce(), "cohort: only the first native completion may announce a restart");
		check(c.request() && !c.request(), "cohort: packet callbacks queue one deferred restart");
		check(c.start() && !c.start(), "cohort: nested or duplicate native starts cannot restart the loader twice");
		check(!c.resume_controls(true),"cohort: controls wait for an actual native campaign load");
		check(!c.observe(false,false), "cohort: wait until native loading was actually observed");
		check(!c.observe(true,false) && c.active, "cohort: native loader keeps checksum hold");
		check(!c.resume_controls(false),"cohort: controls remain disabled while the native world loads");
		check(c.resume_controls(true) && !c.resume_controls(true),"cohort: native controls resume once after world restore");
		check(!c.observe(false,true) && c.active, "cohort: newcomer load barrier keeps checksum hold");
		check(c.observe(false,false) && !c.active, "cohort: native load and barrier released, checksums resume");
		check(!c.observe(false,false), "cohort: checksum resume happens once");
	}
	{
		machine m;
		check(!m.defer_add(&r1, 1, false, 0), "lobby (not started): the native add runs");
		m.started = true;
		check(!m.defer_add(&r1, 1, true, 0), "started: an active record (a ready toggle) runs the native add");
		check(m.defer_add(&r2, 2, false, 100), "started: a new record is deferred - pending");
		check(m.pending(&r2) && !m.holding(), "pending: outside the lockstep, nothing held");
		check(m.defer_add(&r2, 2, false, 200), "a second 0x1e of the pending player stays deferred");
		check(!m.pending(&r1), "other records are not pending");
		auto& s = *m.find(&r2);
		check(m.step(s, seen(false, false, false, false, false), 300) == action::none && s.at == stage::pending, "mid-zone: pending waits");
		check(m.step(s, seen(true, false, false, false, false), 400) == action::run_add && s.at == stage::adding, "zone load pending: the add runs");
		check(!m.pending(&r2) && m.holding(), "adding: no longer pending (walked by the lockstep), the barrier is held");
		check(!m.defer_add(&r2, 2, false, 450), "the host's own add call during adding is not deferred");
		check(m.step(s, seen(true, false, false, false, false), 500) == action::none, "adding: no image before the host has loaded");
		check(m.step(s, seen(true, true, false, false, true), 600) == action::none, "adding: the add's wait is still on");
		check(m.step(s, seen(true, false, false, false, true), 700) == action::send_image && s.at == stage::streaming, "host loaded and the add done: the image goes out");
		check(m.step(s, seen(true, false, false, false, true), 800) == action::none, "streaming: waits for the 'J'");
		check(m.step(s, seen(true, false, true, false, true), 900) == action::send_start && s.at == stage::starting, "image done: 0x26 to the newcomer");
		check(m.step(s, seen(true, false, true, false, true), 1000) == action::none && m.holding(), "starting: the barrier stays held until the newcomer loaded");
		check(m.step(s, seen(true, false, true, true, true), 1100) == action::finish && s.at == stage::joined && !m.holding(), "newcomer loaded: released");
	}
	{
		machine m; m.started = true;
		m.defer_add(&r1, 1, false, 0); m.defer_add(&r2, 2, false, 0); m.defer_add(&r3, 3, false, 0); m.defer_add(&r4, 0, false, 0);
		check(!m.defer_add(&r5, 1, false, 0), "four seats taken: a fifth record is not tracked (the engine cap refuses it first)");
		m.gone(&r2);
		check(!m.pending(&r2) && m.defer_add(&r5, 2, false, 10), "a pending player left: the seat is free again for the next newcomer");
		m.reset();
		check(!m.any() && !m.started, "session end resets");
	}
	{
		machine m; m.started = true; m.defer_add(&r1, 1, false, 0);
		auto& s = *m.find(&r1);
		m.step(s, seen(true, false, false, false, false), 1000);
		check(m.step(s, seen(true, true, false, false, true), 1000 + add_limit_ms + 1) == action::give_up && s.at == stage::free && !m.holding(), "the add's wait outlives its limit: give up, release");
		m.defer_add(&r2, 2, false, 0);
		auto& t = *m.find(&r2);
		m.step(t, seen(true, false, false, false, false), 0); m.step(t, seen(true, false, false, false, true), 0);
		check(m.step(t, seen(true, false, false, false, true), image_limit_ms + 1) == action::give_up, "the image outlives its limit: give up");
	}
	{
		using namespace gameplay_capabilities;
		check(std::string(rule_name(join_in_progress_id)) == "JoinInProgress" && join_in_progress_id == 26, "rule 26: name and ID");
		record off, on;
		check(on.add(join_in_progress_id, join_in_progress_rules::agreement_revision, {1, 4, 0, 0}, true), "rule 26 registers");
		off.finalize(); on.finalize();
		difference d;
		check(on.compare(off.bytes(), &d) != result::match, "a peer without rule 26 does not agree");
		record previous;
		check(previous.add(join_in_progress_id, 3, {1, 4, 0, 0}, true), "prior JIP capability record is valid"); previous.finalize();
		check(on.compare(previous.bytes()) != result::match, "the prior pending-seat behavior cannot agree with this revision");
		char text[160]{};
		on.describe(d.buckets, d.local_only, true, text, sizeof text);
		check(std::strstr(text, "26 JoinInProgress") != nullptr, "the refusal names 26 JoinInProgress");
		std::printf("  refusal text: %s\n", text);
	}
	if (argc > 1)
	{
		std::vector<std::uint8_t> mapped;
		const bool loaded = image(argv[1], mapped);
		check(loaded, "retail image mapped");
		if (loaded)
		{
			const auto* bad = join_in_progress_rules::first_mismatch(mapped.data());
			check(bad == nullptr, bad ? bad->what : "every install guard matches the retail executable");
			check(join_in_progress_rules::first_mismatch(mapped.data(), true) == nullptr, "AM: probe-only visibility/spawner guards match retail");
			for (const auto& g : join_in_progress_rules::audit_probe_guards)
			{
				auto changed = mapped; changed[g.va - limits_rules::image_base] ^= 1;
				const auto* refused = join_in_progress_rules::first_mismatch(changed.data(), true);
				check(refused && refused->va == g.va, "AM: probe refuses a changed visibility/spawner site");
				check(join_in_progress_rules::first_mismatch(changed.data()) == nullptr, "AM: probe-only guards do not change release acceptance");
			}
			// The cohort restart must refuse an executable whose native clock-step reset changed.
			auto clock_changed = mapped;
			clock_changed[0x55b230 - limits_rules::image_base] ^= 1;
			const auto* clock_bad = join_in_progress_rules::first_mismatch(clock_changed.data());
			check(clock_bad && clock_bad->va == 0x55b230, "cohort clock reset ABI is guarded against a changed executable");
			auto getter_changed = mapped;
			getter_changed[0x55b610 - limits_rules::image_base] ^= 1;
			const auto* getter_bad = join_in_progress_rules::first_mismatch(getter_changed.data());
			check(getter_bad && getter_bad->va == 0x55b610, "cohort clock getter entry refuses an unowned detour");
			auto seats_changed = mapped;
			seats_changed[0x612b25 - limits_rules::image_base] ^= 1;
			const auto* seats_bad = join_in_progress_rules::first_mismatch(seats_changed.data());
			check(seats_bad && seats_bad->va == 0x612b25, "participant creation hero-seat side effects have an exact retail guard");
			for (const auto va : {0x5d950cu, 0x5d95bdu, 0x5604f0u})
			{
				auto changed = mapped;
				changed[va - limits_rules::image_base] ^= 1;
				check(join_in_progress_rules::first_mismatch(changed.data()) != nullptr,
				      "roster hook refuses a changed hero lookup, format call or formatter ABI");
			}
			using namespace join_in_progress_rules;
			auto same = [&](std::uint32_t va, const auto& bytes) { return std::memcmp(mapped.data() + va - limits_rules::image_base, bytes.data(), bytes.size()) == 0; };
            for (std::size_t i = 0; i < ready_calls.size(); ++i) {
                check(same(ready_calls[i], ready_originals[i]), "Team readiness replacement matches the retail CALL bytes");
                auto changed = mapped;
                changed[ready_calls[i] - limits_rules::image_base] ^= 1;
                const auto* bad_ready = join_in_progress_rules::first_mismatch(changed.data());
                check(bad_ready && bad_ready->va == ready_calls[i], "changed Team readiness CALL refuses the JIP transaction");
            }
			check(same(roster_hero_call, roster_hero_original) && same(teardown_call, teardown_original) && same(gamespy_call, gamespy_original) && same(gamespy_think, gamespy_think_original) && same(add_entry, add_original) &&
			      same(record_seats, record_seats_original) &&
			      same(area_barrier_call, area_barrier_original) &&
			      same(campaign_unregister_call, campaign_unregister_original) &&
			      same(checksum_send, checksum_send_original) && same(checksum_compare, checksum_compare_original) &&
			      same(broadcast_entry, broadcast_entry_original) &&
			      same(campaign_load_call, campaign_load_original) &&
			      same(campaign_receive, campaign_receive_original) && same(campaign_begin, campaign_begin_original) &&
			      same(frame_walk, frame_walk_original) && same(stream_walk, stream_walk_original) && same(loaded_call, loaded_original) &&
			      same(mesh_wait_call, mesh_wait_original) && same(natneg_reply, natneg_reply_original) && same(admit_controller, admit_controller_original) &&
			      same(broadcast_local_tests[0], broadcast_local_originals[0]) && same(broadcast_local_tests[1], broadcast_local_originals[1]) && same(broadcast_local_tests[2], broadcast_local_originals[2]) && same(broadcast_local_tests[3], broadcast_local_originals[3]) && same(watchdog_peer, watchdog_peer_original) && same(broadcast_local_tests[4], broadcast_local_originals[4]) && same(seat_binding_call, seat_binding_original) && same(seat_rebinding_call, seat_rebinding_original) && same(start_alone, start_alone_original) && same(keep_hosting, keep_hosting_original) && same(slow_drop_call, slow_drop_original) &&
			      same(unregister_2c, branch_original) && same(unregister_18, branch_original) && same(lobby_tick, branch_original) && same(keep_1f, branch_original),
			      "the replaced bytes are the retail instructions at every site");
		}
	}
	{
		using join_in_progress_core::decide_mesh_reply;
		using join_in_progress_core::mesh_reply;
		// Four players: the newest joiner completes both meshes at once; "being added" is the last one completed.
		check(decide_mesh_reply(false, true, true) == mesh_reply::refused,
		      "a mesh reply from an unbound participant never rebinds a record that has its own peer (Q1a: two records on one peer)");
		check(decide_mesh_reply(true, true, false) == mesh_reply::already_bound && decide_mesh_reply(true, true, true) == mesh_reply::already_bound,
		      "a mesh reply from a peer a record already holds leaves every binding as it is (and frees nothing)");
		check(decide_mesh_reply(false, true, false) == mesh_reply::stock && decide_mesh_reply(false, false, false) == mesh_reply::stock,
		      "a record still without a peer, or no record being added, keeps the stock completion");
		check(std::any_of(join_in_progress_rules::guards.begin(), join_in_progress_rules::guards.end(), [](const auto& g) {
			return g.va <= join_in_progress_rules::mesh_reply_call && g.va + g.hex.size() / 2 >= join_in_progress_rules::mesh_reply_call + 5;
		}) && std::any_of(join_in_progress_rules::guards.begin(), join_in_progress_rules::guards.end(), [](const auto& g) {
			return g.va <= join_in_progress_rules::loaded_flag_call && g.va + g.hex.size() / 2 >= join_in_progress_rules::loaded_flag_call + 5;
		}), "the 0x25 completion call and the 0x5b flag call are guarded retail bytes");
	}
	{
		// AE: the canonical cohort restart decisions (join_in_progress_core::restart_reset).
		restart_reset r;
		check(!r.should_restore(true) && !r.should_restore(false), "AE: no pristine camera image - no restore, whatever the epoch");
		r.snapshot = true;
		check(r.should_restore(true) && !r.should_restore(false), "AE: the camera is restored only for a cohort restart");
		check(!r.should_reseed(0), "AE: an unarmed first frame never reseeds (ordinary play, lobby START)");
		r.arm();
		check(!r.should_reseed(5) && r.armed, "AE: armed but not frame 0 (the old world still ticking): no reseed, stays armed");
		check(r.should_reseed(0) && !r.armed && r.reseeds == 1, "AE: frame 0 of the restarted epoch reseeds exactly once");
		check(!r.should_reseed(0), "AE: a repeated frame 0 (arbitration retry) cannot reseed twice");
		r.arm(); r.disarm();
		check(!r.should_reseed(0), "AE: a session that ends before its first frame disarms");
		check(snapshot_wanted(false) && !snapshot_wanted(true), "AE: the pristine image is taken only when the camera is not yet constructed");
		// The 0x47 prepare marker: a lobby START (host outside the epoch) must never make the guests restore/reseed alone.
		std::uint8_t body[8]{2,0,0,0, 0, 0, 0,0};
		check(prepare_stamp(false) == 0 && prepare_stamp(true) == late_restart_marker, "AE: the host stamps the marker only for a late-join restart");
		check(!prepare_is_late_restart(body, 8), "AE: a lobby START prepare (zero tail bytes) is not a late restart");
		std::memcpy(body + prepare_marker_at, &late_restart_marker, 2);
		check(prepare_is_late_restart(body, 8), "AE: the stamped prepare is a late restart");
		check(!prepare_is_late_restart(body, 6) && !prepare_is_late_restart(nullptr, 8), "AE: a short or missing body is never a late restart");
		body[6] ^= 1; check(!prepare_is_late_restart(body, 8), "AE: residue one bit off the marker is not a late restart");
	}
	{
		// AE failing-first evidence: live-060y run 1 (E1, one PC) and run 4 (E1, two PCs) first periods, as logged by
		// AC's producer observer. The old restart must be reported as disagreeing on exactly the camera and RNG terms,
		// with every hero transform equal; run 4 frame 30 has EQUAL hashes and must still disagree.
		auto sample = [](std::uint16_t frame, std::uint32_t hero_sum, std::uint32_t camera, std::uint32_t rng, std::uint32_t hash,
		                 std::array<std::array<std::uint32_t, 6>, 4> t) {
			component_sample s; s.frame = frame; s.heroes = 4; s.hero_sum = hero_sum; s.camera = camera; s.rng_draws = rng; s.hash = hash; s.produced = true;
			for (unsigned i = 0; i < 4; ++i) { s.transform[i] = t[i]; s.have[i] = true; }
			return s;
		};
		const std::array<std::array<std::uint32_t, 6>, 4> e1{{{0x44FA6DA5,0x44CF125B,0x3F8117A5,0,0,0x4096CBE4},{0x44FA6DA5,0x44D9ADA5,0x3F8117A5,0,0,0x4096CBE4},
			{0x44EFD25B,0x44D9ADA5,0x3F8117A5,0,0,0x4096CBE4},{0x44EFD25B,0x44CF125B,0x3F8117A5,0,0,0x4096CBE4}}};
		const auto run1_host = sample(30, 0x08BD60DC, 0x1FA6, 195, 0x08BD8145, e1), run1_guest = sample(30, 0x08BD60DC, 0x1F60, 272, 0x08BD814C, e1);
		check(reconciles(run1_host) && reconciles(run1_guest), "AE: live-060y run 1 samples reconcile (hash = hero_sum + camera + rng)");
		check(compare_components(run1_host, run1_guest) == (camera_differs | rng_differs), "AE: run 1 frame 30 disagrees on camera and RNG only (heroes equal)");
		const auto run4_host = sample(30, 0x08BD60DC, 0x1FA6, 194, 0x08BD8144, e1), run4_guest = sample(30, 0x08BD60DC, 0x1F60, 264, 0x08BD8144, e1);
		check(run4_host.hash == run4_guest.hash && compare_components(run4_host, run4_guest) == (camera_differs | rng_differs),
		      "AE: run 4 frame 30 (two PCs): equal hashes by cancellation still disagree on camera and RNG");
		const auto run4_host60 = sample(60, 0x08BD60DC, 0x1FA7, 256, 0x08BD8183, e1), run4_guest60 = sample(60, 0x08BD60DC, 0x1F60, 341, 0x08BD8191, e1);
		check(reconciles(run4_host60) && reconciles(run4_guest60) && compare_components(run4_host60, run4_guest60) == (camera_differs | rng_differs),
		      "AE: run 4 frame 60 disagrees on the same two terms (the hashes break)");
		std::array<std::array<std::uint32_t, 6>, 4> e2h = e1, e2g = e1;
		e2h[3] = {0x44EF325B,0x44BF525B,0x3F811165,0,0,0x4096CBE4}; e2g[3] = {0x44F0C5F6,0x44BFF47D,0x3F782773,0,0,0x4096CBE4};
		const auto run2_host = sample(30, 0x086D6C14, 0x1E8E, 202, 0x086D8B6C, e2h), run2_guest = sample(30, 0x08701D26, 0x1E9A, 206, 0x08703C8E, e2g);
		check(compare_components(run2_host, run2_guest) == (hero_differs | camera_differs | rng_differs), "AE: run 2 (E2) also disagrees on the AI hero in slot 3");
		auto canonical_guest = run1_host; // what the canonical restart must produce: every term equal
		check(compare_components(run1_host, canonical_guest) == agree, "AE: identical producer inputs agree");
		component_sample missing; check(compare_components(run1_host, missing) & incomplete, "AE: a missing sample is reported, not treated as agreement");
		auto other_frame = run1_host; other_frame.frame = 60;
		check(compare_components(run1_host, other_frame) & frame_differs, "AE: samples pair by native frame");
	}
	{
		// AE: the restart sites are guarded retail bytes.
		using namespace join_in_progress_rules;
		auto covered = [](std::uint32_t va, std::size_t n) {
			return std::any_of(guards.begin(), guards.end(), [&](const auto& g) { return g.va <= va && g.va + g.hex.size() / 2 >= va + n; });
		};
		check(covered(camera_construct_call, 5) && covered(frame_gate_run_call, 5) && covered(camera_constructor, 8) && covered(seed_setter, 8),
		      "AE: camera construction call, frame-gate input run, constructor and seed setter are guarded");
		check(camera_object + camera_bytes == camera_guard_byte, "AE: the pristine image spans exactly the singleton (up to its construction guard byte)");
	}
	{
		// AE state-machine edges: a second joiner during a bring-in, a joiner leaving mid-transfer.
		machine m; m.started = true;
		check(m.defer_add(&r1, 1, false, 0), "edge: first newcomer pending");
		auto& s = *m.find(&r1);
		m.step(s, seen(true, false, false, false, false), 10); m.step(s, seen(true, false, false, false, true), 20);
		check(s.at == stage::streaming && m.holding(), "edge: the first newcomer's image is streaming");
		check(m.defer_add(&r2, 2, false, 30) && m.pending(&r2), "edge: a second joiner arriving mid-transfer is pending, not added");
		auto& t = *m.find(&r2);
		check(m.step(t, seen(false, false, false, false, true), 40) == action::none && t.at == stage::pending, "edge: the second joiner waits for the NEXT zone load");
		m.gone(&r1);
		check(!m.find(&r1) && !m.holding() && m.pending(&r2), "edge: the streaming joiner left - its seat is free, nothing held, the second still waits");
		check(m.defer_add(&r3, 3, false, 50), "edge: a replacement can take the freed seat");
	}
	{
		// latejoin-residue P1/P2/P3 decisions.
		restart_reset r;
		check(!r.should_reseed_segment(), "P1: no segment epoch unless the cohort load armed the restart");
		r.arm();
		check(r.should_reseed_segment() && r.should_reseed_segment() && r.segment_reseeds == 2, "P1: LoadMap entry and the zone start each take an epoch while armed");
		check(r.should_reseed(0) && !r.should_reseed_segment(), "P1: after frame 0 took the final epoch no segment epoch follows");
		check(brain_ring_reset_wanted(0, 32, 32) && brain_ring_reset_wanted(0, 31, 32) && !brain_ring_reset_wanted(2, 32, 32) && !brain_ring_reset_wanted(0, 0, 0),
		      "P2: the brain ring is made canonical on the stock pool (follow-up 4: also with held slots), never on a relocated pool");
		const std::uint32_t direct[4]{0x4990a5, 1, 2, 3}, wrapped_float[4]{0x5601bd, 0x4870c5, 2, 3}, wrapped_int[4]{0x5601fb, 0, 0, 0x48aea7}, wrapped_int_b[4]{0x56023f, 0, 0, 0x46e605};
		check(normalise_draw_caller(direct, 0x5601bd, 0x5601fb, 0x56023f) == 0x4990a5, "P3: a tail-jumped bool draw names its caller directly");
		check(normalise_draw_caller(wrapped_float, 0x5601bd, 0x5601fb, 0x56023f) == 0x4870c5, "P3: the float wrapper's caller is one frame up");
		check(normalise_draw_caller(wrapped_int, 0x5601bd, 0x5601fb, 0x56023f) == 0x48aea7 && normalise_draw_caller(wrapped_int_b, 0x5601bd, 0x5601fb, 0x56023f) == 0x46e605,
		      "P3: the int wrapper's caller is three frames up (esi/edi pushed)");
		auto* trace = new draw_trace();
		for (unsigned i = 0; i < draw_ring_size + 10; ++i) trace->note(i, i, std::uint16_t(i));
		check(trace->pending() == draw_ring_size && trace->overflow == 10 && trace->ring[trace->flushed & (draw_ring_size - 1)].caller == 10,
		      "P3: the draw ring is bounded; the oldest unflushed entries are dropped and counted");
		delete trace;
		auto guarded = [](std::uint32_t va) { return std::any_of(join_in_progress_rules::guards.begin(), join_in_progress_rules::guards.end(), [&](const auto& g) { return g.va == va; }); };
		check(guarded(0x484101) && guarded(0x4867f8) && guarded(0x522f70) && guarded(0x560090), "P1-P3: the zone-load sites, the ring init and the draw entry are guarded");
	}
	{
		check(camera_anchor_from_party(true, true, true) && !camera_anchor_from_party(true, false, true) && !camera_anchor_from_party(true, true, false) && !camera_anchor_from_party(false, true, true),
		      "follow-up 3: the zone-start camera anchors on the party list only during an armed late restart with the reset on");
		check(std::any_of(join_in_progress_rules::guards.begin(), join_in_progress_rules::guards.end(), [](const auto& g) { return g.va == 0x4868f4; }) &&
		      std::any_of(join_in_progress_rules::guards.begin(), join_in_progress_rules::guards.end(), [](const auto& g) { return g.va == 0x486968; }),
		      "follow-up 3: the anchor table load and the camera vt+0x74 call are guarded");
	}
	{
		// follow-up 5: the dormant-entity respawn hold (CSmartEntMgr update) and the spawn-entry probe.
		using namespace join_in_progress_rules;
		check(smart_hold_wanted(true, true, true) && !smart_hold_wanted(true, false, true) && !smart_hold_wanted(true, true, false) && !smart_hold_wanted(false, true, true),
		      "follow-up 5: the respawn is held only during an armed late restart with the reset on (a lobby START never arms)");
		auto guarded_span = [](std::uint32_t va, std::size_t n) {
			return std::any_of(guards.begin(), guards.end(), [&](const auto& g) { return g.va <= va && g.va + g.hex.size() / 2 >= va + n; });
		};
		check(guarded_span(smart_update_call, 5) && guarded_span(spawn_entry, spawn_entry_original.size()) && guarded_span(smart_manager_vtable, 32),
		      "follow-up 5: the game update's getter call, the spawn entry prologue and the manager vtable are guarded retail bytes");
		check(smart_update_call + 5 + 0x0004840c == smart_manager_getter, "follow-up 5: the replaced call's rel32 reaches the CSmartEntMgr getter");
		// p060fix/AJ: the first sweep after frame 0 examines every record on every peer (game+0x1f0 := the far marker).
		{
			std::uint32_t anchor[3] = {0x44f6d70fu, 0x44f3b1ecu, 0x43aa3853u};
			check(!respawn_sweep_marker(anchor) && anchor[0] == respawn_far_marker_bits && anchor[1] == 0 && anchor[2] == 0, "AJ: a camera eye in game+0x1f0 becomes the far marker (1e30, 0, 0) and is reported as an eye");
			check(respawn_sweep_marker(anchor) && anchor[0] == respawn_far_marker_bits, "AJ: the marker already there is kept and reported as such");
			std::uint32_t partial[3] = {respawn_far_marker_bits, 0, 0x3f800000u};
			check(!respawn_sweep_marker(partial) && partial[2] == 0, "AJ: a marker with a stale z is not the marker");
			float marker; std::memcpy(&marker, &respawn_far_marker_bits, 4);
			check(marker > 1.0e29f && marker < 1.1e30f && respawn_far_marker_bits == respawn_far_marker, "AJ: the marker is the 1e30 the retail setter 0x4685b0 pushes (rules and core agree)");
			const auto setter = std::find_if(guards.begin(), guards.end(), [](const auto& g) { return g.va == game_respawn_reset; });
			check(setter != guards.end() && setter->hex.find("caf24971") != std::string_view::npos && setter->hex.find("8d8ef0010000") != std::string_view::npos,
			      "AJ: the guarded retail setter bytes carry the marker constant and the +0x1f0 destination");
			check(guarded_span(0x46f2b7, 0x21) && guarded_span(respawn_sweep_threshold, 4) && guarded_span(respawn_marker_call, 12) && 0x46f2b7 + 0x21 == 0x46f2d8,
			      "AJ: the all-flag compare, its 10000.0f threshold and the finish's setter call are guarded, ending where the site guard begins");
			const auto threshold = std::find_if(guards.begin(), guards.end(), [](const auto& g) { return g.va == respawn_sweep_threshold; });
			check(threshold != guards.end() && threshold->hex == "00401c46", "AJ: the threshold constant is 10000.0f (0x461c4000)");
			// AJ2: the periodic event (kind 0x6d5e30) follows the AI bit: 0x71 (released seat) -> clear + drop; 0x51 -> drop only (nothing queued); 0x70 player keeps; 0x50 player without one is reported
			{
				const auto r71 = hero_event_rule(0x71), r51 = hero_event_rule(0x51), r70 = hero_event_rule(0x70), r50 = hero_event_rule(0x50);
				check(r71.clear_pending && r71.drop_events && !r71.missing, "AJ2: a released seat's AI hero (0x71) loses its pending bit and its queued periodic events");
				check(!r51.clear_pending && r51.drop_events && !r51.missing, "AJ2: a never-bound AI hero (0x51) is in the drop set with nothing to clear");
				check(!r70.clear_pending && !r70.drop_events && !r70.missing, "AJ2: a player hero (0x70) keeps its periodic event");
				check(!r50.clear_pending && !r50.drop_events && r50.missing, "AJ2: a player hero without the event (0x50) is only reported");
				check(hero_ai_bit == 0x01 && hero_event_pending_bit == 0x20 && hero_event_kind == 0x6d5e30, "AJ2: the bits and the kind are the retail ones (0x42a460 / 0x42ef30 / 0x41da60)");
				check(guarded_span(hero_event_arm, 18) && guarded_span(hero_event_fire + 0xd, 16) && guarded_span(hero_ai_switch_on + 3, 9), "AJ2: the pending-bit setter, the fire's clear and the AI switch-on are guarded retail bytes");
				const auto arm = std::find_if(guards.begin(), guards.end(), [](const auto& g) { return g.va == hero_event_arm; });
				check(arm != guards.end() && arm->hex.find("a820") != std::string_view::npos && arm->hex.find("0c20") != std::string_view::npos, "AJ2: the setter's bytes test and OR bit 0x20");
				event_entry e{}; e.handle = 0x3404; e.token = true; e.event = hero_event_kind; e.live = true; e.owner = 0x03b118e0;
				const std::uint32_t party[1] = {0x3404};
				check(moved_event_dropped(e, hero_event_kind, party, 1, 0x3ff) && !moved_event_dropped(e, moved_event_kind, party, 1, 0x3ff), "AJ2: the rebuild's per-kind drop matches the periodic kind by the owner's dense index only");
			}
			// AK (ZD-3P-E2p): the NPC move-intent rule - a CActor without a controller that is not a party hero; a positive speed is the re-arm's "every frame"
			{
				const auto goal = npc_move_intent_rule(actor_vtable, actor_vtable, -1, false, 0, std::uint8_t(actor_fast_update_bit), 1.0f, 0.0f, 0);
				check(goal.considered && goal.had_speed && !goal.had_fast_flag && !goal.had_follow, "AK: a controller-less CActor with a positive move speed is an AI actor carrying a goal");
				check(!npc_move_intent_rule(actor_vtable, actor_vtable, 0, false, 0, std::uint8_t(actor_fast_update_bit), 1.0f, 0.0f, 0).considered, "AK: an actor bound to a controller is never touched (the party passes own the heroes)");
				check(!npc_move_intent_rule(actor_vtable, actor_vtable, -1, true, 0, std::uint8_t(actor_fast_update_bit), 1.0f, 0.0f, 0).considered, "AK: a party hero is never touched even without a controller (the empty seat's AI hero)");
				check(!npc_move_intent_rule(0x12345678, actor_vtable, -1, false, 1, std::uint8_t(actor_fast_update_bit), 1.0f, 1.0f, 1).considered, "AK: an entity with another vtable is never touched");
				const auto idle = npc_move_intent_rule(actor_vtable, actor_vtable, -1, false, 0x01, std::uint8_t(actor_fast_update_bit), 0.0f, -1.0f, 0x1917);
				check(idle.considered && !idle.had_speed && idle.had_fast_flag && idle.had_follow, "AK: zero or negative speeds are no goal (the tail's fcomp-with-0 test); the +6 flag and a follow handle are counted only");
				check(guarded_span(actor_update_rearm_tail, actor_update_rearm_tail_bytes) && guarded_span(actor_move_goal_clear + 0x20, 0x45) && actor_update_rearm_tail + actor_update_rearm_tail_bytes == 0x43500c,
				      "AK: the re-arm tail (0x434fc3..0x43500c) and the clear's body (0x421bb0..0x421bf5) are guarded retail bytes");
				const auto tail = std::find_if(guards.begin(), guards.end(), [](const auto& g) { return g.va == actor_update_rearm_tail; });
				check(tail != guards.end() && tail->hex.rfind("f6450601", 0) == 0 && tail->hex.find("d98580010000") != std::string_view::npos && tail->hex.find("d98584010000") != std::string_view::npos && tail->hex.find("cdcccc3d") != std::string_view::npos,
				      "AK: the tail tests +6 bit 0, then +0x180 and +0x184 against 0.0, and re-arms 0.1 s (0x3dcccccd)");
				const auto clear = std::find_if(guards.begin(), guards.end(), [](const auto& g) { return g.va == actor_move_goal_clear + 0x20; });
				check(clear != guards.end() && clear->hex.find("8d8ed0010000") != std::string_view::npos && clear->hex.find("8d8edc010000") != std::string_view::npos && clear->hex.find("898680010000898684010000") != std::string_view::npos,
				      "AK: the clear writes the goal +0x1d0, the start +0x1dc and zeroes +0x180 / +0x184");
				check(actor_move_speed_a_at == 0x180 && actor_move_speed_b_at == 0x184 && actor_move_goal_at == 0x1d0 && actor_move_start_at == 0x1dc && actor_follow_handle_at == 0x3ec && actor_vtable == 0x682ccc && world_entity_by_index_slot == 0x10 && move_goal_none_bits == 0x00800000,
				      "AK: the offsets, the CActor vtable, the world slot and FLT_MIN are the retail ones");
				check(entity_flags_at == 4 && entity_flag_bytes == 16 && entity_registered_at == 0x6c && entity_registered_bit == 1 && world_flag_update_slot == 0x3c && world_flag_members_slot == 0x40 && world_flag_record_slot == 0x44 && entity_active_flag == 16,
				      "AM: notification uses the registered bit and installed CWorld flag interfaces");
				check(visibility_cache_clear_slot == 0x38 && guarded_span(0x464330, 0x40), "AM/ZF: complete callback-free grid cache clear is guarded");
				check(guarded_span(0x686c8c, 4), "AM/ZF: grid cache-clear virtual slot is guarded");
				check(guarded_span(0x45fe40, 0x1f) && guarded_span(0x4c6990, 0x1cc) && guarded_span(0x4015d0, 0x23) && guarded_span(0x68f848, 12),
				      "AM: full flag notification, full cache/bitmap update, copy helper and virtual slots are guarded");
			}
			check(image_rides_running_transfer(true, true) && !image_rides_running_transfer(true, false) && !image_rides_running_transfer(false, true) && !image_rides_running_transfer(false, false),
			      "AJ: a second newcomer rides the running transfer only inside this late restart (a lobby START's sender still refuses)");
			// 2..4 peers, 1..3 newcomers: the host machine seats every newcomer admitted at one zone load in the same step
			machine m; m.started = true;
			int recs[3] = {1, 2, 3};
			for (int k = 0; k < 3; ++k) check(m.defer_add(&recs[k], k + 1, false, 1000u), "AJ: a newcomer is seated pending");
			observation o; o.zone_load_pending = true;
			unsigned adds = 0; for (auto& s : m.seats) if (s.at != stage::free && m.step(s, o, 1000u) == action::run_add) ++adds;
			check(adds == 3 && m.holding(), "AJ: three pending newcomers all run their native add at the one zone load");
			o.zone_load_pending = false; o.add_waiting = false; o.host_loaded = true;
			unsigned images = 0; for (auto& s : m.seats) if (s.at == stage::adding && m.step(s, o, 1100u) == action::send_image) ++images;
			check(images == 3, "AJ: all three step to streaming in the same advance (the first starts the sender, the others ride it)");
			o.image_done = true;
			unsigned starts = 0; for (auto& s : m.seats) if (s.at == stage::streaming && m.step(s, o, 1200u) == action::send_start) ++starts;
			o.loaded = true;
			unsigned finishes = 0; for (auto& s : m.seats) if (s.at == stage::starting && m.step(s, o, 1300u) == action::finish) ++finishes;
			check(starts == 3 && finishes == 3 && !m.holding(), "AJ: every newcomer finishes at the shared start; the host's cohort sync waits for all of them (holding() false only then)");
		}
		check(spawn_resume == spawn_entry + spawn_entry_original.size() && spawn_entry_original[4] == 0x33 && spawn_entry_original[5] == 0xf6,
		      "follow-up 5: the displaced prologue ends on an instruction boundary (xor esi,esi) and resumes right after it");
		// follow-up 6: the per-actor deciders the draw trace names (AI think branch: EDI; touch nudge: ESI), nothing else.
		check(draw_actor_of(0x51e3c6) == draw_actor_register::edi && draw_actor_of(0x51e3e0) == draw_actor_register::edi &&
		      draw_actor_of(0x42974d) == draw_actor_register::esi && draw_actor_of(0x429773) == draw_actor_register::esi &&
		      draw_actor_of(0x423698) == draw_actor_register::none && draw_actor_of(0x4ee0f4) == draw_actor_register::none,
		      "follow-up 6: the draw trace names the actor only for the AI think branch and the touch nudge");
		auto* t = new draw_trace(); t->note(0x51e3c6, 1, 2, 0x3804u, 0xffffffffu); t->note(0x4204a6, 2, 2);
		check(t->ring[0].a == 0x3804u && t->ring[0].b == 0xffffffffu && t->ring[1].a == 0 && t->ring[1].b == 0, "follow-up 6: a draw entry carries the actor's handle and controller when given, 0 otherwise");
		delete t;
	}
	{
		std::array<std::uint32_t, 32> ring{}; std::int32_t w = 7, r = 11, f = 28;
		check(rebuild_brain_ring(0x80000007u, ring, w, r, f) == 4 && w == 28 && r == 0 && f == 28 && ring[0] == 3 && ring[27] == 30 && ring[28] == 0,
		      "follow-up 4: the brain ring is rebuilt over the free slots in ascending order (held slots kept out)");
		check(rebuild_brain_ring(0u, ring, w, r, f) == 0 && w == 0 && r == 0 && f == 32 && ring[31] == 31, "follow-up 4: with every brain free the rebuilt ring is identity");
		check(brain_ring_reset_wanted(0, 28, 32) && !brain_ring_reset_wanted(2, 32, 32), "follow-up 4: the rebuild runs on the stock pool whatever is held, not on a layout it does not know");
		{
			// p060fix/AP: the ring rebuild resets the consumers' per-slot state of exactly the free slots.
			const std::array<std::uint32_t, 3> held{0x80000007u, 0x1u, 0u};
			const std::span<const std::uint32_t> one(held.data(), 1), three(held.data(), 3);
			unsigned free32 = 0, free96 = 0;
			for (std::uint32_t i = 0; i < 32; ++i) free32 += brain_slot_held(one, i) ? 0u : 1u;
			for (std::uint32_t i = 0; i < 96; ++i) free96 += brain_slot_held(three, i) ? 0u : 1u;
			check(free32 == 28 && brain_slot_held(one, 0) && brain_slot_held(one, 31) && !brain_slot_held(one, 3) && brain_slot_held(one, 32) && brain_slot_held(one, 95)
			      && free96 == 91 && brain_slot_held(three, 32) && !brain_slot_held(three, 33) && !brain_slot_held(three, 95),
			      "AP: brain_slot_held reads the held words (0,1,2,31 / 32 held; a slot past the words counts as held, so it is never reset)");
		}
		// p060fix/AI S2: the rebuild also runs on the stock pool at stage 1 (the standalone ActorPools=124 install) and on
		// E4's 96-slot pool at stage 2; any other stage/count pairing is left alone.
		check(brain_ring_reset_wanted(1, 28, 32) && brain_ring_reset_wanted(2, 91, 96) && !brain_ring_reset_wanted(1, 32, 96) && !brain_ring_reset_wanted(0, 32, 96) && !brain_ring_reset_wanted(3, 96, 96),
		      "AI: the rebuild runs at stage 0/1 with 32 controllers and at stage 2 with 96, nowhere else");
		{
			const std::array<std::uint32_t, 3> held{0x7u, 0x100u, 0x80000000u}; std::array<std::uint32_t, 96> ring96{}; w = 17; r = 23; f = 91;
			const auto kept = rebuild_brain_ring(std::span<const std::uint32_t>(held), std::span<std::uint32_t>(ring96), w, r, f);
			bool ascending = true; unsigned expect = 3;
			for (unsigned i = 0; i < 91; ++i) { if (expect == 40) ++expect; ascending &= ring96[i] == expect; ++expect; }
			check(kept == 5 && w == 91 && r == 0 && f == 91 && ascending && ring96[91] == 0 && ring96[95] == 0 && brain_ring_held_words(96) == 3 && brain_ring_held_words(32) == 1,
			      "AI: the 96-slot ring is rebuilt over the free slots in ascending order across the three held words (0,1,2 / 40 / 95 kept out)");
			// The stage-2 ring layout the rebuild writes equals the E4 ledger's operands at the alloc/init/release sites
			// (e4_core_sites.hpp rows), and the stock layout equals those rows' retail operands.
			const actor_ai::descriptor e4{2, 0x30000000, 0x30000004, 0x61c, 96, 0x30024db8, 0x268, 96, 2, 0x24db0, 0x525302};
			const auto row = [](std::uint32_t va) { for (const auto& e : e4_core_sites::edits) if (e.va == va) return &e; return static_cast<const e4_core_sites::edit*>(nullptr); };
			const auto operand = [](const e4_core_sites::edit* e) { std::uint32_t v = 0; for (unsigned i = 0; i < e->size; ++i) v |= std::uint32_t(std::strtoul(std::string(e->hex + 2 * (e->offset + i), 2).c_str(), nullptr, 16)) << (8 * i); return v; };
			const auto layout = [&](std::uint32_t va, std::uint32_t (*field)(const actor_ai::descriptor&)) { const auto* e = row(va); return e && !e->dynamic && e->size == 4 && e->value == field(e4) && operand(e) == field(actor_ai::stock); };
			check(layout(0x522a12, actor_ai::ring_read) && layout(0x522a18, actor_ai::ring) && layout(0x522a25, actor_ai::ring_held) && layout(0x522a59, actor_ai::ring_free) && layout(0x522f74, actor_ai::ring_write) &&
			      row(0x522a44) && row(0x522a44)->value == 96 && operand(row(0x522a44)) == 32,
			      "AI: the stage-2 ring/read/write/free/held offsets are the E4 ledger's operands at 0x522a12/18/25/59 and 0x522f74 (stock = their retail operands), capacity 32 -> 96");
		}
		check(std::any_of(join_in_progress_rules::guards.begin(), join_in_progress_rules::guards.end(), [](const auto& g) { return g.va == 0x484069; }), "follow-up 4: the LoadZone site is guarded");
	}
	{
		// Follow-up 7: the timed-event queue's canonical order and drop rule.
		auto entry = [](float time, std::uint32_t handle, std::uint32_t owner, std::uint32_t event, std::uint8_t tag) {
			event_entry e{}; e.time = time; e.handle = handle; e.owner = owner; e.event = event; e.payload[0] = tag;
			e.token = positive_entity_token(handle); e.live = owner != 0; return e; };
		const auto host_hero = entry(2.0f, 0x3801, 0x10000, 0x10320, 0), joiner_hero = entry(2.0f, 0x1801, 0x10000, 0x10320, 0);
		const auto other = entry(2.0f, 0x3802, 0x20000, 0x20320, 0), later = entry(2.5f, 0x3801, 0x10000, 0x10320, 0);
		const auto second_event = entry(2.0f, 0x3801, 0x10000, 0x10328, 0), payload_b = entry(2.0f, 0x3801, 0x10000, 0x10320, 1);
		const auto dead = entry(2.0f, 0x35ce, 0, 0x12345678, 0), non_token = entry(2.0f, 0, 0, 0x6d55fc, 0), negative = entry(2.0f, 0x80000001u, 0, 0x6d5604, 0);
		check(!canonical_event_less(host_hero, joiner_hero, 0x3ff) && !canonical_event_less(joiner_hero, host_hero, 0x3ff), "follow-up 7: the owner's generation never orders (warm host 0x3801 = cold joiner 0x1801)");
		check(canonical_event_less(host_hero, other, 0x3ff) && !canonical_event_less(other, host_hero, 0x3ff), "follow-up 7: same time: the lower dense index first");
		check(canonical_event_less(host_hero, later, 0x3ff) && canonical_event_less(other, later, 0x3ff), "follow-up 7: the earlier time first whatever the owner");
		check(canonical_event_less(host_hero, second_event, 0x3ff) && !canonical_event_less(second_event, host_hero, 0x3ff), "follow-up 7: the same owner's events by their offset (+0x320 before +0x328)");
		check(canonical_event_less(host_hero, payload_b, 0x3ff), "follow-up 7: equal otherwise: the payload bytes decide");
		check(canonical_event_less(other, non_token, 0x3ff) && canonical_event_less(non_token, negative, 0x3ff), "follow-up 7: non-token owners sort after every token, by raw handle");
		check(event_entry_dropped(dead) && !event_entry_dropped(host_hero) && !event_entry_dropped(non_token) && !event_entry_dropped(negative), "follow-up 7: only a dead entity token is dropped");
		{
			// Follow-up 9 (round 6): the static "moved" kind 0x6d7c5c scheduled on the host's actor (pool at one address) and
			// on the joiner's (another) must have one key; a member event keys by its offset and sorts before any static kind.
			event_entry hk{}, jk{}, mem{}, dk{};
			hk.time = jk.time = mem.time = dk.time = 2.0f; hk.token = jk.token = mem.token = dk.token = true; hk.live = jk.live = mem.live = true;
			hk.handle = 0x3801; hk.owner = 0x03290008; hk.event = 0x6d7c5c;
			jk.handle = 0x1801; jk.owner = 0x032d0008; jk.event = 0x6d7c5c;
			mem.handle = 0x1801; mem.owner = 0x032d0008; mem.event = 0x032d0008 + 0x320;
			dk.handle = 0x35ce; dk.owner = 0; dk.event = 0x6d7c5c; // a dead owner: the raw event address
			check(event_kind_key(hk) == 0x6d7c5c && event_kind_key(jk) == 0x6d7c5c && event_kind_key(dk) == 0x6d7c5c, "follow-up 9: a static kind's key is its address in every process whatever the owner's address");
			check(event_kind_key(mem) == 0x320 && canonical_event_less(mem, jk, 0x3ff) && !canonical_event_less(jk, mem, 0x3ff), "follow-up 9: a member event (its offset) sorts before a static kind on the same owner");
			check(!canonical_event_less(hk, jk, 0x3ff) && !canonical_event_less(jk, hk, 0x3ff), "follow-up 9: the same kind on the same slot is equal across processes");
			check(cohort_sync_clears_late_restart(false) && !cohort_sync_clears_late_restart(true), "follow-up 9: cohort synchronized ends the late restart only when no frame-0 epoch is still armed");
			// Follow-up 10 (round 7): the pump's resume after the world restore is the ONLY controls resume of an existing member
			// (AE10's zone-start resume consumed it and the host never produced its frame-0 input); it happens once, after the
			// epoch saw the loading flags, and never before.
			cohort ep{}; ep.begin(); ep.start();
			check(!ep.resume_controls(true), "follow-up 10: no resume before the epoch has seen the world loading");
			ep.observe(true, false);
			check(!ep.resume_controls(false) && ep.resume_controls(true) && !ep.resume_controls(true), "follow-up 10: the pump resumes once, when the world is no longer loading");
		}
		check(event_queue_rebuild_wanted(true, true, true) && !event_queue_rebuild_wanted(true, false, true) && !event_queue_rebuild_wanted(true, true, false) && !event_queue_rebuild_wanted(false, true, true), "follow-up 7: the rebuild runs only in an armed late restart with the reset on");
		check(join_in_progress_rules::event_body_at + join_in_progress_rules::event_heap_at + 8 * join_in_progress_rules::event_records <= join_in_progress_rules::event_body_at + join_in_progress_rules::event_heap_count_at
		      && join_in_progress_rules::event_record_bytes * join_in_progress_rules::event_records == join_in_progress_rules::event_used_bitmap_at, "follow-up 7: the manager layout is self-consistent (1125 records of 24 bytes, 1125 heap pairs before the count)");
		for (const auto va : {0x4b2fc0u, 0x68de14u, 0x4b2d70u, 0x4b2c10u, 0x4b2b80u, 0x4b2c90u, 0x4b2a10u, 0x41a76bu, 0x41e440u, 0x4b2de0u})
			check(std::any_of(join_in_progress_rules::guards.begin(), join_in_progress_rules::guards.end(), [va](const auto& g) { return g.va == va; }), "follow-up 7: every timed-event site is guarded");
		check(event_pump_slot_accepted(0x4b2d60, 0x4b2d60, 0) && event_pump_slot_accepted(0x4b2d60, 0x4b2d60, 0x50001000) && event_pump_slot_accepted(0x50001000, 0x4b2d60, 0x50001000)
		      && !event_pump_slot_accepted(0x50001000, 0x4b2d60, 0) && !event_pump_slot_accepted(0x12345678, 0x4b2d60, 0x50001000) && !event_pump_slot_accepted(0, 0x4b2d60, 0),
		      "follow-up 8: the pump slot composes with the xml1 turrets hook only (retail, or the module's reported hook)");
		check(std::none_of(join_in_progress_rules::guards.begin(), join_in_progress_rules::guards.end(), [](const auto& g) { return g.va <= 0x68de20 && 0x68de20 < g.va + g.hex.size() / 2; }), "follow-up 8: no retail guard covers the hooked pump slot 0x68de20");
		{
			// Follow-up 12 (round 8): a party hero's queued "moved" event (static kind 0x6d7c5c) is dropped by dense index
			// whatever its generation; another owner's moved event, a member event of a party hero and a non-token owner stay.
			event_entry mv_joiner{}, mv_host{}, mv_other{}, member{}, non_token{};
			mv_joiner.time = mv_host.time = mv_other.time = member.time = non_token.time = 2.0f;
			mv_joiner.token = mv_host.token = mv_other.token = member.token = true; mv_joiner.live = mv_host.live = mv_other.live = member.live = true;
			mv_joiner.handle = 0x1802; mv_joiner.owner = 0x032d0010; mv_joiner.event = 0x6d7c5c;
			mv_host.handle = 0x3802; mv_host.owner = 0x03290010; mv_host.event = 0x6d7c5c;
			mv_other.handle = 0x1805; mv_other.owner = 0x032d0040; mv_other.event = 0x6d7c5c;
			member.handle = 0x1802; member.owner = 0x032d0010; member.event = 0x032d0010 + 0x320;
			non_token.handle = 0; non_token.owner = 0; non_token.event = 0x6d7c5c;
			const std::uint32_t party[4] = {0x3801, 0x3802, 0x3803, 0x3804};
			check(moved_event_dropped(mv_joiner, 0x6d7c5c, party, 4, 0x3ff) && moved_event_dropped(mv_host, 0x6d7c5c, party, 4, 0x3ff), "follow-up 12: a party hero's moved event is dropped on both peers whatever its generation");
			check(!moved_event_dropped(mv_other, 0x6d7c5c, party, 4, 0x3ff) && !moved_event_dropped(member, 0x6d7c5c, party, 4, 0x3ff) && !moved_event_dropped(non_token, 0x6d7c5c, party, 4, 0x3ff), "follow-up 12: another owner's moved event, a party hero's member event and a non-token owner are kept");
			check(!moved_event_dropped(mv_joiner, 0x6d7c5c, party, 0, 0x3ff), "follow-up 12: no party (the canonicalisation did not run): nothing dropped");
			for (const auto va : {0x471740u, 0x47178cu, 0x4729b0u})
				check(std::any_of(join_in_progress_rules::guards.begin(), join_in_progress_rules::guards.end(), [va](const auto& g) { return g.va == va; }), "follow-up 12: the moved check, its schedule and the fire are guarded");
			check(join_in_progress_rules::actor_last_position_at + join_in_progress_rules::actor_vector_bytes == join_in_progress_rules::actor_last_orientation_at
			      && join_in_progress_rules::actor_position_at + join_in_progress_rules::actor_vector_bytes == join_in_progress_rules::actor_orientation_at, "follow-up 12: the last-known vectors are adjacent 12-byte copies of position/orientation");
		}
	}
	{
		// Follow-up 14 (AE15). (1) The camera's derived spans: inside the object, dword-aligned, non-overlapping, and
		// never the fields the zone path configures (mode +0x84, flags +0x4d8, pitch +0x2f8, magnet count +0x428); the
		// pure restore copies exactly those spans and counts the dwords it changed.
		using namespace join_in_progress_rules;
		bool spans_ok = true; std::uint32_t total = 0;
		for (std::size_t i = 0; i < camera_derived_spans.size(); ++i)
		{
			const auto& x = camera_derived_spans[i];
			spans_ok &= x.bytes != 0 && x.at % 4 == 0 && x.bytes % 4 == 0 && x.at + x.bytes <= camera_bytes;
			for (std::size_t j = i + 1; j < camera_derived_spans.size(); ++j) spans_ok &= x.at + x.bytes <= camera_derived_spans[j].at || camera_derived_spans[j].at + camera_derived_spans[j].bytes <= x.at;
			for (const std::uint32_t keep : {camera_mode_at, camera_flags_at, 0x2f8u, 0x428u, 0x154u, 0x0u}) spans_ok &= !(x.at <= keep && keep < x.at + x.bytes);
			total += x.bytes;
		}
		check(spans_ok && total == 0x1cc, "follow-up 14 / AE16: the derived camera spans are aligned, disjoint, inside the object and exclude the configured fields (0x1cc bytes)");
		check(std::any_of(camera_derived_spans.begin(), camera_derived_spans.end(), [](const auto& x) { return x.at <= camera_basis_at && camera_basis_at + camera_basis_bytes <= x.at + x.bytes && x.at + x.bytes >= 0x2f8; }),
		      "AE16: the nine camera basis dwords +0x2cc..+0x2f0 and the yaw history up to +0x2f8 lie in ONE span");
		check(actor_gravity_hold_at == 0x2f0 && actor_last_fall_at == 0x28c && actor_last_update_at == 0x150 && actor_velocity_at + 0x18 == actor_velocity_prev_at && actor_angular_at == actor_velocity_at + 0xc && actor_angular_prev_at == actor_velocity_prev_at + 0xc,
		      "AE16: the motion fields are the physics step's stamps and the two velocity pairs");
		std::array<std::uint8_t, camera_bytes> cam{}, pristine{};
		for (std::uint32_t i = 0; i < camera_bytes; ++i) { cam[i] = std::uint8_t(i * 7); pristine[i] = std::uint8_t(i * 3 + 1); }
		auto before = cam;
		const unsigned changed = restore_camera_spans(cam.data(), pristine.data(), camera_derived_spans, camera_bytes);
		bool restored = true, untouched = true;
		for (std::uint32_t i = 0; i < camera_bytes; ++i)
		{
			bool in = false;
			for (const auto& x : camera_derived_spans) in |= x.at <= i && i < x.at + x.bytes;
			if (in) restored &= cam[i] == pristine[i]; else untouched &= cam[i] == before[i];
		}
		check(restored && untouched && changed == total / 4, "follow-up 14: the restore copies exactly the derived spans from the pristine image and counts every changed dword");
		check(restore_camera_spans(cam.data(), pristine.data(), camera_derived_spans, camera_bytes) == 0, "follow-up 14: a second restore changes nothing");
		check(camera_yaw_at == 0x74 && std::any_of(camera_derived_spans.begin(), camera_derived_spans.end(), [](const auto& x) { return x.at <= 0x74u && 0x74u < x.at + x.bytes; })
		      && std::any_of(camera_derived_spans.begin(), camera_derived_spans.end(), [](const auto& x) { return x.at <= camera_yaw_history_at && camera_yaw_history_at + 8 <= x.at + x.bytes; })
		      && std::any_of(camera_derived_spans.begin(), camera_derived_spans.end(), [](const auto& x) { return x.at <= 0x104u && 0x104u + 12 <= x.at + x.bytes; })
		      && std::any_of(camera_derived_spans.begin(), camera_derived_spans.end(), [](const auto& x) { return x.at <= 0xe0u && 0xe0u + 12 <= x.at + x.bytes; }),
		      "follow-up 14: the current yaw, the yaw history, the eye +0x104 and the target +0xe0 are derived spans");
		// (2) The attach suppression and the party test.
		check(attach_suppressed(true, true, true) && !attach_suppressed(false, true, true) && !attach_suppressed(true, false, true) && !attach_suppressed(true, true, false),
		      "follow-up 14: a party hero's ground attach callback is suppressed only in an armed late restart with the reset on");
		const std::uint32_t party[4] = {0x3801, 0x3802, 0, 0x3804};
		check(party_member(0x1802, party, 4, 0x3ff) && party_member(0x3804, party, 4, 0x3ff) && !party_member(0x1803, party, 4, 0x3ff) && !party_member(0, party, 4, 0x3ff) && !party_member(0x80000002u, party, 4, 0x3ff) && !party_member(0x1802, party, 0, 0x3ff),
		      "follow-up 14: party membership is by dense index whatever the generation; 0, a negative token, an empty slot and an empty party are not members");
		check(attach_override_original.size() == 8 && attach_override_resume == attach_override + 8 && attach_override_original[0] == 0x83 && attach_override_original[3] == 0x56 && attach_override_original[4] == 0x8b,
		      "follow-up 14: the displaced OnAttached prologue is sub esp,0x18 / push esi / mov esi,[esp+0x20] (8 bytes, position-independent)");
		check(party_bound_call + 5 + 0x0000ea9e == party_bound_target, "follow-up 14: the replaced call at 0x4868ad reaches the retail getter 0x495350");
		for (const auto va : {0x429710u, 0x419f88u, 0x470bb0u, 0x4868a5u})
			check(std::any_of(guards.begin(), guards.end(), [va](const auto& g) { return g.va == va; }), "follow-up 14: the OnAttached override, the set-ground call, FindGround and the party-binding tail are guarded");
		check(std::none_of(guards.begin(), guards.end(), [](const auto& g) { return g.va <= 0x46f0a9u && 0x46f0a9u < g.va + g.hex.size() / 2; }), "follow-up 14: the world-section gate is no longer guarded (the AE12 hold is gone)");
		// (3) Review S2: a heap-allocated kind object is keyed by its vtable, in-image kinds by their address.
		event_entry heap_host{}, heap_guest{}, in_image{}, member{};
		heap_host.time = heap_guest.time = in_image.time = member.time = 2.0f;
		heap_host.token = heap_guest.token = in_image.token = member.token = true; heap_host.live = heap_guest.live = in_image.live = member.live = true;
		heap_host.handle = 0x3516; heap_host.owner = 0x03290100; heap_host.event = 0x033a4568; heap_host.kind_vtable = 0x687418;
		heap_guest.handle = 0x1516; heap_guest.owner = 0x032d0100; heap_guest.event = 0x03454568; heap_guest.kind_vtable = 0x687418;
		in_image.handle = 0x1516; in_image.owner = 0x032d0100; in_image.event = 0x6d7c5c; in_image.kind_vtable = 0x12345678;
		member.handle = 0x1516; member.owner = 0x032d0100; member.event = 0x032d0100 + 0x320; member.kind_vtable = 0x12345678;
		check(event_kind_key(heap_host) == 0x687418 && event_kind_key(heap_guest) == 0x687418 && !canonical_event_less(heap_host, heap_guest, 0x3ff) && !canonical_event_less(heap_guest, heap_host, 0x3ff),
		      "follow-up 14 (S2): the same heap kind at two per-process addresses has one key and sorts equal");
		check(event_kind_key(in_image) == 0x6d7c5c && event_kind_key(member) == 0x320 && !event_kind_in_image(0x033a4568) && event_kind_in_image(0x6d7c5c) && !event_kind_in_image(0xa40000),
		      "follow-up 14 (S2): an in-image kind keeps its address, a member its offset; the image range is [0x401000, 0xa40000)");
		event_entry heap_no_vtable = heap_host; heap_no_vtable.kind_vtable = 0;
		check(event_kind_key(heap_no_vtable) == 0x033a4568, "follow-up 14 (S2): a heap kind whose vtable could not be read keeps its raw address");
	}
	std::printf("%d PASS / %d FAIL\n", passes, failures);
	return failures ? 1 : 0;
}
