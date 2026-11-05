#include "join_in_progress_core.hpp"
#include "gns_transport_core.hpp" // p060fix/AR: the lost-newcomer decision and its ledger (the cohort model below)
#include "join_in_progress_rules.hpp"
#include "gns_transport_rules.hpp" // AR8: the input block layout the epoch-marker hooks read
#include "join_in_progress_diagnostics.hpp"
#include "gameplay_capabilities.hpp"
#include "actor_ai_layout.hpp" // p060fix/AI: the brain ring layout per stage, checked against E4's ledger rows
#include "e4_core_sites.hpp"
#include <cstdlib>
#include <algorithm>
#include <iterator>
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
#include "join_in_progress_lost_newcomer.inl"
#include "join_in_progress_execution.inl"
	// p060fix/AR: the pure pieces, one by one.
	void lost_newcomer_units()
	{
		using namespace gns_transport_core;
		{
			const auto w = newcomer_lost(2, 0x08);
			const auto p = parse(w.data(), w.size());
			check(w.size() == newcomer_lost_size && w[0] == 11 && p.valid && p.k == kind::newcomer_lost && p.index == 2 && p.frame == 0x08,
				"AR wire: [11, lost index, newcomers still joining, 0xa4] round-trips (player 2 lost, player 3 still joining)");
			const auto self = newcomer_lost(3, 0x0e);
			check(parse(self.data(), self.size()).frame == 0x06, "AR wire: the lost index is never listed as still joining");
			const std::uint8_t host0[] = {11, 0, 0, 0xa4}, four[] = {11, 4, 0, 0xa4}, magic[] = {11, 2, 0, 0xa3}, own[] = {11, 2, 0x04, 0xa4}, hostbit[] = {11, 2, 0x01, 0xa4}, shortw[] = {11, 2, 0};
			check(!parse(host0, 4).valid && !parse(four, 4).valid && !parse(magic, 4).valid && !parse(own, 4).valid && !parse(hostbit, 4).valid && !parse(shortw, 3).valid,
				"AR wire: the host (0), index 4, a wrong marker, a mask naming the lost one or the host, and a short frame are refused");
			check(newcomer_lost_authorized(true, true, 0) && !newcomer_lost_authorized(true, false, 0) && !newcomer_lost_authorized(false, true, 0) && !newcomer_lost_authorized(true, true, 1) && !newcomer_lost_authorized(true, true, -1),
				"AR wire: only the bound host's reliable decision is taken (never another guest's, never unreliable)");
			check(std::uint8_t(kind::newcomer_lost) != std::uint8_t(kind::join_boundary) && std::uint8_t(kind::newcomer_lost) != 7 && std::uint8_t(kind::newcomer_lost) != 8 && std::uint8_t(kind::newcomer_lost) != 9 && std::uint8_t(kind::newcomer_lost) != 16,
				"AR wire: kind 11 is past the room's party (7/8), removal (9), restore (16) lanes and the boundary decision (10)");
		}
		{
			newcomer_ledger l;
			l.admit(0x0c | 0x01);
			check(l.admitted == 0x0c && !l.own_removal_allowed(2) && !l.own_removal_allowed(3) && l.own_removal_allowed(1) && l.own_removal_allowed(0),
				"AR ledger: the boundary mask's newcomers are never removed by this PC's own timeout before its restart; an old peer still is (the host bit is ignored)");
			check(l.decide(2) && l.must_remove(2) && !l.decide(2), "AR ledger: the host's decision is taken once");
			l.removed(2);
			check(!l.must_remove(2) && !l.decide(2) && !l.own_removal_allowed(2), "AR ledger: a decision carried out is not repeated (the native 0x2b afterwards finds nothing)");
			check(l.decide(1) && !l.own_removal_allowed(1), "AR ledger: a decision about a participant this PC did not know as a newcomer (a co-newcomer) also blocks its own timeout");
			const auto left = l.restart_entry();
			check(left == 0x02 && l.admitted == 0 && l.decided == 0 && l.applied == 0 && l.own_removal_allowed(2), "AR ledger: the restart entry returns the open decisions and starts afresh (the restarted epoch's agreement applies)");
			check(!l.decide(0) && !l.decide(4) && !l.decide(-1), "AR ledger: no decision about the host or out-of-range indexes");
		}
		check(classify_newcomer_loss(true, true, false) == newcomer_loss::before_restart && classify_newcomer_loss(true, true, true) == newcomer_loss::ordinary &&
			classify_newcomer_loss(true, false, false) == newcomer_loss::ordinary && classify_newcomer_loss(false, true, false) == newcomer_loss::ordinary,
			"AR classify: only the host decides, only for an admission newcomer, only before its restart is announced");
		{
			machine m; m.started = true;
			const void* r1 = reinterpret_cast<const void*>(0x1001); const void* r2 = reinterpret_cast<const void*>(0x1002); const void* r3 = reinterpret_cast<const void*>(0x1003);
			m.seats[0] = seat{r1, 1, stage::streaming, 0}; m.seats[1] = seat{r2, 2, stage::starting, 0}; m.seats[2] = seat{r3, 3, stage::pending, 0};
			admission_losses a;
			check(a.newcomer(m, 1) && a.newcomer(m, 2) && !a.newcomer(m, 3) && !a.newcomer(m, 0), "AR admission: streaming/starting seats are newcomers; a pending seat outside the announced mask is not (outside the lockstep)");
			a.announced = 0x08;
			check(a.newcomer(m, 3), "AR admission: a pending seat named in the announced boundary mask is a newcomer (old peers hold for it)");
			check(a.remaining(m, 1) == 0x04, "AR admission: the newcomers still joining besides the lost one");
			a.decided(1, 5000); m.seats[0] = seat{};
			check(a.newcomer(m, 1) && a.lost == 0x02 && a.lost_at == 5000 && a.remaining(m, 2) == 0, "AR admission: a decided newcomer stays one (its later native removal is the same decision)");
			a.clear(); check(!a.lost && !a.announced, "AR admission: cleared at the restart entry / session end");
		}
		check(restart_removal_held(true, true, false, false) && restart_removal_held(true, false, true, false) && restart_removal_held(true, false, false, true) &&
			!restart_removal_held(true, false, false, false) && !restart_removal_held(false, true, true, true),
			"AR window: the restart has begun from its 0x26 (host: its broadcast) or its native start until the cohort epoch ends");
		check(removal_window(true, false, true, false, false) && removal_window(true, false, false, true, false) && !removal_window(true, false, false, true, true) &&
			!removal_window(true, false, false, false, false) && !removal_window(false, true, true, true, false) && removal_window(true, true, false, false, true),
			"AR window: no removal from the restart's 0x26 until its restored zone runs again (then the agreed frame 0); a zone seen before the start does not open it");
		{
			host_decisions d;
			check(!d.already(2, true), "AR host decisions: nothing sent yet");
			d.record(2);
			check(d.already(2, true) && d.already(2, true), "AR host decisions: one decision per newcomer in the admission (its later native removal does not send again)");
			check(!d.already(2, false) && !d.already(2, true), "AR host decisions (review P1): a bit from an admission that no longer lists the index is dropped - a reused index is classified afresh");
			d.record(3); d.reset();
			check(!d.already(3, true) && d.sent == 0, "AR host decisions: reset at every end of an admission (restart entry, no-restart, session end)");
		}
		check(owed_restart_due(0x02, false, false, false, true, false, false, 0, 0) && !owed_restart_due(0, false, false, false, true, false, false, 0, 0) &&
			!owed_restart_due(0x02, true, false, false, true, false, false, 0, 0) && !owed_restart_due(0x02, false, true, false, true, false, false, 0, 0) &&
			!owed_restart_due(0x02, false, false, true, true, false, false, 0, 0) && !owed_restart_due(0x02, false, false, false, false, false, false, 0, 0) &&
			!owed_restart_due(0x02, false, false, false, true, true, false, 0, 0),
			"AR owed restart: only when every newcomer was lost before its image, no transfer runs, an old peer remains and no zone loads");
		check(!owed_restart_due(0x02, false, false, false, true, false, true, owed_add_wait_ms - 1, 0) && owed_restart_due(0x02, false, false, false, true, false, true, owed_add_wait_ms, 0) &&
			!owed_restart_due(0x02, false, false, false, true, false, false, 0, owed_attempts_max),
			"AR owed restart: a native add wait still on is waited out for 3 s; at most 5 attempts");
		lost_newcomer_model::run_all();
		lost_newcomer_model::control_060zj();
		lost_newcomer_model::sensitivity();
		// b2d-fix-jip (live-b2d N-STALL4): the decision at a transport tick before the native add was parsed.
		{
			check(newcomer_decision_waits_for_record(true, false) && !newcomer_decision_waits_for_record(true, true) && !newcomer_decision_waits_for_record(false, false) && !newcomer_decision_waits_for_record(false, true),
				"AR carry (b2d): an absent record keeps the decision open at a tick and settles it only at the restart entry; a removed record settles it anywhere");
			// X-4p-b on the slow guest: decision (pump) -> tick: no record -> 0x1f parsed: record created -> tick: removed.
			newcomer_ledger slow;
			slow.decide(3);
			bool record_exists = false;
			auto tick = [&](bool last_chance) { // carry_decisions' shape: absent at a tick stays open; a record is removed; absent at the entry settles
				if (!slow.must_remove(3)) return false;
				if (!record_exists) { if (newcomer_decision_waits_for_record(true, last_chance)) return false; slow.removed(3); return true; }
				record_exists = false; slow.removed(3); return true;
			};
			check(!tick(false) && slow.must_remove(3), "AR carry (b2d, X-4p-b): at the tick before the native add is parsed the decision stays open (the b2d build closed it as 'already gone')");
			record_exists = true; // the parser runs the host's 0x1f (mesh completion, 0x25 reply): the record for the removed seat exists
			check(tick(false) && !record_exists && !slow.must_remove(3) && (slow.applied & 0x08), "AR carry (b2d, X-4p-b): the next tick removes the record the native add created - the restart sees 3 participants on every peer");
			check(!tick(false) && slow.restart_entry() == 0, "AR carry (b2d): nothing is left open at the restart entry");
			// X-4p-c's order (the add parsed first) is unchanged: one tick removes it.
			newcomer_ledger fast; fast.decide(3); record_exists = true;
			slow = fast; check(tick(false) && !slow.must_remove(3), "AR carry (b2d, X-4p-c): with the record already there the tick removes it as before");
			// The native 0x2b removed it before any tick: open until the restart entry, settled there without a removal.
			newcomer_ledger gone; gone.decide(2);
			check(newcomer_decision_waits_for_record(true, false) && gone.must_remove(2), "AR carry (b2d): a record the native 0x2b already removed keeps the decision open through the ticks");
			gone.removed(2); // the restart entry (last chance): absent settles
			check(!gone.must_remove(2) && gone.restart_entry() == 0, "AR carry (b2d): the restart entry settles it as 'already gone' (no decision left open, no wait)");
		}
	}
	// b2d-fix-jip (live-b2d N-LATE4 / N-2c-LATE): the restart-release barrier (join_in_progress_core::input_epoch_barrier) -
	// unit rules, then a label model of the receiver: every peer of a cohort runs the same filter over its own arrival order
	// and the model compares what label each peer gives the sender's records (what the one-label-late split is made of).
	namespace barrier_model
	{
		using namespace join_in_progress_core;
		enum class ev { stale, real, resent, marker };
		struct entry { bool stale; int label; };
		struct receiver
		{
			input_epoch_barrier barrier; input_epoch_memory memory;
			unsigned counter[4]{}; std::uint32_t read[4]{}; bool armed = true;
			std::vector<entry> stored[4], held[4]; // per participant block: what the native store holds, what the barrier holds aside
			unsigned dropped_at_marker = 0, flushed = 0, resets = 0, lates = 0;
			void reset_all(int own, std::uint32_t now, std::uint32_t occupied = 0x0f) { memory.forget(); barrier.disarm(); if (armed) barrier.arm(own, occupied, now); for (unsigned i = 0; i < 4; ++i) { counter[i] = 0; read[i] = 0; stored[i].clear(); held[i].clear(); } }
			// The store hook's decision for one record of `index` arriving at `now`.
			void receive(unsigned index, ev e, std::uint32_t now, std::uint16_t nonce = 0, int wire_label = 0)
			{
				if (e == ev::marker)
				{
					const auto a = input_epoch_store_action(0, input_epoch_mark_for(nonce), memory.last[index], read[index]);
					if (a == input_epoch_action::ignore) return;
					memory.last[index] = nonce;
					dropped_at_marker += barrier.marker(index); held[index].clear();
					if (a == input_epoch_action::late) { ++lates; return; }
					++resets; counter[index] = 0; read[index] = 0; stored[index].clear(); // the block reset
					return;
				}
				const int label = e == ev::resent ? wire_label : int(counter[index]++); // the decoder: single records take the counter, batch records the wire label
				const auto h = barrier.record(index, now);
				if (h == input_epoch_hold::hold) { held[index].push_back({e == ev::stale, label}); return; }
				if (h == input_epoch_hold::flush) { for (const auto& r : held[index]) { stored[index].push_back(r); ++flushed; } held[index].clear(); barrier.take(index); }
				stored[index].push_back({e == ev::stale, label});
			}
			void consume(unsigned index, unsigned n) { read[index] += n; }
			int first_real_label(unsigned index = 2) const { for (const auto& r : stored[index]) if (!r.stale) return r.label; return -1; }
			unsigned stale_stored(unsigned index = 2) const { unsigned n = 0; for (const auto& r : stored[index]) n += r.stale; return n; }
		};
		bool same_labels(const std::vector<receiver>& peers, unsigned index = 2)
		{
			for (std::size_t i = 1; i < peers.size(); ++i)
				if (peers[i].first_real_label(index) != peers[0].first_real_label(index) || peers[i].stale_stored(index) != peers[0].stale_stored(index)) return false;
			return true;
		}
	}
	void restart_release_barrier_units()
	{
		using namespace join_in_progress_core;
		using namespace barrier_model;
		{
			input_epoch_barrier b;
			check(!b.any() && b.record(1, 100) == input_epoch_hold::store && b.marker(1) == 0, "barrier: never armed - every record is stored, a marker drops nothing");
			b.arm(0, 0x0f, 1000);
			check(b.any() && !b.expecting[0] && b.expecting[1] && b.expecting[2] && b.expecting[3] && b.armed_at == 1000, "barrier: a full roster arms every occupied index but this PC's own at its ring reset");
			check(b.record(0, 1001) == input_epoch_hold::store && b.held[0] == 0, "barrier: this PC's own records (the own-record store) are never held");
			check(b.record(2, 1001) == input_epoch_hold::hold && b.record(2, 1500) == input_epoch_hold::hold && b.held[2] == 2 && b.expecting[2], "barrier: a peer's records before its marker are held and counted");
			check(b.marker(2) == 2 && !b.expecting[2] && b.held[2] == 0 && b.marker(2) == 0, "barrier: its marker ends the expectation and drops the held records (a second marker drops nothing)");
			check(b.record(2, 1600) == input_epoch_hold::store, "barrier: after the marker its records are stored as usual");
			check(b.record(3, 1000 + input_epoch_hold_cap_ms - 1) == input_epoch_hold::hold && b.record(3, 1000 + input_epoch_hold_cap_ms) == input_epoch_hold::flush && !b.expecting[3] && b.held[3] == 1,
			      "barrier: at the cap (5000 ms after the reset) the next record flushes - the held one(s) are stored first, then it");
			check(b.take(3) == 1 && b.held[3] == 0 && b.record(3, 9000) == input_epoch_hold::store, "barrier: take() hands the held count to the replay; after the flush nothing is held");
			input_epoch_barrier full; full.arm(0, 0x0f, 0);
			for (unsigned i = 0; i < input_epoch_hold_records; ++i) full.record(1, 10);
			check(full.held[1] == input_epoch_hold_records && full.record(1, 11) == input_epoch_hold::flush && !full.expecting[1], "barrier: a full hold (96 records) flushes before the cap");
			full.arm(2, 0x0f, 50); check(!full.expecting[2] && full.expecting[1] && full.held[1] == 0 && full.armed_at == 50, "barrier: re-arming (the start's reset after the release's) clears the counts and the clock");
			full.disarm(); check(!full.any() && full.record(1, 60) == input_epoch_hold::store, "barrier: disarmed (session end, RestartInputEpoch=0) - stored as stock");
			check(input_epoch_hold_cap_ms == 5000 && input_epoch_hold_records == 96 && input_epoch_record_bytes == 0x24, "barrier: cap 5 s (observed marker skew 1.0-2.4 s), 96 records, the store's 0x24-byte record");
			input_epoch_barrier wrap; wrap.arm(0, 0x03, 0xfffffff0u);
			check(wrap.record(1, 0x00000010u) == input_epoch_hold::hold && wrap.record(1, 0xfffffff0u + input_epoch_hold_cap_ms) == input_epoch_hold::flush, "barrier: the cap survives a GetTickCount wrap");
		}
		{ // Only listed participants at this reset can be expected, including rosters with holes and a nonzero own index.
			struct roster_case { int own; std::uint32_t occupied; bool expected[4]; bool any; };
			const roster_case cases[] = {
				{0, 0x00, {false, false, false, false}, false},
				{2, 0x04, {false, false, false, false}, false},
				{0, 0x05, {false, false, true, false}, true},
				{2, 0x0d, {true, false, false, true}, true},
				{3, 0x0f, {true, true, true, false}, true},
			};
			for (const auto& c : cases)
			{
				input_epoch_barrier b; b.arm(c.own, c.occupied, 1000);
				bool correct = b.any() == c.any;
				for (unsigned i = 0; i < 4; ++i)
				{
					correct = correct && b.record(i, 1001) == (c.expected[i] ? input_epoch_hold::hold : input_epoch_hold::store);
					correct = correct && b.record(i, 63000) == (c.expected[i] ? input_epoch_hold::flush : input_epoch_hold::store);
				}
				check(correct && !b.any(), "barrier occupancy: only occupied remote indices hold/flush; holes, empty and own-only rosters store without a false 62-second cap");
			}
			input_epoch_barrier changed; changed.arm(0, 0x05, 1000); changed.record(2, 1001);
			changed.arm(0, 0x03, 2000);
			check(changed.held[2] == 0 && changed.record(2, 2001) == input_epoch_hold::store && changed.record(1, 2001) == input_epoch_hold::hold,
			      "barrier occupancy: the next reset replaces the roster and drops a departed participant's held records");
			changed.arm(0, 0x01, 3000);
			check(!changed.any() && changed.held[1] == 0 && changed.record(1, 63000) == input_epoch_hold::store,
			      "barrier occupancy: re-arming own-only clears the last remote expectation and held records");
		}
		// The label model. P = index 2 restarts; the receivers (host 0, guest 1[, guest 3]) each reset at t0 and see P's
		// records in their own order. `stale` = a record P sent before its restart (in flight); `real` = P's frame 0 after
		// its marker (a single record); `resent` = P's frame 0 through the reliable 0x5c/0x5d resend (wire label).
		auto cohort = [](std::size_t n, bool barrier) { std::vector<receiver> peers(n); for (std::size_t i = 0; i < n; ++i) { peers[i].armed = barrier; peers[i].reset_all(int(i == 0 ? 0 : i == 1 ? 1 : 3), 1000, n == 1 ? 0x05u : n == 2 ? 0x07u : 0x0fu); } return peers; };
		for (const std::size_t players : {2u, 3u, 4u}) // 2, 3 and 4 players: 1, 2 and 3 receivers of P's stream
		{
			const std::size_t n = players - 1;
			{ // on-time marker on every peer (the common case): nothing changes - label 0 = P's first real record
				auto p = cohort(n, true);
				for (auto& r : p) { r.receive(2, ev::marker, 2500, 0x2d25); r.receive(2, ev::real, 2800); }
				bool ok = same_labels(p); for (auto& r : p) ok = ok && r.first_real_label() == 0 && r.stale_stored() == 0 && r.resets == 1 && r.dropped_at_marker == 0;
				check(ok, "barrier model: on-time marker - every peer labels P's first real record 0 (nothing held)");
			}
			{ // X-4p-a: ONE peer (the host) gets a stale record first and P's marker 0.3 s after it would have passed its gate
				auto fixed = cohort(n, true), before = cohort(n, false);
				for (auto* set : {&fixed, &before})
				{
					auto& host = (*set)[0];
					host.receive(2, ev::stale, 1300); // m11's pre-restart record, parsed after the host's reset
					const bool gate_can_pass = !host.stored[2].empty();
					if (gate_can_pass) host.consume(2, 1); // the b2d host passed its frame-0 gate on it (05:36:01.880)
					host.receive(2, ev::marker, 2538, 0x2d25); // m11's marker, 05:36:02.169
					host.receive(2, ev::real, 2800);
					for (std::size_t i = 1; i < set->size(); ++i) { (*set)[i].receive(2, ev::marker, 2500, 0x2d25); (*set)[i].receive(2, ev::real, 2800); }
				}
				check(!fixed[0].stored[2].empty() && fixed[0].first_real_label() == 0 && fixed[0].stale_stored() == 0 && fixed[0].dropped_at_marker == 1 && fixed[0].lates == 0 && same_labels(fixed),
				      players == 4 ? "barrier model (X-4p-a, 4 players): the host holds m11's stale record, cannot pass its gate on it, drops it at m11's marker - label 0 = m11's frame 0 on the host and both guests"
				                   : "barrier model (X-4p-a shape): the one late peer holds the stale record and drops it at the marker - labels equal on every peer");
				check(before[0].lates == 1 && before[0].stale_stored() == 1 && before[0].first_real_label() == 1 && (before.size() == 1 || (before[1].first_real_label() == 0 && !same_labels(before))),
				      "barrier model (control, b2d build): without the barrier the host stores the stale record as label 0, passes its gate, ignores the late marker, and labels m11's frame 0 as 1 while the guests label it 0 - the N-LATE4 split");
			}
			{ // X-2cR / X-2cL / X-2cR2 (2 players): the guest gets the host's stale record, the marker 1.2-2.4 s after its reset
				auto fixed = cohort(1, true), before = cohort(1, false);
				for (auto* set : {&fixed, &before}) { auto& g = (*set)[0]; g.reset_all(1, 1000); g.receive(0, ev::stale, 1100); g.receive(0, ev::stale, 1120); if (!g.stored[0].empty()) g.consume(0, 1); g.receive(0, ev::marker, 3400, 0x1d59); g.receive(0, ev::real, 3600); }
				check(fixed[0].stale_stored(0) == 0 && fixed[0].first_real_label(0) == 0 && fixed[0].dropped_at_marker == 2 && fixed[0].lates == 0 && before[0].lates == 1 && before[0].first_real_label(0) == 2,
				      "barrier model (X-2cR shape): the guest holds the host's two stale records and drops them at the marker (label 0 = the host's frame 0, as on the host); the b2d build labelled it 2 after a late marker");
			}
			{ // late on every peer (every parser behind): the same hold and drop everywhere
				auto p = cohort(n, true);
				for (auto& r : p) { r.receive(2, ev::stale, 1200); r.receive(2, ev::marker, 4500, 0x2d25); r.receive(2, ev::real, 4700); }
				bool ok = same_labels(p); for (auto& r : p) ok = ok && r.first_real_label() == 0 && r.dropped_at_marker == 1;
				check(ok, "barrier model: marker late (inside the cap) on every peer - every peer drops its held stale record; labels equal");
			}
			{ // marker lost on every peer: the cap stores the held records as they came - the stock labelling, the same everywhere
				auto p = cohort(n, true);
				for (auto& r : p) { r.receive(2, ev::stale, 1200); r.receive(2, ev::real, 6100); }
				bool ok = same_labels(p); for (auto& r : p) ok = ok && r.flushed == 1 && r.stale_stored() == 1 && r.first_real_label() == 1;
				check(ok, "barrier model: marker lost everywhere - at the cap each peer stores the stale record (label 0) and the real one (label 1): the stock labelling, equal on every peer");
			}
			{ // marker lost on one peer only, with a stale record there: the residual (the cap gives that peer the stock labelling)
				auto p = cohort(n, true), before = cohort(n, false);
				for (auto* set : {&p, &before})
				{
					(*set)[0].receive(2, ev::stale, 1200); (*set)[0].receive(2, ev::real, 6100);
					for (std::size_t i = 1; i < set->size(); ++i) { (*set)[i].receive(2, ev::marker, 2500, 0x2d25); (*set)[i].receive(2, ev::real, 2800); }
				}
				check(p[0].first_real_label() == 1 && (n == 1 || (!same_labels(p) && !same_labels(before))) && p[0].stale_stored() == before[0].stale_stored(),
				      "barrier model (residual): a marker lost on one peer that holds a stale record gives that peer the stock labelling at the cap - the b2d build's outcome, never worse");
				auto clean = cohort(n, true);
				clean[0].receive(2, ev::real, 6100); for (std::size_t i = 1; i < n; ++i) { clean[i].receive(2, ev::marker, 2500, 0x2d25); clean[i].receive(2, ev::real, 2800); }
				check(same_labels(clean) && clean[0].first_real_label() == 0, "barrier model: a marker lost on one peer with nothing stale there - its cap stores the real record as label 0, equal to the others");
			}
			{ // P-LJ3: the newcomer's frame 0 reaches every peer through the reliable resend 11 s after their resets, its marker 16.6 s after
				auto p = cohort(n, true);
				for (auto& r : p) { r.receive(2, ev::resent, 12000, 0, 0); r.consume(2, 1); r.receive(2, ev::marker, 12013, 0x70d8); r.receive(2, ev::real, 12100); }
				bool ok = same_labels(p); for (auto& r : p) ok = ok && r.lates == 1 && r.first_real_label() == 0 && r.flushed == 0 && r.dropped_at_marker == 0;
				check(ok, "barrier model (P-LJ3): past the cap the resent frame 0 is stored (wire label 0) and consumed, the marker is late and IGNORED on every peer alike (the live PASS, unchanged)");
			}
			{ // a resent real record inside the cap, then the marker: dropped, requested again (the resend serves it by label)
				auto p = cohort(n, true);
				for (auto& r : p) { r.receive(2, ev::resent, 3000, 0, 0); r.receive(2, ev::marker, 3500, 0x70d8); r.receive(2, ev::resent, 4000, 0, 0); r.receive(2, ev::real, 4100); }
				// (How the block counter follows a stored batch record is the decoder's business and not modelled: only the first label is checked.)
				bool ok = same_labels(p); for (auto& r : p) ok = ok && r.dropped_at_marker == 1 && r.resets == 1 && r.first_real_label() == 0 && r.stored[2].size() == 2 && !r.stored[2][0].stale;
				check(ok, "barrier model: a resent frame 0 held inside the cap is dropped at the marker and comes again by label - label 0 = frame 0 on every peer");
			}
		}
		{ // the hold is per participant: P's hold never touches Q's records; a marker for Q releases only Q
			receiver r; r.reset_all(0, 1000);
			r.receive(2, ev::stale, 1100); r.receive(1, ev::marker, 1200, 0x1111); r.receive(1, ev::real, 1300);
			check(r.held[2].size() == 1 && r.barrier.expecting[2] && !r.barrier.expecting[1] && r.stored[1].size() == 1 && !r.stored[1][0].stale && r.stored[2].empty(), "barrier model: holds are per participant - Q's marker releases Q alone, P's stale record stays held");
		}
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
		// p060fix/AR2: the host's sync waits for its own first shared frame when no newcomer is left (ZK-3b1/3b2/3b2r/3b3)
		check(host_sync_waits_for_frame0(true, false, false, 0) && host_sync_waits_for_frame0(true, false, false, 9999),
			"AR2: the host with no newcomer left keeps its epoch before frame 0");
		check(!host_sync_waits_for_frame0(true, false, true, 0), "AR2: the host's own frame 0 ends the wait (the sync lands after it, as with a newcomer)");
		check(!host_sync_waits_for_frame0(true, true, false, 0), "AR2: a newcomer still joining is waited for by the seats, not this rule");
		check(!host_sync_waits_for_frame0(false, false, false, 0), "AR2: an existing guest syncs as before (passing runs: before its frame 0)");
		check(!host_sync_waits_for_frame0(true, false, false, 10000) && !host_sync_waits_for_frame0(true, false, false, 60000),
			"AR2: the cap releases the host when frame 0 never comes");
		check(host_sync_waits_for_frame0(true, false, false, 2999, 3000) && !host_sync_waits_for_frame0(true, false, false, 3000, 3000), "AR2: the cap is exact");
		// the cohort itself: holding supplied by the rule keeps the epoch, frame 0 releases it on the next observation
		cohort c; c.begin(); c.start(); c.observe(true, false);
		bool frame0 = false;
		check(!c.observe(false, host_sync_waits_for_frame0(true, false, frame0, 100)) && c.active, "AR2: the zero-newcomer host's epoch survives the zone's end");
		frame0 = true;
		check(c.observe(false, host_sync_waits_for_frame0(true, false, frame0, 170)) && !c.active, "AR2: the zero-newcomer host syncs after its first shared frame");
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
			check(join_in_progress_rules::execution_mismatch(mapped.data(),true,true)==nullptr,"AR5: all optional instruction/ABI contracts match retail");
			for(const auto group : {std::span<const join_in_progress_rules::guard>(join_in_progress_rules::execution_wrapper_guards),std::span<const join_in_progress_rules::guard>(join_in_progress_rules::execution_observer_guards)}) {
				bool refuse=true,off=true;
				for(const auto& g:group) for(unsigned i=0;i<g.hex.size()/2;++i) {
					auto changed=mapped;changed[g.va-limits_rules::image_base+i]^=1;
					refuse &= join_in_progress_rules::execution_mismatch(changed.data(),true,true)!=nullptr;
					off &= join_in_progress_rules::execution_mismatch(changed.data(),false,false)==nullptr;
				}
				check(refuse && off,"AR5: every optional guard byte refuses ON and adds no OFF acceptance constraint");
			}
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
			// b3: the two 4057 notice CALLs and the lone-host release counts are retail bytes, each under a guard that refuses a change.
			for (std::size_t i = 0; i < lost_sync_message_calls.size(); ++i) {
				check(same(lost_sync_message_calls[i], lost_sync_message_originals[i]) && same(solo_release_counts[i], solo_release_original),
				      "b3: the 4057 notice CALL and the lone-host participant-count imm8 are the retail bytes");
				for (const auto va : {lost_sync_message_calls[i], solo_release_counts[i]}) {
					auto changed = mapped;
					changed[va - limits_rules::image_base] ^= 1;
					const auto* bad = join_in_progress_rules::first_mismatch(changed.data());
					check(bad && bad->va <= va && va < bad->va + bad->hex.size() / 2, "b3: a changed 4057 notice CALL or release count refuses the JIP transaction");
				}
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
		{
			// p060fix/AQ: the join hold's state machine (src/join_in_progress_core.hpp join_hold).
			using ev = join_hold::event; using role = join_hold::role;
			check(join_hold_default_seconds == 10 && join_hold_max_seconds == 15 && join_hold_seconds(6) == 6 && join_hold_seconds(0) == 0 && join_hold_seconds(-4) == 0 && join_hold_seconds(16) == 15 && join_hold_seconds(15) == 15,
			      "AQ2: JoinHoldSeconds defaults to 10 (Owen 11-01: to be safe over internet joins; LAN max zone-load end -> ready 6.40 s, live-060ze ZE-4PE2p) and is clamped to 0..15");
			{ join_hold d; check(d.enabled() && d.cap_ms == 10000 && d.begin(join_hold::role::host, 1, 0) && d.poll(9999, 1, 0) == ev::none && d.poll(10000, 1, 0) == ev::cap, "AQ2: a default hold caps at 10000 ms (still held at 9999)"); }
			join_hold h; h.configure(6); check(h.enabled() && h.cap_ms == 6000, "AQ: JoinHoldSeconds=6 caps at 6000 ms (the checks below use 6)");
			// 1 newcomer, ready before the cap; the restart ends it
			check(h.begin(role::host, 1, 1000) && h.active() && h.newcomers == 1 && !h.begin(role::host, 1, 1001), "AQ: the host opens one hold (a second open is refused while it lasts)");
			check(h.poll(2000, 1, 0) == ev::none && h.poll(6999, 1, 0) == ev::none, "AQ: a newcomer still streaming keeps the hold (5999 ms)");
			check(h.poll(4000, 0, 1) == ev::ready && h.all_ready && h.ready == 1 && h.ready_at == 4000 && h.poll(4500, 0, 1) == ev::none, "AQ: its image in, the hold reports ready once and keeps holding");
			check(h.poll(7500, 0, 1) == ev::none, "AQ: once every newcomer is ready the cap no longer applies (the native start's 1 s wait follows)");
			check(h.release(ev::restart, 5000) == ev::restart && !h.active() && h.ended_ms == 4000 && h.ready == 1 && h.last == ev::restart, "AQ: this PC's restart ends it (4000 ms, 1 ready)");
			check(h.release(ev::restart, 6000) == ev::none && h.poll(9000, 1, 0) == ev::none, "AQ: a closed hold ignores a second release and every poll");
			// 2 newcomers (the rider), ready only when both are in
			check(h.begin(role::host, 1, 0) && (h.add_newcomer(), h.newcomers == 2) && std::strcmp(join_hold_text(h.newcomers), "2 players are joining...") == 0, "AQ: the rider joins the open hold: \"2 players are joining...\"");
			check(h.poll(100, 2, 0) == ev::none && h.poll(200, 1, 1) == ev::none && !h.all_ready && h.poll(300, 0, 2) == ev::ready && h.ready == 2, "AQ: two newcomers: ready only when both images are in");
			check(h.release(ev::restart, 400) == ev::restart && h.ready == 2, "AQ: two newcomers: the restart ends it, 2 ready");
			// cap reached (a throttled joiner) at exactly cap_ms, across the tick wrap
			check(h.begin(role::host, 1, 0xfffff000u) && h.poll(0xfffff000u + 5999u, 1, 0) == ev::none && h.poll(0xfffff000u + 6000u, 1, 0) == ev::cap && !h.active() && h.ended_ms == 6000 && h.ready == 0,
			      "AQ: the cap ends a hold whose newcomer is not ready at 6000 ms, across the tick wrap (0 ready)");
			// joiner killed / left: every seat freed ends it; one of two leaving keeps it
			check(h.begin(role::host, 2, 0) && h.poll(100, 1, 0) == ev::none && h.poll(200, 0, 0) == ev::gone && !h.active(), "AQ: every newcomer gone (seat freed) ends the hold at once");
			// host Quit / kill: the session's end
			check(h.begin(role::host, 1, 0) && h.release(ev::session, 1500) == ev::session && h.ended_ms == 1500, "AQ: the session's end (host Quit) ends the hold");
			// ready but the restart never begins: bounded
			check(h.begin(role::host, 1, 0) && h.poll(1000, 0, 1) == ev::ready && h.poll(1000 + join_hold_restart_limit_ms - 1, 0, 1) == ev::none && h.poll(1000 + join_hold_restart_limit_ms, 0, 1) == ev::restart_late && !h.active(),
			      "AQ: every newcomer ready but no restart within 4000 ms: the hold ends (never an endless freeze)");
			// existing participants (3 / 4 peers): they cannot see the seats; only the cap, their restart or the session end
			for (int joining = 1; joining <= 3; ++joining)
			{
				join_hold m; m.configure(6); char what[160];
				std::snprintf(what, sizeof what, "AQ: an existing participant with %d newcomer(s) holds until its restart (text \"%s\"), never ready by itself", joining, join_hold_text(joining));
				check(m.begin(role::member, joining, 0) && m.newcomers == joining && m.poll(5999, -1, -1) == ev::none && !m.all_ready && m.release(ev::restart, 4000) == ev::restart && m.ready == joining, what);
			}
			{ join_hold m; m.configure(6); check(m.begin(role::member, 0, 0) && m.newcomers == 1 && m.poll(6000, -1, -1) == ev::cap, "AQ: a member whose host died is released by the cap (count 0 shows as one player)"); }
			{ join_hold n; check(!n.begin(role::none, 1, 0) && !n.active(), "AQ: a newcomer (role none) never holds"); }
			{ join_hold m; m.configure(6); check(m.arm(role::member, 1, 0) && m.poll(9999, -1, -1)==ev::none && m.poll(10000, -1, -1)==ev::cap,
				"AQ3: an existing peer whose zone/restart never arrives is released within its cap plus the 4 s safety limit"); }
			{ join_hold m; m.configure(6); check(m.arm(role::member,1,0) && m.open(9000) && m.poll(10000,-1,-1)==ev::cap,
				"AQ3: opening a delayed member hold cannot renew the absolute admission safety deadline"); }
			{ join_hold m; m.configure(6); check(m.arm(role::member,1,0) && !m.open(10000) && m.poll(10000,-1,-1)==ev::cap,
				"AQ3: a member already past its admission deadline cannot reopen at the zone-load end"); }
			{ join_hold off; off.configure(0); check(!off.enabled() && !off.begin(role::host, 1, 0) && off.poll(10000, 1, 0) == ev::none, "AQ: JoinHoldSeconds=0 never holds (today's path)"); }
			{ join_hold big; big.configure(40); check(big.cap_ms == 15000, "AQ: JoinHoldSeconds above 15 is clamped to 15 s"); }
			check(std::strcmp(join_hold_text(1), "A player is joining...") == 0 && std::strcmp(join_hold_text(3), "3 players are joining...") == 0 && std::strcmp(join_hold_text(0), "A player is joining...") == 0,
			      "AQ: the hold screen text: \"A player is joining...\" / \"N players are joining...\"");
			// p060fix/AQ2: the host's hold is armed at the admission and opens (its clock starts) at the zone load's end.
			{
				join_hold a; a.configure(6);
				check(a.arm(role::host, 1, 1000) && a.active() && a.armed && !a.arm(role::host, 1, 1001) && !a.begin(role::host, 1, 1001), "AQ2: the admission arms one hold (held at once; a second arm or open is refused while it lasts)");
				check(a.poll(1000 + 7000, 1, 0) == ev::none && a.armed, "AQ2: while armed (the zone load) the cap does not apply: 7000 ms of loading keep it");
				check(a.open(9500) && !a.armed && a.armed_ms == 8500 && a.since == 9500 && a.elapsed(9500) == 0 && !a.open(9600), "AQ2: the zone load's end opens it once; the clock restarts there (armed 8500 ms)");
				check(a.poll(9500 + 5999, 1, 0) == ev::none && a.poll(9500 + 6000, 1, 0) == ev::cap && !a.active() && !a.armed && a.ended_ms == 6000, "AQ2: the cap counts 6000 ms from the opening, not from the admission");
				check(a.arm(role::host, 1, 0) && a.poll(join_hold_armed_limit_ms - 1, 1, 0) == ev::none && a.poll(join_hold_armed_limit_ms, 1, 0) == ev::cap && !a.active() && join_hold_armed_limit_ms == 30000,
				      "AQ2: a zone load that never ends: the armed hold ends after 30000 ms (never an endless freeze)");
				check(a.arm(role::host, 1, 0) && a.poll(500, 0, 0) == ev::gone && !a.active(), "AQ2: the newcomer gone during the zone load (its add given up / it left) ends the armed hold");
				check(a.arm(role::host, 1, 0) && (a.add_newcomer(), a.newcomers == 2) && a.open(300) && a.poll(400, 1, 1) == ev::none && a.poll(500, 0, 2) == ev::ready && a.ready == 2 && a.release(ev::restart, 900) == ev::restart && a.ended_ms == 600,
				      "AQ2: two newcomers admitted at one zone load share the armed hold; ready when both images are in; the logged time runs from the opening");
				check(a.arm(role::host, 1, 0) && a.release(ev::session, 200) == ev::session && !a.armed, "AQ2: the session's end ends an armed hold");
				join_hold off; off.configure(0); check(!off.arm(role::host, 1, 0) && !off.active() && !off.open(10), "AQ2: JoinHoldSeconds=0 arms nothing");
				join_hold m; check(m.begin(role::member, 1, 0) && !m.armed && !m.open(10), "AQ2: an existing participant's hold is open from its 0x47 (never armed)");
			}
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
			// AR6 (fable-ar6): the pause-code guard at the controller apply - a function of the record alone.
			{
				using join_in_progress_core::applied_code_mask; using join_in_progress_core::pause_code_bit; using join_in_progress_core::ring_record_mask;
				check(pause_code_bit == 0x00080000u && join_in_progress_core::pause_guard_frames == 2, "AR6: UI code 0x13 is bit 19; the guard covers frames 0 and 1");
				check(applied_code_mask(0x00080000u, 0) == 0 && applied_code_mask(0x00080000u, 1) == 0, "AR6: frames 0 and 1 drop the pause code");
				check(applied_code_mask(0x00080000u, 2) == 0x00080000u && applied_code_mask(0x00080000u, 1000) == 0x00080000u, "AR6: frame 2 and later keep it");
				check(applied_code_mask(0x0008a5a5u, 0) == 0x0000a5a5u && applied_code_mask(0x0000a5a5u, 0) == 0x0000a5a5u, "AR6: only bit 0x13 changes; a record without it is untouched");
				check(applied_code_mask(0xffffffffu, 1) == 0xfff7ffffu && applied_code_mask(0, 0) == 0, "AR6: every other code survives; zero stays zero");
				const std::uintptr_t sync = 0x79fbb0u, slot0 = sync + 0x268u;
				check(ring_record_mask(slot0 + 8u) && ring_record_mask(slot0 + 0x28u + 8u) && ring_record_mask(slot0 + 299u * 0x28u + 8u), "AR6: block 0 slots 0, 1, 299 - the code mask at slot + 8");
				check(ring_record_mask(sync + 3u * 0x3184u + 0x268u + 8u) && ring_record_mask(sync + 3u * 0x3184u + 0x268u + 299u * 0x28u + 8u), "AR6: block 3 (the last participant) too");
				check(!ring_record_mask(sync + 4u * 0x3184u + 0x268u + 8u) && !ring_record_mask(slot0 + 300u * 0x28u + 8u), "AR6: a fifth block or a 301st slot is not a ring record");
				check(!ring_record_mask(slot0 + 4u) && !ring_record_mask(slot0 + 12u) && !ring_record_mask(slot0) && !ring_record_mask(sync + 0x3170u), "AR6: the frame word, the axes, the valid byte and the ring head are not the mask field");
				check(!ring_record_mask(0x12345678u) && !ring_record_mask(0) && !ring_record_mask(sync - 0x28u + 8u), "AR6: a stack record, null and memory below the synchronizer are left alone");
				check(ring_record_mask(0x1000u + 0x268u + 8u, 0x1000u) && !ring_record_mask(0x1000u + 0x268u + 9u, 0x1000u), "AR6: the test works on any synchronizer address and needs the exact field");
			}
			// AR6 Layer 1 (fable-ar6): the sender keeps the first whole_input_frames records full after a ring reset.
			{
				using join_in_progress_core::restart_whole_input; using join_in_progress_core::send_form; using join_in_progress_core::whole_input_frames;
				check(whole_input_frames == 32, "AR6 L1: the whole-input window is 32 frames (~1 s at 30 fps)");
				check(restart_whole_input(0) == send_form::force_full && restart_whole_input(31) == send_form::force_full, "AR6 L1: frames 0 and 31 go out full");
				check(restart_whole_input(32) == send_form::stock && restart_whole_input(2000) == send_form::stock, "AR6 L1: frame 32 and later use the stock delta decision");
			}
			// AR7 (fable-ar7) / b2c review: the clock step. The step word travels with its placeholder bit (the sender's
			// `and eax, 0xffff7fff` masks the code-mask dword), so the stock rule is already the same on every peer; the
			// patch ([Online] SymmetricClockStep, default OFF) only changes what a flagged record counts, everywhere alike.
			{
				using namespace join_in_progress_core;
				const std::uint16_t capped = std::uint16_t(step_word_cap | step_placeholder_bit), frame33 = std::uint16_t(333u | step_placeholder_bit);
				check(step_seconds(step_word_cap) == 1.0f && step_seconds(capped) == 1.0f && step_seconds(0) == 0.0f, "AR7: the step word is 0.1 ms units, bit 15 is not part of the value");
				check(wire_step_word(capped) == capped && wire_step_word(frame33) == frame33 && wire_step_word(step_word_cap) == step_word_cap, "AR7: the wire carries the step word verbatim, placeholder bit included (0x5597e9 -> 0x5597fe)");
				check(wire_code_mask(0xffffffffu) == 0xffff7fffu && wire_code_mask(0x00088000u) == 0x00080000u && wire_code_mask(0x00080000u) == 0x00080000u, "AR7: it is the code-mask dword that loses bit 15 on the wire (0x5597f1)");
				check(stock_clock_step(capped) == step_placeholder_seconds && stock_clock_step(wire_step_word(capped)) == step_placeholder_seconds, "AR7: stock - a flagged 1.0 s record counts 0.025 s on its owner and, received verbatim, 0.025 s on every other peer");
				check(stock_clock_step(step_word_cap) == 1.0f && stock_clock_step(333) == step_seconds(333), "AR7: stock - an unflagged record counts its value everywhere");
				bool stock_symmetric = true, patched_symmetric = true;
				for (unsigned w = 0; w < 0x10000u; ++w)
				{
					const auto word = std::uint16_t(w);
					if (stock_clock_step(word) != stock_clock_step(wire_step_word(word))) stock_symmetric = false;
					if (symmetric_clock_step(word) != symmetric_clock_step(wire_step_word(word)) || symmetric_clock_step(word) != step_seconds(word)) patched_symmetric = false;
				}
				check(stock_symmetric, "AR7: for every step word the stock step on the owner equals the stock step on a receiver (the stock rule is symmetric; the AR7 premise was wrong)");
				check(patched_symmetric, "AR7: for every step word the patched step is the value, on the owner and on every receiver alike");
				check(symmetric_clock_step(frame33) == step_seconds(333) && stock_clock_step(frame33) == step_placeholder_seconds && symmetric_clock_step(capped) == 1.0f, "AR7: with the patch a flagged 33.3 ms record counts 33.3 ms and a capped placeholder 1.0 s (stock: 25 ms) - a pacing change, not a desync fix");
				check(std::uint32_t(0x3ccccccd) == [] { float f = step_placeholder_seconds; std::uint32_t u; std::memcpy(&u, &f, 4); return u; }(), "AR7: the stock constant at 0x55923f is 0.025f");
				// The patch: 2 bytes at 0x559233 (the first instruction after `and eax, 0x7fff`) jump to the unflagged path.
				using namespace join_in_progress_rules;
				check(clock_step_site + 2 + clock_step_patch[1] == clock_step_target && clock_step_patch[0] == 0xeb && clock_step_patch[2] == 0x90 && clock_step_patch[3] == 0x90, "AR7: eb 1a at 0x559233 lands on 0x55924f (movzx ecx, ax), then two nops");
				const std::string region(clock_step_guards[0].hex);
				const auto at = [&](std::uint32_t va, std::size_t n) { return region.substr((va - clock_step_guards[0].va) * 2, n * 2); };
				check(clock_step_guards[0].va == 0x559222 && region.size() == 2 * (0x55926b - 0x559222), "AR7: the guard covers 0x559222..0x55926a (the read, both paths and the join)");
				check(at(clock_step_site, 4) == "89442410" && at(clock_step_target, 3) == "0fb7c8" && at(0x55922e, 5) == "25ff7f0000" && at(0x55923b, 8) == "c7442414cdcccc3c", "AR7: the guarded bytes hold the original mov, the jump target's movzx, the mask and the 0.025 s constant");
				check(std::string(clock_step_guards[2].hex).substr(0, 12) == "d90530006800" && clock_step_guards[2].va == clock_step_observe_site && clock_step_observe_resume == clock_step_observe_site + clock_step_observe_original.size(), "AR7: the observer site is the guarded fld dword [0x680030] (6 bytes), resume right after it");
				check(step_observe_frames == 41 && step_observe_lines == 2048, "AR7: the observer covers frames 0..40 and at most 2048 lines per process");
				bool belt = true;
				for (unsigned n = 10; n <= 60; ++n) { float f = canonical_clock_step(n); std::uint32_t u; std::memcpy(&u, &f, 4); if (f != 1.0f / float(n) || !(f > 0.0f) || (n == 30 && u != 0x3d088889u) || (n == 60 && u != 0x3c888889u)) belt = false; }
				check(belt && canonical_clock_step(0) == 0.0f, "AR7 belt: 1/N for N in 10..60 (30 -> 0x3d088889, 60 -> 0x3c888889); no rate -> 0 (the stock step stays)");
			}
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
		// p060fix/AQ: the world-section gate is guarded again - by the join hold (the AE12 hold stays gone).
		check(std::any_of(guards.begin(), guards.end(), [](const auto& g) { return g.va <= world_gate_site && world_gate_site + world_gate_original.size() <= g.va + g.hex.size() / 2; }),
		      "AQ: one retail guard covers every byte of the world-section gate site 0x46f0a9 (the join hold's only write)");
		check(world_gate_original.size() == 7 && world_gate_original[0] == 0xf6 && world_gate_original[1] == 0x86 && world_gate_original[2] == 0x28 && world_gate_original[3] == 0x07 && world_gate_original[6] == world_gate_flag_bit,
		      "AQ: the site is the 7-byte `test byte ptr [esi+0x728], 2` (a call rel32 and two nops fit it exactly)");
		check(std::any_of(guards.begin(), guards.end(), [](const auto& g) { return g.va == message_clear; }) && std::any_of(guards.begin(), guards.end(), [](const auto& g) { return g.va == message_show; }),
		      "AQ: the native network message show 0x60a600 and expiry 0x60a690 are guarded (slot offsets +0x1c8 / +0x1f8; the solo-segment notice; since AQ2 the hold draws its own message)");
		// p060fix/AQ2: the admission site the hold arms at is the guarded synchronous area-load boundary call (inside the
		// zone loader 0x484ce0, after its `test byte ptr [ebp+0x220], 2` pending-load check, before the load).
		check(area_barrier_call == 0x484def && std::any_of(guards.begin(), guards.end(), [](const auto& g) { return g.va <= area_barrier_call && area_barrier_call + 5 <= g.va + g.hex.size() / 2; }),
		      "AQ2: the area-load boundary call 0x484def (where the hold is armed through the native add) is guarded");
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
	// AR8 (fable-ar8): the input-stream epoch marker - pure rules and the three sites.
	{
		using namespace join_in_progress_core;
		using namespace join_in_progress_rules;
		check(input_epoch_mark_prefix == 0xfffa0000u && input_epoch_copies == 2, "AR8: markers carry the 0xfffa prefix; two copies per epoch");
		check(input_epoch_mark(0xfffa0001u) && input_epoch_mark(0xfffaffffu) && !input_epoch_mark(0xfffa0000u) && !input_epoch_mark(0xfffb0001u) && !input_epoch_mark(0) && !input_epoch_mark(0x00080000u) && !input_epoch_mark(0xffffffffu),
		      "AR8: a marker is prefix | non-zero nonce; nonce 0, another prefix, zero, the pause code and all-ones are not markers");
		bool natives_clear = true;
		for (const auto code : native_event_codes) natives_clear &= !input_epoch_mark(code);
		check(natives_clear, "AR8: none of the four native lockstep event codes is a marker");
		check(input_epoch_mark_for(0x1234) == 0xfffa1234u && input_epoch_mark_for(0) == 0xfffa0001u && input_epoch_nonce(0xfffa1234u) == 0x1234 && input_epoch_mark(input_epoch_mark_for(0)),
		      "AR8: mark_for/nonce round-trip; a zero nonce becomes 1");
		// b2c review: the sender strips bit 15 of the code mask (0x5597f1), so the nonce is 15 bits - a marker is the same
		// dword in the sender's own ring and after the wire, and a nonce 0x8000 can no longer arrive as "not a marker".
		check(input_epoch_nonce_mask == 0x7fffu && input_epoch_mark_for(0x8000) == 0xfffa0001u && input_epoch_mark_for(0x9234) == 0xfffa1234u && input_epoch_nonce(0xfffa9234u) == 0x1234,
		      "AR8: the nonce is 15 bits (bit 15 of the mask is stripped on the wire); 0x8000 becomes 1, 0x9234 becomes 0x1234");
		bool wire_same = true;
		for (unsigned n = 0; n < 0x10000u && wire_same; ++n) { const auto m = input_epoch_mark_for(std::uint16_t(n)); wire_same = wire_code_mask(m) == m && input_epoch_mark(wire_code_mask(m)) && input_epoch_nonce(wire_code_mask(m)) == input_epoch_nonce(m); }
		check(wire_same, "AR8: for every nonce the marker survives the wire's mask strip unchanged and is still a marker with the same nonce");
		bool nonces_ok = true;
		std::uint16_t prev = 0;
		for (std::uint32_t seed = 0; seed < 70000u && nonces_ok; seed += 7u) { const auto n = next_input_epoch_nonce(seed, prev); nonces_ok = n != 0 && n != prev && !(n & 0x8000u); prev = n; }
		check(nonces_ok && next_input_epoch_nonce(0x5bd1u, 0) != 0 && next_input_epoch_nonce(0, 0) != 0, "AR8: the next nonce is never 0, never the previous one and never has bit 15 (70000 seeds)");
		bool nonces_collide_free = true;
		for (unsigned p = 1; p < 0x8000u && nonces_collide_free; ++p) { const auto seed = std::uint32_t(p ^ 0x5bd1u); const auto n = next_input_epoch_nonce(seed, std::uint16_t(p)); nonces_collide_free = n != 0 && n != p && !(n & 0x8000u); }
		check(nonces_collide_free, "AR8: when the seed would repeat the previous nonce, the fallback is a different non-zero 15-bit nonce (every previous value)");
		check(input_epoch_receive(0x00080000u, 7) == input_epoch_action::store && input_epoch_receive(0xfff31fffu, 7) == input_epoch_action::store, "AR8: a real record and a native event are stored as before");
		check(input_epoch_receive(0xfffa0007u, 0) == input_epoch_action::reset && input_epoch_receive(0xfffa0007u, 6) == input_epoch_action::reset, "AR8: a new nonce resets the participant's block");
		check(input_epoch_receive(0xfffa0007u, 7) == input_epoch_action::ignore, "AR8: the same nonce again (the extra copy) is ignored");
		check(input_epoch_counter_after_ignore(5) == 4 && input_epoch_counter_after_ignore(1) == 0 && input_epoch_counter_after_ignore(0) == 0, "AR8: an ignored copy gives the decoder's counter increment back, never below 0");
		// b2c review finding 2: the store's kind says which decoder kinds wrote the mask slot; only those may be read as a marker.
		{
			bool kinds_ok = input_epoch_store_kind_has_mask(0) && input_epoch_store_kind_has_mask(1) && input_epoch_store_kind_has_mask(3) && input_epoch_store_kind_has_mask(5);
			for (const auto k : {2u, 4u, 6u, 8u, 0x10u, 7u, 0x11u, 0xffffffffu}) kinds_ok = kinds_ok && !input_epoch_store_kind_has_mask(k);
			check(kinds_ok, "AR8: the decoder writes the mask for store kinds 0 (full), 1 (mask), 3 (mask+axes01) and 5 (mask+axes23) only; 2, 4, 6, 8 and 0x10 leave the slot stale");
			// Three peers: A's marker (a full record, kind 0) resets A's block; the next message is B's axes-only delta (kind 2)
			// decoded at the same stack depth, so its record's mask slot still holds A's marker. B's block must not be reset.
			input_epoch_memory memory;
			const auto a_mark = input_epoch_mark_for(0x2a2a);
			std::uint8_t decoder_record[0x28]{};
			std::memcpy(decoder_record + 4, &a_mark, 4);
			check(input_epoch_store_action(0, a_mark, memory.last[0]) == input_epoch_action::reset, "AR8: peer A's marker, a full record (kind 0), resets A's block");
			memory.last[0] = input_epoch_nonce(a_mark);
			std::uint32_t stale = 0; std::memcpy(&stale, decoder_record + 4, 4); // B's kind-2 decode wrote +2 and +8 only
			check(stale == a_mark && input_epoch_store_action(2, stale, memory.last[1]) == input_epoch_action::store && memory.last[1] == 0,
			      "AR8: peer B's axes-only delta (kind 2) reaches the store with A's marker still in the stale mask slot - it is stored, B's block is NOT reset");
			check(input_epoch_store_action(8, stale, memory.last[1]) == input_epoch_action::store && input_epoch_store_action(4, stale, memory.last[2]) == input_epoch_action::store && input_epoch_store_action(6, stale, memory.last[3]) == input_epoch_action::store && input_epoch_store_action(0x10, stale, memory.last[1]) == input_epoch_action::store,
			      "AR8: the same for the step-only (8), axes23 (4), both-axes (6) and body (0x10) kinds of every other peer");
			check(input_epoch_store_action(0, a_mark, memory.last[0]) == input_epoch_action::ignore && input_epoch_store_action(1, a_mark, memory.last[0]) == input_epoch_action::ignore,
			      "AR8: A's second copy (full, or a mask-carrying delta) is the repeat it always was");
			check(input_epoch_store_action(0, 0x00080000u, memory.last[0]) == input_epoch_action::store && input_epoch_store_action(3, 0x00080000u, memory.last[0]) == input_epoch_action::store,
			      "AR8: a real mask in a mask-carrying kind is stored as before");
			// b2c review finding 4: the nonce memory does not outlive the blocks. A new occupant of index 1 whose first nonce
			// equals the old occupant's last one is a new epoch after this PC's all-blocks reset or the session's end.
			memory.last[1] = 0x1357;
			check(input_epoch_store_action(0, input_epoch_mark_for(0x1357), memory.last[1]) == input_epoch_action::ignore, "AR8: without forgetting, a new peer's equal nonce would be taken for a repeat (no reset: the one-label offset)");
			memory.forget();
			check(memory.empty() && input_epoch_store_action(0, input_epoch_mark_for(0x1357), memory.last[1]) == input_epoch_action::reset && input_epoch_store_action(0, input_epoch_mark_for(0x2a2a), memory.last[0]) == input_epoch_action::reset,
			      "AR8: after the forget (every all-blocks ring reset, the session's end) every index's next marker resets its block, equal nonce or not");
		}
		check(input_ring_pending(0, 5) == 5 && input_ring_pending(3, 3) == 0 && input_ring_pending(298, 1) == 3 && input_ring_pending(300, 1) == 0, "AR8: pending records from read/write with the 300-slot wrap; an out-of-range index counts 0");
		// Sites: the two CALL replacements reach the retail reset and send-enable; the store's displaced prologue is 11 bytes.
		check(input_epoch_release_original[0] == 0xe8 && input_epoch_start_original[0] == 0xe8 && input_epoch_release_call + 5 + std::int32_t(0xfff451e3u) == input_ring_reset_all && input_epoch_start_call + 5 + std::int32_t(0xfff44798u) == input_send_enable,
		      "AR8: 0x613f68 calls 0x559150 and 0x6140b3 calls 0x558850 (the displaced rel32s)");
		check(input_epoch_store_original.size() == 11 && input_epoch_store_resume == input_epoch_store_site + 11 && input_epoch_store_original[0] == 0x51 && input_epoch_store_original[5] == 0x69 && input_epoch_store_original[7] == 0x84 && input_epoch_store_original[8] == 0x31,
		      "AR8: the store's prologue push ecx / mov eax,[esp+0xc] / imul eax,eax,0x3184 is re-executed by the bridge; resume at 0x55956b");
		for (const auto va : {input_epoch_release_call, input_epoch_start_call, input_epoch_store_site, input_post_event, input_block_reset, local_participant_index, 0x559ee7u})
			check(std::any_of(std::begin(input_epoch_guards), std::end(input_epoch_guards), [va](const auto& g) { return g.va <= va && va + 5 <= g.va + g.hex.size() / 2; }), "AR8: every site and helper the marker relies on is guarded");
		check(std::string(input_epoch_guards[0].hex).substr((input_epoch_release_call - input_epoch_guards[0].va) * 2, 10) == "e8e351f4ff" && std::string(input_epoch_guards[1].hex).substr((input_epoch_start_call - input_epoch_guards[1].va) * 2, 10) == "e89847f4ff" && std::string(input_epoch_guards[2].hex).substr(0, 22) == "518b44240c69c084310000",
		      "AR8: the guarded bytes at the three sites are the originals the transaction replaces");
		check(input_block_counter == 0x317e && gns_transport_rules::input_block_size == 0x3184 && gns_transport_rules::input_read == 0x3170 && gns_transport_rules::input_write == 0x3174 && gns_transport_rules::input_next == 0x317c,
		      "AR8: the block layout the hooks read (counter +0x317e next to the next frame +0x317c)");
	}
	// b2c-fix-crash (live-b2c X-2c N-2c): a marker that reaches a block this PC has already consumed from is late - ignored.
	{
		check(input_epoch_receive(0xfffa0007u, 0, 0) == input_epoch_action::reset && input_epoch_receive(0xfffa0007u, 0, 1) == input_epoch_action::late && input_epoch_receive(0xfffa0007u, 0, 299) == input_epoch_action::late,
		      "late marker: a new nonce after this PC consumed from the block (read index moved) is late, not a reset");
		check(input_epoch_receive(0xfffa0007u, 7, 1) == input_epoch_action::ignore && input_epoch_receive(0x00080000u, 0, 5) == input_epoch_action::store && input_epoch_receive(0xfff31fffu, 0, 5) == input_epoch_action::store,
		      "late marker: a repeat stays a repeat; real records and native events are stored whatever was consumed");
		check(input_epoch_store_action(0, input_epoch_mark_for(0x0801), 0, 1) == input_epoch_action::late && input_epoch_store_action(2, input_epoch_mark_for(0x0801), 0, 1) == input_epoch_action::store && input_epoch_store_action(0, input_epoch_mark_for(0x0801), 0, 0) == input_epoch_action::reset,
		      "late marker: X-2c's host marker 0801 at read 1 is late through the store action; a stale-slot kind is stored; read 0 still resets");
		check(input_epoch_store_action(0, input_epoch_mark_for(0x0801), 0) == input_epoch_action::reset && input_epoch_receive(input_epoch_mark_for(0x0801), 0) == input_epoch_action::reset,
		      "late marker: the three-argument forms (nothing consumed) are the AR8 rules unchanged");
		// X-2c's receiver sequence: one record consumed (read 1), the host's marker 0801 arrives -> late (ignored, nonce
		// remembered), its second copy -> the repeat it always was; the next all-blocks reset forgets, a new nonce resets.
		input_epoch_memory memory;
		const auto m = input_epoch_mark_for(0x0801);
		check(input_epoch_store_action(0, m, memory.last[0], 1) == input_epoch_action::late, "late marker: X-2c - the first copy is late");
		memory.last[0] = input_epoch_nonce(m);
		check(input_epoch_store_action(0, m, memory.last[0], 1) == input_epoch_action::ignore && input_epoch_store_action(0, m, memory.last[0], 0) == input_epoch_action::ignore,
		      "late marker: the remembered nonce makes the second copy a repeat, consumed or not");
		memory.forget();
		check(input_epoch_store_action(0, input_epoch_mark_for(0x0802), memory.last[0], 0) == input_epoch_action::reset && input_epoch_store_action(0, m, memory.last[0], 0) == input_epoch_action::reset,
		      "late marker: after this PC's next all-blocks reset a new nonce (or the same one) resets again");
	}
	// AS (b2c-fix-crash, live-b2c X-4p N-CRASH): the 0x23 mesh-pair report 0x607390 with a seat whose record was removed.
	{
		using namespace join_in_progress_rules;
		check(mesh_pair_decide(true, true) == mesh_pair_report::stock && mesh_pair_decide(true, false) == mesh_pair_report::partial && mesh_pair_decide(false, true) == mesh_pair_report::partial && mesh_pair_decide(false, false) == mesh_pair_report::none,
		      "AS: the stock function runs only with both records; one record is partial; neither is none");
		check(mesh_pair_mask(3, 2) == 0xc && mesh_pair_mask(0, 1) == 0x3 && mesh_pair_mask(4, 2) == 0x14 && mesh_pair_mask(35, 2) == 0xc,
		      "AS: the pair mask is (1<<a)|(1<<b) with shl-by-cl's 5-bit count (X-4p: edx 0xc = seats 3 and 2)");
		check(mesh_pair_index(0x8) == 3 && mesh_pair_index(0x4) == 2 && mesh_pair_index(0x1) == 0 && mesh_pair_index(0x6) == 1 && mesh_pair_index(0) == 4 && mesh_pair_index(0x10) == 4,
		      "AS: the handler's index of a payload half is its first set bit of four, else 4 (no record carries 4: the guard's partial/none branch)");
		const std::uint32_t payload = (1u << 3) << 16 | (1u << 2); // X-4p: m85 (seat 2) reports its mesh with the removed seat 3
		check(mesh_pair_index(payload >> 16) == 3 && mesh_pair_index(payload & 0xffff) == 2 && mesh_pair_mask(mesh_pair_index(payload >> 16), mesh_pair_index(payload & 0xffff)) == 0xc && mesh_pair_decide(false, true) == mesh_pair_report::partial,
		      "AS: X-4p's 0x23 payload names seats 3 (adding, removed) and 2 (sender) - mask 0xc as the crash registers (edx 0xc, ebx 4, ecx 2); partial: seat 2's record alone");
		check(mesh_pair_original.size() == 5 && mesh_pair_resume == mesh_pair_site + 5 && mesh_pair_original[0] == 0x53 && mesh_pair_original[1] == 0x8b && mesh_pair_original[2] == 0x5c && mesh_pair_original[3] == 0x24 && mesh_pair_original[4] == 0x08,
		      "AS: the entry's push ebx / mov ebx,[esp+8] (5 bytes) are re-executed by the bridge; resume at 0x607395");
		for (const auto va : {mesh_pair_site, mesh_pair_resume, 0x6073e4u, 0x6073f3u, 0x60741cu, mesh_pair_handler, 0x604e0au, 0x604e1eu, 0x6141e0u, record_by_index})
			check(std::any_of(std::begin(mesh_pair_guards), std::end(mesh_pair_guards), [va](const auto& g) { return g.va <= va && va + 4 <= g.va + g.hex.size() / 2; }), "AS: the entry, the faulting load, the manager compare and timer, the handler, both callers and both helpers are guarded");
		check(std::string(mesh_pair_guards[0].hex).substr(0, 10) == "538b5c2408" && std::string(mesh_pair_guards[1].hex).substr((0x6073e4 - 0x6073a0) * 2, 6) == "8b5e20" && std::string(mesh_pair_guards[1].hex).substr((0x6073f3 - 0x6073a0) * 2, 12) == "3b9720220000" && std::string(mesh_pair_guards[2].hex).substr((0x60741c - 0x6073f9) * 2, 12) == "d99f28220000",
		      "AS: the guarded bytes hold the entry the transaction replaces, the faulting load at 0x6073e4, the manager+0x2220 compare and the +0x2228 timer store");
		check(mesh_pair_manager_pending == 0x2220 && mesh_pair_manager_retries == 0x2238 && mesh_pair_manager_settle == 0x2228 && record_mesh_pending == 0x60 && clock_time_slot == 0x28,
		      "AS: the manager fields and the record's pending-mesh mask the guard writes are the stock function's (0x6073f3..0x60741c; 0x6073e4/0x6073f0 = record+0x40+0x20)");
	}
	lost_newcomer_units(); // p060fix/AR (+ b2d-fix-jip: the decision before the native add)
	restart_release_barrier_units(); // b2d-fix-jip: the restart-release barrier
	restart_execution_units(); // AR5
	std::printf("%d PASS / %d FAIL\n", passes, failures);
	return failures ? 1 : 0;
}
