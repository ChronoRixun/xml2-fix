#include "join_in_progress_core.hpp"
#include "join_in_progress_rules.hpp"
#include "gameplay_capabilities.hpp"
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
			using namespace join_in_progress_rules;
			auto same = [&](std::uint32_t va, const auto& bytes) { return std::memcmp(mapped.data() + va - limits_rules::image_base, bytes.data(), bytes.size()) == 0; };
			check(same(teardown_call, teardown_original) && same(gamespy_call, gamespy_original) && same(add_entry, add_original) &&
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
	std::printf("%d PASS / %d FAIL\n", passes, failures);
	return failures ? 1 : 0;
}
