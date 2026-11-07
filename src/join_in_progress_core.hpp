#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <span>

// [Online] JoinInProgress: the host's per-newcomer state machine, free of game memory (test/join_in_progress_test.cpp).
//
//   pending    admitted after Start Game; the record is unmarked (no broadcast, no input stream, no add) and the
//              newcomer waits in its "Adding player" lobby screen
//   adding     the next zone load is pending: the native add ran (0x1f to everyone, the mesh peers connect, the record
//              is marked); the host waits until the add's wait ends
//   streaming  the host has loaded the zone: one canonical live campaign goes to every remote participant ('G'/'H')
//   starting   the native sender completed every image and announced 0x26; all participants replay that campaign
//   joined     the newcomer loaded; the zone starts for everyone (the all-loaded barrier is released)
// A seat whose record leaves, or a stage that outlives its limit, is dropped (the host stops holding the barrier).
namespace join_in_progress_core
{
	// AR5 is independent of the admission/checksum cohort, which can finish before frame 0.
	enum class restart_execution_phase : std::uint8_t { off, restoring, world_ready };
	enum class restart_wait_policy : std::uint8_t { native, input_only, unclassified };
	struct restart_wait {
		bool readable = false, session = false, loading = false, modal = false;
		std::uint32_t flags = 0, phase = 0, mode = 0, run_permission = 0;
	};
	struct restart_execution {
		std::uint32_t generation = 0;
		restart_execution_phase phase = restart_execution_phase::off;
		bool restored = false, canonical_seen = false;
		void clear() { phase = restart_execution_phase::off; restored = canonical_seen = false; }
		void begin(bool late) { clear(); if (late) { ++generation; phase = restart_execution_phase::restoring; } }
		void image_returned() { if (phase == restart_execution_phase::restoring) restored = true; }
		void observe_ready(bool valid, bool loading, bool start_menu) {
			if (phase == restart_execution_phase::restoring && restored && valid && !loading && !start_menu)
				phase = restart_execution_phase::world_ready;
		}
		void canonical() { if (phase != restart_execution_phase::off) canonical_seen = true; }
		void area_load() { if (phase == restart_execution_phase::world_ready) clear(); }
		restart_wait_policy classify(const restart_wait& w) const {
			if (!w.session || phase != restart_execution_phase::world_ready || w.loading || w.modal) return restart_wait_policy::native;
			if (!w.readable || w.flags != 1 || (w.phase != 1 && w.phase != 2) || w.run_permission)
				return restart_wait_policy::unclassified;
			return restart_wait_policy::input_only;
		}
	};
	// Admission policy is a host setup choice, not deterministic simulation state.
	inline constexpr std::array<std::uint32_t,4> agreement_parameters{{1,4,0,0}};
	// A release can retain rule 26's spectators/lifecycle without admitting a
	// newcomer to a restored simulation. The host checks at request delivery,
	// before participant/controller allocation (including a race with START).
	inline bool admission_allowed(bool started, bool allow_late = false) { return !started || allow_late; }
	inline constexpr std::array<std::uint8_t, 8> started_refusal{{'L','C','L','J',1,0,0,0}};
	inline bool is_started_refusal(const void* body, std::size_t size) {
		return body && size == started_refusal.size() && std::memcmp(body, started_refusal.data(), size) == 0;
	}
	// The online roster's parentheses name the held hero, not the participant.
	// A solo-story spectator has no hero; keep the native name when one exists.
	template<class Format>
	int format_roster_hero(Format format, char* out, int capacity, const char* pattern, const char* hero)
	{
		return format(out, capacity, pattern, hero && hero[0] ? hero : "Spectating");
	}

	// Solo story segments (rule 26 revision 5): the first game seats one hero alone (Wolverine's opening, Magma's
	// mansion tours); the other players keep their participant and watch. When the story widens the party again, the
	// native zone-start seat binding (game vt+0x188) hands the heroes out in slot order, so each player would get
	// whichever hero sits in "their" slot. Every PC remembers which hero each controller held when the segment started,
	// and inside the segment and in the zone where it ends swaps a remembered hero back to its controller after the
	// native binding (segment_tracker below; other level changes keep the binding). The inputs are lockstep state only
	// (party names, the binding, the heroes' controllers, the session's controllers), so every PC swaps alike.
	struct seat_binding
	{
		std::array<std::array<char, 32>, 4> name{}; // the party slots' hero names ("" empty)
		std::array<std::int32_t, 4> controller{{-1, -1, -1, -1}}; // game+0x514: the slot's controller
		std::array<bool, 4> human{};                 // game+0x524: the slot is played by that controller
	};
	using owner_memory = std::array<std::array<char, 32>, 4>; // per controller: its hero's name, "" none
	inline bool same_name(const std::array<char, 32>& a, const std::array<char, 32>& b) { return a[0] && std::strncmp(a.data(), b.data(), a.size()) == 0; }
	// Every controller of the session holds a hero: the zone is "wide", and the binding is remembered.
	inline bool wide(const seat_binding& s, const std::array<bool, 4>& active)
	{
		for (int c = 0; c < 4; ++c)
		{
			if (!active[c]) continue;
			bool held = false;
			for (int k = 0; k < 4; ++k) held |= s.human[k] && s.controller[k] == c && s.name[k][0];
			if (!held) return false;
		}
		return true;
	}
	inline void remember(owner_memory& m, const seat_binding& s)
	{
		for (int k = 0; k < 4; ++k)
			if (s.human[k] && s.controller[k] >= 0 && s.controller[k] < 4 && s.name[k][0]) m[s.controller[k]] = s.name[k];
	}
	// Hand each remembered hero back to its controller (a swap when that controller holds another hero, a move when it
	// holds none - the solo hero goes to the player who had it); returns the changes made.
	inline int restore_owners(seat_binding& s, const owner_memory& m)
	{
		int moved = 0;
		for (int c = 0; c < 4; ++c)
		{
			if (!m[c][0]) continue;
			int want = -1, held = -1;
			for (int k = 0; k < 4; ++k)
			{
				if (same_name(s.name[k], m[c])) want = k;
				if (s.human[k] && s.controller[k] == c) held = k;
			}
			if (want < 0 || want == held) continue; // not in the party, or already theirs
			if (held >= 0)
			{
				std::swap(s.controller[want], s.controller[held]);
				std::swap(s.human[want], s.human[held]);
				++moved;
				continue;
			}
			// This player holds nothing (a solo segment): the solo hero goes to the player whose hero it was, unless
			// its current player remembers it as theirs too.
			const std::int32_t other = s.human[want] ? s.controller[want] : -1;
			if (other >= 0 && other < 4 && same_name(m[other], m[c])) continue;
			s.controller[want] = c;
			s.human[want] = true;
			++moved;
		}
		return moved;
	}

	// Which bindings get the owners (0.3.0d): a solo segment starts at the first binding that leaves an active
	// controller without a hero and ends at the next wide one. Only the bindings inside a segment (the solo hero goes
	// to the player who had it) and those of the zone where it ends (every remembered hero back to its player) are
	// changed. A normal level change keeps the heroes the players hold now (hero switches included): the native
	// binding already reads each hero's own controller there. The memory is the players' heroes when the segment
	// STARTED: the live heroes when seatParty changed the party (a switch made in the last wide zone counts), else
	// the last wide binding.
	struct segment_tracker
	{
		bool in_segment = false; // the last binding left an active player without a hero
		bool handback = false;   // the segment ended in this zone: its load and start bindings hand the heroes back
		owner_memory owners{};   // who had which hero when the segment started
		owner_memory wide{};     // the last wide binding's owners (fallback when a hero's controller can't be read)
	};
	enum class segment_step { none, started, inside, ended };
	// Before the owners are applied. from_load: the zone load's binding (0x486d3b) opens a new decision; the zone
	// start's refresh (0x422618) and in-zone refreshes keep it.
	inline segment_step observe_segment(segment_tracker& t, bool is_wide, bool from_load)
	{
		if (from_load) t.handback = false;
		if (!is_wide)
		{
			if (t.in_segment) return segment_step::inside;
			t.in_segment = true;
			t.owners = t.wide; // the caller replaces this with the heroes held when seatParty changed the party
			return segment_step::started;
		}
		if (t.in_segment)
		{
			t.in_segment = false;
			t.handback = true;
			return segment_step::ended;
		}
		return t.handback ? segment_step::ended : segment_step::none;
	}
	// After the binding (and any owners applied). A wide binding outside a segment is what the players hold now. The
	// zone start's refresh is followed by the native application to the heroes (game vt+0x184), so the hand-back is
	// done there (or at once when nothing had to move).
	inline void settle_segment(segment_tracker& t, const seat_binding& b, bool is_wide, bool from_load, int moved)
	{
		if (is_wide && !t.in_segment)
		{
			t.wide = {};
			remember(t.wide, b);
		}
		if (t.handback && (!from_load || moved == 0)) t.handback = false;
	}

	inline void rewind_campaign(std::uint8_t* wrapper, std::uint32_t payload)
	{
		// Serialization leaves this native wrapper cursor at the end. Only the payload crosses the wire, so
		// receivers get a fresh cursor; the host must rewind its own wrapper before replaying that same image.
		std::memcpy(wrapper + payload, &wrapper, sizeof(wrapper));
	}
	inline std::uint32_t campaign_transfer_mode(std::uint32_t host_mode)
	{
		// A playing New Game host stays in mode 1, whose native load path restores stats only. The newcomer needs
		// the same full campaign restore as Load Saved Campaign (mode 2), including the current area and mission state.
		return host_mode == 1 ? 2 : host_mode;
	}
	inline int campaign_restore_mode(int mode, bool cohort_restart) { return cohort_restart && mode == 0 ? 2 : mode; }
	// p060fix/AR2 (live-060zk ZK-3b1/3b2/3b2r/3b3): with a newcomer in the restart the host's cohort sync ("cohort
	// synchronized", the epoch's end: checksum protection, the removal window, the host-only ready/loaded bridges)
	// lands AFTER its first shared frame (`player N reached the shared start`, ~65 ms after frame 0). With every
	// newcomer lost before the restart nothing held it and the host synced ~70 ms BEFORE its frame 0 - in all four
	// failing runs, and in none of the passing ones - and whichever peer reached frame 0 first ended on the native
	// pause/ready screen. The host keeps its epoch until its own first shared frame when no newcomer is left to wait
	// for (the same point as the newcomer case); a cap bounds the wait if frame 0 never comes (every other peer gone).
	inline bool host_sync_waits_for_frame0(bool host, bool newcomers_left, bool frame0_seen, std::uint32_t waited_ms, std::uint32_t cap_ms = 10000)
	{
		return host && !newcomers_left && !frame0_seen && waited_ms < cap_ms;
	}
	inline bool participant_loaded(std::uint32_t grid, bool cohort_loading_observed, std::uint16_t frame)
	{
		// A full shared restart has no team-menu loaded grid. Its first simulation frame proves that the native
		// frame gate received every participant's fresh input after the network epoch reset to frame zero.
		return grid == 2 || (cohort_loading_observed && frame != 0);
	}
	struct cohort
	{
		bool active = false, announced = false, requested = false, started = false, saw_loading = false, controls_resumed = false;
		void begin() { active = true; announced = requested = started = saw_loading = controls_resumed = false; }
		bool announce() { if (!active || announced) return false; announced = true; return true; }
		bool request() { if (!active || requested || started) return false; requested = true; return true; }
		bool start() { if (!active || started) return false; requested = false; started = true; return true; }
		bool resume_controls(bool ready)
		{
			if (!active || !started || !saw_loading || !ready || controls_resumed) return false;
			controls_resumed = true; return true;
		}
		bool observe(bool loading, bool holding)
		{
			if (!active || !started) return false;
			if (loading) saw_loading = true;
			if (!saw_loading || loading || holding) return false;
			active = announced = requested = started = saw_loading = controls_resumed = false;
			return true;
		}
	};
	enum class stage : std::uint8_t { free, pending, adding, streaming, starting, joined };
	enum class action : std::uint8_t { none, run_add, send_image, send_start, finish, give_up };
	constexpr std::uint32_t add_limit_ms = 45000, image_limit_ms = 90000, load_limit_ms = 120000;

	inline const char* name(stage s)
	{
		switch (s)
		{
		case stage::free: return "free";
		case stage::pending: return "pending";
		case stage::adding: return "adding";
		case stage::streaming: return "streaming";
		case stage::starting: return "starting";
		case stage::joined: return "joined";
		}
		return "?";
	}

	struct seat
	{
		const void* record = nullptr;
		int index = -1;
		stage at = stage::free;
		std::uint32_t since = 0;
	};

	// What the load tick sees on the host for one seat.
	struct observation
	{
		bool zone_load_pending = false; // the zone manager has a load pending (or the load screen is up)
		bool add_waiting = false;       // the native add's "Adding player" wait is still on
		bool image_done = false;        // the newcomer's 'J' arrived (its record has the image bit)
		bool loaded = false;            // the newcomer reported its zone loaded
		bool host_loaded = false;       // the host's own load tick runs (its zone is loaded)
	};

	struct machine
	{
		std::array<seat, 4> seats{};
		bool started = false; // Start Game passed in this session

		seat* find(const void* record)
		{
			for (auto& s : seats) if (s.at != stage::free && s.record == record) return &s;
			return nullptr;
		}
		const seat* find(const void* record) const { return const_cast<machine*>(this)->find(record); }
		bool pending(const void* record) const { const auto* s = find(record); return s && s->at == stage::pending; }
		bool holding() const
		{
			for (const auto& s : seats) if (s.at == stage::adding || s.at == stage::streaming || s.at == stage::starting) return true;
			return false;
		}
		bool any() const { for (const auto& s : seats) if (s.at != stage::free) return true; return false; }

		// The host's add of a participant (0x608350) for record: true = defer it (the record becomes or stays pending).
		bool defer_add(const void* record, int index, bool active, std::uint32_t now)
		{
			if (!started || !record || active) return false;
			if (auto* s = find(record)) return s->at == stage::pending;
			for (auto& s : seats)
				if (s.at == stage::free) { s = seat{record, index, stage::pending, now}; return true; }
			return false; // no room: the native add runs (as stock)
		}
		void gone(const void* record) { if (auto* s = find(record)) *s = seat{}; }
		void reset() { seats = {}; started = false; }

		// One step for one seat; the caller performs the action and reports through the next observation.
		action step(seat& s, const observation& o, std::uint32_t now)
		{
			const std::uint32_t age = now - s.since;
			auto to = [&](stage next, action a) { s.at = next; s.since = now; return a; };
			switch (s.at)
			{
			case stage::pending:
				return o.zone_load_pending ? to(stage::adding, action::run_add) : action::none;
			case stage::adding:
				if (!o.add_waiting && o.host_loaded) return to(stage::streaming, action::send_image);
				return age > add_limit_ms ? to(stage::free, action::give_up) : action::none;
			case stage::streaming:
				if (o.image_done) return to(stage::starting, action::send_start);
				return age > image_limit_ms ? to(stage::free, action::give_up) : action::none;
			case stage::starting:
				if (o.loaded) return to(stage::joined, action::finish);
				return age > load_limit_ms ? to(stage::free, action::give_up) : action::none;
			default:
				return action::none;
			}
		}
	};

	// p060fix/AR: a newcomer lost between its admission and the shared start (gns_transport_core.hpp newcomer_lost).
	// The admission on the host: the boundary mask it announced and the newcomers it decided lost. A newcomer of it is a
	// seat at adding/streaming/starting, a pending seat named in the announced mask, or one already decided lost.
	struct admission_losses
	{
		std::uint8_t announced = 0, lost = 0;
		std::uint32_t lost_at = 0;
		unsigned owed_attempts = 0;
		void clear() { *this = {}; }
		static std::uint8_t bit(int index) { return index >= 1 && index <= 3 ? std::uint8_t(1u << index) : std::uint8_t(0); }
		bool newcomer(const machine& m, int index) const
		{
			const auto b = bit(index);
			if (!b) return false;
			if ((lost | announced) & b) return true;
			for (const auto& st : m.seats)
				if (st.index == index && (st.at == stage::adding || st.at == stage::streaming || st.at == stage::starting)) return true;
			return false;
		}
		// The newcomers still joining besides `except` (and those already decided lost).
		std::uint8_t remaining(const machine& m, int except) const
		{
			std::uint8_t out = 0;
			for (const auto& st : m.seats)
				if (st.at == stage::adding || st.at == stage::streaming || st.at == stage::starting) out |= bit(st.index);
			return std::uint8_t(out & ~bit(except) & ~lost);
		}
		void decided(int index, std::uint32_t now) { if (!lost) lost_at = now ? now : 1; lost |= bit(index); }
	};
	// The restart has begun on this PC: its 0x26 was broadcast (host) or handled, or its native campaign start entered.
	inline bool restart_removal_held(bool active, bool announced, bool requested, bool started) { return active && (announced || requested || started); }
	// Every peer refrains from removing a lost participant inside its own restart: from the restart's 0x26 until its
	// restored zone runs again (the mode-2 load returned and the pump next sees no load). Live ZJ-7j: the guest's
	// timeout removal landed after resetgame, before the mode-2 load. Afterwards the restarted frame 0 waits for the
	// lost participant on every peer and the agreed cut frame removes it there (never held to the cohort sync: a host
	// seat waiting at `starting` for a dead newcomer would hold that sync for its 120 s load limit).
	inline bool removal_window(bool active, bool announced, bool requested, bool started, bool zone_back)
	{
		return restart_removal_held(active, announced, requested, started) && !(started && zone_back);
	}
	// Once the host announced a positive admission (every old peer holds for it), a cohort restart follows whatever
	// happens to the newcomers: the held worlds are only made equal again by the one image. When every newcomer of the
	// admission was lost before its image, the host still captures one for the existing participants (owed restart).
	constexpr std::uint32_t owed_add_wait_ms = 3000; // the native "Adding player" wait normally ends with the removal
	constexpr unsigned owed_attempts_max = 5;
	inline bool owed_restart_due(std::uint8_t lost, bool restart_begun, bool sender_running, bool newcomer_left, bool other_participants,
		bool zone_loading, bool add_waiting, std::uint32_t since_ms, unsigned attempts)
	{
		return lost && !restart_begun && !sender_running && !newcomer_left && other_participants && !zone_loading &&
			(!add_waiting || since_ms >= owed_add_wait_ms) && attempts < owed_attempts_max;
	}

	// Canonical cohort restart (p060fix/AE, join_in_progress_rules.hpp "Canonical cohort restart"): the camera singleton
	// goes back to its constructed image on every peer before the native campaign start, and the synchronized RNG is
	// reseeded by every peer at the first shared simulation frame (frame 0 of the restarted network epoch).
	struct restart_reset
	{
		bool snapshot = false;  // the pristine camera image exists (taken when the game constructed the singleton)
		bool armed = false;     // a cohort restart's load ran: the next frame-0 input run reseeds
		bool reseeded = false;
		unsigned restores = 0, reseeds = 0;
		void arm() { armed = true; reseeded = false; }
		void disarm() { armed = false; }
		// At the input-manager run of a frame (the gate passed: every participant's input for this frame is here).
		bool should_reseed(std::uint16_t frame)
		{
			if (!armed || reseeded || frame != 0) return false;
			reseeded = true; armed = false; ++reseeds; return true;
		}
		// Before the native campaign start of a cohort restart: restore only with an image, only for a restart.
		bool should_restore(bool cohort_restart) const { return snapshot && cohort_restart; }
		// P1: inside a late restart's zone load, each deterministic segment (LoadMap entry, zone start) starts from the
		// native seed: armed by the cohort load, before frame 0 took the final epoch.
		unsigned segment_reseeds = 0;
		bool should_reseed_segment() { if (!armed || reseeded) return false; ++segment_reseeds; return true; }
	};
	// Follow-up 3: the zone-start camera anchor comes from the party list only during an armed late restart.
	inline bool camera_anchor_from_party(bool enabled, bool late_restart, bool armed) { return enabled && late_restart && armed; }
	// Follow-up 5: the dormant-entity respawn (CSmartEntMgr update) is held from an armed late restart's zone load to
	// its frame-0 epoch on every peer; a lobby START (never armed) and [Dev] LateJoinRestartReset=0 keep retail.
	inline bool smart_hold_wanted(bool enabled, bool late_restart, bool armed) { return enabled && late_restart && armed; }
	// p060fix/AJ: the first dormant-entity sweep after the first shared frame examines every record on every peer. The
	// game update decides `all` by `distance2(eye, game+0x1f0) > 10000` and stores the eye into game+0x1f0 after each
	// update; the zone-loaded finish puts the far marker (1e30, 0, 0) there so the first update after a load sweeps
	// everything, and the one world tick the hold spans lands before that finish on some peers and after it on others
	// (live-060zb ZB-3Pp: the joiner swept 115 records at frame 0, the host 40 per frame over frames 2..11). At the
	// frame-0 release every peer puts the marker back: `anchor` is the 12-byte vector; returns true when it already
	// held the marker (a peer whose held tick came before the finish).
	inline constexpr std::uint32_t respawn_far_marker_bits = 0x7149f2ca; // igVec3f(1e30f, 0, 0), the constant 0x4685b0 pushes
	inline bool respawn_sweep_marker(std::uint32_t* anchor)
	{
		const bool had = anchor[0] == respawn_far_marker_bits && anchor[1] == 0 && anchor[2] == 0;
		anchor[0] = respawn_far_marker_bits; anchor[1] = 0; anchor[2] = 0;
		return had;
	}
	// p060fix/AJ2: a party hero's periodic event (static kind 0x6d5e30, +0x3d8 bit 0x20 pending) follows its AI bit:
	// a player hero carries one, an AI hero none. At the frame-0 epoch every peer clears the pending bit of an AI hero
	// that still has one (a seat released at runtime) and drops its queued events in the rebuild; a player hero
	// without one is only reported (it never arises from the drop path; the host is the reference either way).
	struct hero_event_decision { bool clear_pending, drop_events, missing; };
	inline hero_event_decision hero_event_rule(std::uint8_t flags, std::uint8_t ai_bit = 0x01, std::uint8_t pending_bit = 0x20)
	{
		const bool ai = (flags & ai_bit) != 0, pending = (flags & pending_bit) != 0;
		return {ai && pending, ai, !ai && !pending};
	}
	// p060fix/AK (live-060zd ZD-3P-E2p, three peers at E2): every peer reached frame 0 with identical draws, brains,
	// heroes and timed-event queue, yet at frame 1 six of the fourteen AI actors of mansion2_2 re-armed their update
	// every frame (a positive move speed) on the newcomer and six OTHERS on the host and the lobby guest, so the same
	// brains thought on different frames (actor 0x115: frame 14 vs 17) and the draws parted (82 vs 78 at frame 30).
	// The epoch clears the move intent of every actor that is a CActor (its vtable), has no controller (+0x3d4 = -1)
	// and is not a party hero; what it found is counted for the log. Party-size and exit independent: every live NPC
	// on every peer, nothing keyed on a seat, a role or a PC.
	struct npc_intent_decision { bool considered = false, had_speed = false, had_fast_flag = false, had_follow = false; };
	inline npc_intent_decision npc_move_intent_rule(std::uint32_t vtable, std::uint32_t actor_vtable, std::int32_t controller, bool party_hero, std::uint8_t flags6, std::uint8_t fast_bit, float speed_a, float speed_b, std::uint32_t follow)
	{
		if (vtable != actor_vtable || party_hero || controller != -1) return {};
		return {true, speed_a > 0.0f || speed_b > 0.0f, (flags6 & fast_bit) != 0, follow != 0};
	}
	// A second newcomer admitted at the same zone load rides the campaign transfer the first one started (the native
	// sender images every participant and the 0x26 broadcast restarts the cohort once every image is in); only a
	// sender that is not this late restart's refuses the image.
	inline bool image_rides_running_transfer(bool sender_running, bool late_restart) { return sender_running && late_restart; }

	// p060fix/AQ: the join hold ([Online] JoinHoldSeconds, default 10 since AQ2, 0..15, 0 = off). From the host's image capture
	// (and an existing participant's late-restart 0x47) until this PC's own cohort restart begins, the game update's
	// world section is skipped, so no peer runs game frames the image does not contain (live-060zg ZG-C-3Pp2: the
	// host simulated 4.36 s alone, 493 draws, seven hits, between the capture and the restart). The window stays alive
	// (main loop, scheduler, network, rendering, the native network message "A player is joining..."). The cap bounds
	// the wait for the newcomers' images only: once every newcomer is ready (the host saw every image arrive) the hold
	// lasts to the restart, which the native start reaches after its fixed 1 s network wait (0x60814b: 0x60a920(1.0)),
	// bounded by restart_limit_ms. At the cap, or when every newcomer left, the PC simulates as before the hold.
	// p060fix/AQ2: on the host the hold is ARMED at the admission (the native add at the synchronous area-load boundary,
	// before the zone loads) - the world section is skipped from then on - and OPENS (its clock, line and message) at
	// the first pump after that zone load: live-060zi ZI-1bp, the host ran ~30 world updates (+55 draws, +1.0 s game
	// time) alone between its zone-load end and the image capture. While armed the cap does not apply (the zone load
	// is disk time, not the newcomer's); armed_limit_ms bounds it, and every newcomer gone ends it as usual.
	// The default: 10 s (Owen, 2026-11-01 01:25: "should likely be 10 for a default just to be safe" - internet joins
	// can be slower than the LAN evidence). Measured with the AQ2 opening over the live-060zd..zi host logs, the longest
	// zone-load-end -> every-newcomer-ready time is 6.40 s (live-060ze ZE-4PE2p: 1.12 s zone-load end -> capture +
	// 5.27 s capture -> ready): 3.6 s of margin; AQ's 6 would have been 0.40 s short.
	inline constexpr int join_hold_default_seconds = 10, join_hold_max_seconds = 15;
	inline constexpr std::uint32_t join_hold_restart_limit_ms = 4000, join_hold_armed_limit_ms = 30000;
	inline int join_hold_seconds(int configured)
	{
		return configured < 0 ? 0 : configured > join_hold_max_seconds ? join_hold_max_seconds : configured;
	}
	struct join_hold
	{
		enum class role : std::uint8_t { none, host, member };
		// What a poll or a release reports; none = keep holding.
		enum class event : std::uint8_t { none, ready, cap, restart_late, gone, restart, session };
		role who = role::none;
		std::uint32_t cap_ms = std::uint32_t(join_hold_default_seconds) * 1000u;
		std::uint32_t since = 0, admitted_at = 0, ready_at = 0, ended_ms = 0, armed_ms = 0;
		int newcomers = 0, ready = 0;
		bool all_ready = false;
		bool armed = false; // AQ2: held since the admission, clock not started (the zone load has not ended yet)
		event last = event::none;
		unsigned holds = 0;
		bool active() const { return who != role::none; }
		void configure(int seconds) { cap_ms = std::uint32_t(join_hold_seconds(seconds)) * 1000u; }
		bool enabled() const { return cap_ms != 0; }
		// Opens the hold; false when it is off, already open or the role is none (a newcomer never holds).
		bool begin(role r, int count, std::uint32_t now)
		{
			if (!enabled() || active() || r == role::none) return false;
			who = r; admitted_at = since = now; ready_at = 0; ended_ms = 0; armed_ms = 0; newcomers = count < 1 ? 1 : count; ready = 0; all_ready = false;
			armed = false; last = event::none; ++holds;
			return true;
		}
		// AQ2: the host's hold at the admission: held at once, the clock starts at open() (the zone load's end).
		bool arm(role r, int count, std::uint32_t now)
		{
			if (!begin(r, count, now)) return false;
			armed = true;
			return true;
		}
		// AQ2: the zone load ended: the hold's clock (cap, elapsed, the logged hold time) starts now. False when not armed.
		bool open(std::uint32_t now)
		{
			if (!active() || !armed || member_expired(now)) return false;
			armed = false; armed_ms = now - since; since = now;
			return true;
		}
		void add_newcomer() { if (active()) ++newcomers; }
		bool member_expired(std::uint32_t now) const { return who == role::member && now - admitted_at >= cap_ms + join_hold_restart_limit_ms; }
		std::uint32_t elapsed(std::uint32_t now) const { return now - since; }
		// One pump. waiting / done: the host's admitted newcomers before / with their image; a member passes -1 (it
		// cannot see the seats; only the cap, its own restart and the session's end end its hold).
		event poll(std::uint32_t now, int waiting, int done)
		{
			if (!active()) return event::none;
			if (member_expired(now)) return close(event::cap, now); // open() must never renew the admission safety deadline
			if (who == role::host && waiting >= 0)
			{
				ready = done;
				if (waiting == 0 && done == 0) return close(event::gone, now);
				if (waiting == 0 && !all_ready)
				{
					all_ready = true; ready_at = now;
					if (done > newcomers) newcomers = done;
					return event::ready;
				}
			}
			if (armed) return elapsed(now) >= (who == role::member ? cap_ms + join_hold_restart_limit_ms : join_hold_armed_limit_ms) ? close(event::cap, now) : event::none;
			if (!all_ready && elapsed(now) >= cap_ms) return close(event::cap, now);
			if (all_ready && now - ready_at >= join_hold_restart_limit_ms) return close(event::restart_late, now);
			return event::none;
		}
		// This PC's restart begins (deferred_campaign_start) or the session ended.
		event release(event why, std::uint32_t now) { return active() ? close(why, now) : event::none; }
		event close(event why, std::uint32_t now)
		{
			ended_ms = elapsed(now);
			// The native start runs only once every image is in (the 0x26 / J completion), on every role.
			if (why == event::restart) { all_ready = true; if (ready < newcomers) ready = newcomers; }
			who = role::none; last = why; armed = false;
			return why;
		}
	};
	// The on-screen text (the native timed network message stores the pointer, so these are static).
	inline const char* join_hold_text(int newcomers)
	{
		switch (newcomers)
		{
		case 0: case 1: return "A player is joining...";
		case 2: return "2 players are joining...";
		default: return "3 players are joining...";
		}
	}
	// P2 (follow-up 4): the AI brain free ring is rebuilt over the FREE slots in ascending order (read 0, write = free
	// count mod slots, free count = the slots not held in the bitmap words, one word per 32 slots), so every peer
	// allocates the same next slots even when a warm host still holds one (probe round: slot 31 held, bits
	// 0x80000007). One entry per controller slot (the stock 32 or E4's 96). Returns the number of held slots.
	inline unsigned rebuild_brain_ring(std::span<const std::uint32_t> held, std::span<std::uint32_t> ring, std::int32_t& write, std::int32_t& read, std::int32_t& free_count)
	{
		const auto slots = static_cast<std::uint32_t>(ring.size());
		unsigned held_count = 0, n = 0;
		for (std::uint32_t slot = 0; slot < slots; ++slot)
		{
			if (held[slot / 32] & (1u << (slot % 32))) { ++held_count; continue; }
			ring[n++] = slot;
		}
		for (unsigned i = n; i < slots; ++i) ring[i] = 0;
		read = 0; write = std::int32_t(n % slots); free_count = std::int32_t(n);
		return held_count;
	}
	inline unsigned rebuild_brain_ring(std::uint32_t bitmap, std::array<std::uint32_t, 32>& ring, std::int32_t& write, std::int32_t& read, std::int32_t& free_count)
	{ return rebuild_brain_ring(std::span<const std::uint32_t>(&bitmap, 1), std::span<std::uint32_t>(ring), write, read, free_count); }
	// The rebuild runs only on a pool layout this module knows exactly (actor_ai_layout.hpp ring offsets): the stock
	// 32-controller pool at stage 0, the same pool at stage 1 (the standalone ActorPools=124 install observes the pop
	// and keeps the stock layout), or E4's relocated 96-controller pool at stage 2. Anything else is left as it is.
	inline bool brain_ring_reset_wanted(std::uint32_t stage, std::int32_t, std::uint32_t controllers)
	{ return (stage <= 1 && controllers == 32) || (stage == 2 && controllers == 96); }
	inline std::uint32_t brain_ring_held_words(std::uint32_t controllers) { return (controllers + 31) / 32; }
	// p060fix/AP: a slot is held when its bit is set in the held words (one word per 32 slots); a slot past the words
	// counts as held (nothing is reset that the bitmap does not show free).
	inline bool brain_slot_held(std::span<const std::uint32_t> held, std::uint32_t slot)
	{ return slot / 32 >= held.size() || (held[slot / 32] & (1u << (slot % 32))) != 0; }
	// Follow-up 7: the timed-event queue (every actor update is one of its entries) is rebuilt in a canonical order at
	// the late restart's epochs on every peer. One entry = one heap pair resolved to its record.
	struct event_entry
	{
		float time = 0; std::uint32_t index = 0;           // the heap pair
		std::uint32_t handle = 0, event = 0;              // the record: owner handle (+0), event object (+4)
		std::uint32_t owner = 0;                           // the resolved owner entity (0: none)
		std::uint8_t payload[16]{};                        // the record's +8..+23
		bool token = false, live = false;                  // a positive entity token; resolved to an entity
		std::uint32_t kind_vtable = 0;                     // follow-up 14 (S2): the event object's vtable when it lies outside the image (0 otherwise)
	};
	// Follow-up 9: the event object is either a member of its owner (the actor update at +0x320 and its siblings:
	// its OFFSET is the process-independent name) or a static kind object in the executable's .data (round 6: the
	// "moved" notification 0x6d7c5c, the table at 0x6d55a4..; an absolute address, identical in every process because
	// XMen2.exe is never relocated). `event - owner` for a static kind is a per-process number (the actor pool moves
	// between boots), so the key is the offset inside the owner, else the absolute address; every offset (< 0x2000)
	// sorts before every address (>= 0x400000).
	constexpr std::uint32_t event_member_span = 0x2000;
	// Follow-up 14 (review S2): an event object outside the executable image is a heap allocation whose address is
	// per-process (round 10 `zone start before rebuild`: kinds 33a4568 / 3454568 on the same owners); its vtable
	// pointer is static and names the class, so that is the key (the raw address only when no vtable was read).
	constexpr std::uint32_t image_lo = 0x401000, image_hi = 0xa40000;
	inline bool event_kind_in_image(std::uint32_t event) { return event >= image_lo && event < image_hi; }
	inline std::uint32_t event_kind_key(const event_entry& e)
	{
		const std::uint32_t offset = e.event - e.owner;
		if (e.owner && offset < event_member_span) return offset;
		if (event_kind_in_image(e.event) || !e.kind_vtable) return e.event;
		return e.kind_vtable;
	}
	// Canonical order: time, then the owner's DENSE index (never its generation: the warm host's generations are
	// higher than a cold joiner's for the same slots), then the event's kind key (offset inside its owner, or the
	// static kind's address), then the payload bytes, then the raw handle (non-token owners).
	inline bool canonical_event_less(const event_entry& a, const event_entry& b, std::uint32_t index_mask)
	{
		if (a.time != b.time) return a.time < b.time;
		const std::uint32_t ia = a.token ? (a.handle & index_mask) : 0xffffffffu, ib = b.token ? (b.handle & index_mask) : 0xffffffffu;
		if (ia != ib) return ia < ib;
		const std::uint32_t oa = event_kind_key(a), ob = event_kind_key(b);
		if (oa != ob) return oa < ob;
		const int c = std::memcmp(a.payload, b.payload, sizeof a.payload);
		if (c) return c < 0;
		return (a.token ? 0u : a.handle) < (b.token ? 0u : b.handle); // a token's raw value (its generation) never orders
	}
	// An entry is dropped only when its owner is a positive entity token that no longer resolves (the old world's
	// actors on a warm host: their fire is a no-op that still shapes the heap). Everything else is kept.
	inline bool event_entry_dropped(const event_entry& e) { return e.token && !e.live; }
	inline bool positive_entity_token(std::uint32_t handle) { return handle != 0 && (handle >> 31) == 0; }
	inline bool event_queue_rebuild_wanted(bool enabled, bool late_restart, bool armed) { return enabled && late_restart && armed; }
	// Follow-up 9 (round 6): `cohort synchronized` landed BEFORE frame 0 on the joiner (1.3 s / 245 ms earlier) and
	// AFTER it on the host, and it cleared `late_restart`, so the joiner's frame-0 epoch skipped every step gated on
	// it (the FPU line was host-only); and should_reseed() consumed `armed` before the queue rebuild asked for it, so
	// the frame-0 rebuild never ran on either peer (rebuilds=2 at the frame-0 epoch). While a frame-0 epoch is still
	// armed, `cohort synchronized` keeps the flag: the epoch itself ends the late restart on every peer.
	inline bool cohort_sync_clears_late_restart(bool armed) { return !armed; }
	// Follow-up 8: the manager's pump slot composes with the one module that hooks it (xml1_turrets): retail, or that
	// module's installed hook (never 0, never anything else).
	inline bool event_pump_slot_accepted(std::uint32_t slot, std::uint32_t native, std::uint32_t module_hook) { return slot == native || (module_hook != 0 && slot == module_hook); }
	// Follow-up 14 (AE15): a party hero's ground-attach callback (CActor OnAttached 0x429710: the settle nudge draws)
	// is suppressed only inside an armed late restart with the reset on - from the mode-2 load to the frame-0 epoch,
	// which consumes `armed` before it re-probes every party hero's ground on every peer. A lobby START never arms.
	inline bool attach_suppressed(bool enabled, bool late_restart, bool armed) { return enabled && late_restart && armed; }
	inline bool party_member(std::uint32_t handle, const std::uint32_t* party, unsigned party_count, std::uint32_t index_mask)
	{
		if (!positive_entity_token(handle)) return false;
		for (unsigned i = 0; i < party_count; ++i) if (party[i] && (party[i] & index_mask) == (handle & index_mask)) return true;
		return false;
	}
	// The camera's derived spans (rules camera_derived_spans) back to the pristine image's bytes; returns how many
	// dwords changed. Pure: both images are plain byte arrays of `bytes` bytes.
	template<class Spans>
	inline unsigned restore_camera_spans(std::uint8_t* camera, const std::uint8_t* pristine, const Spans& spans, std::uint32_t bytes)
	{
		unsigned changed = 0;
		for (const auto& s : spans)
		{
			if (!s.bytes || s.at + s.bytes > bytes) continue;
			for (std::uint32_t off = s.at; off + 4 <= s.at + s.bytes; off += 4)
				if (std::memcmp(camera + off, pristine + off, 4) != 0) ++changed;
			std::memcpy(camera + s.at, pristine + s.at, s.bytes);
		}
		return changed;
	}
	// Follow-up 12 (round 8): at the frame-0 epoch the rebuild also drops a queued "moved" event (the static kind
	// 0x6d7c5c) whose owner is one of the party heroes (by dense index), after their moved-check state was made
	// canonical (last-known = current, pending bit clear) on every peer; other owners' moved events are kept.
	inline bool moved_event_dropped(const event_entry& e, std::uint32_t moved_kind, const std::uint32_t* party, unsigned party_count, std::uint32_t index_mask)
	{
		if (!e.token || event_kind_key(e) != moved_kind) return false;
		for (unsigned i = 0; i < party_count; ++i) if (party[i] && (party[i] & index_mask) == (e.handle & index_mask)) return true;
		return false;
	}
	// P3: the synchronized-draw caller trace. The wrappers 0x560190 (tail jump), 0x5601b0 (call, returns 0x5601bd) and
	// 0x5601e0 (call after push esi/edi, returns 0x5601fb or 0x56023f) are normalised to the code that asked for
	// randomness: [sp] is the return address at 0x560090 entry, sp[1] / sp[3] the outer frames.
	inline std::uint32_t normalise_draw_caller(const std::uint32_t* sp, std::uint32_t float_return, std::uint32_t int_return_a, std::uint32_t int_return_b)
	{
		const std::uint32_t ret = sp[0];
		if (ret == float_return) return sp[1];
		if (ret == int_return_a || ret == int_return_b) return sp[3];
		return ret;
	}
	// Follow-up 6: a and b name the actor behind a draw where the caller is known to decide per actor (the AI think
	// branch 0x51e3c6/0x51e3e0: the actor's handle and controller; the touch-nudge 0x42974d..73: the touched entity's
	// handle and its packed x/y). 0 when not applicable.
	// AE7b: c and d add, for the think branch, the brain's next think time (the value compared at 0x51e3ab, as float
	// bits) and the actor's distance to the camera eye (integer units).
	struct draw_entry_record { std::uint32_t caller = 0, count = 0; std::uint16_t frame = 0; std::uint8_t phase = 0; std::uint32_t a = 0, b = 0, c = 0, d = 0; };
	// Which register holds the actor at the draw (pushad order: sp[-9] = EDI, sp[-8] = ESI, where sp -> the return address).
	enum class draw_actor_register { none, edi, esi };
	inline draw_actor_register draw_actor_of(std::uint32_t caller)
	{
		if (caller == 0x51e3c6 || caller == 0x51e3e0) return draw_actor_register::edi; // the AI think branch: EDI = the actor
		if (caller == 0x42974d || caller == 0x429763 || caller == 0x429773) return draw_actor_register::esi; // the touch nudge: ESI = the entity
		return draw_actor_register::none;
	}
	constexpr unsigned draw_ring_size = 32768; // power of two; ~384 KiB, static (the probe round overflowed 8192 in phase 0)
	struct draw_trace
	{
		std::array<draw_entry_record, draw_ring_size> ring{};
		unsigned head = 0, flushed = 0, overflow = 0;
		std::uint8_t phase = 0;
		void note(std::uint32_t caller, std::uint32_t count, std::uint16_t frame, std::uint32_t a = 0, std::uint32_t b = 0, std::uint32_t c = 0, std::uint32_t d = 0)
		{
			if (head - flushed >= draw_ring_size) { ++overflow; ++flushed; } // the oldest unflushed entry is lost, counted
			ring[head++ & (draw_ring_size - 1)] = {caller, count, frame, phase, a, b, c, d};
		}
		unsigned pending() const { return head - flushed; }
	};
	// The pristine image is only meaningful when taken right after construction: never from a running camera.
	inline bool snapshot_wanted(bool constructed_before_the_fix) { return !constructed_before_the_fix; }
	// The 0x47 prepare body's two unused tail bytes (body+6): "LJ" while the host restarts a running session for a
	// late joiner, 0 at a lobby START. Every host with the fix writes them, so a receiver never sees stack residue.
	constexpr std::uint16_t late_restart_marker = 0x4a4c;
	constexpr std::size_t prepare_body_size = 8, prepare_marker_at = 6;
	inline std::uint16_t prepare_stamp(bool late_restart) { return late_restart ? late_restart_marker : 0; }
	inline bool prepare_is_late_restart(const void* body, std::size_t size)
	{
		if (!body || size < prepare_body_size) return false;
		std::uint16_t marker = 0;
		std::memcpy(&marker, static_cast<const std::uint8_t*>(body) + prepare_marker_at, sizeof marker);
		return marker == late_restart_marker;
	}

	// First-shared-frame agreement across roles (the live-060y evidence and the offline tool scripts/jip_components.py):
	// the producer's inputs as the fix logs them ("jip checksum hero" / "jip checksum produced" lines).
	struct component_sample
	{
		std::uint16_t frame = 0;
		unsigned heroes = 0;
		std::array<std::array<std::uint32_t, 6>, 8> transform{};
		std::array<bool, 8> have{};
		std::uint32_t hero_sum = 0, camera = 0, rng_draws = 0, hash = 0;
		bool produced = false;
	};
	enum disagreement : unsigned { agree = 0, hero_differs = 1, camera_differs = 2, rng_differs = 4, frame_differs = 8, incomplete = 16 };
	inline unsigned compare_components(const component_sample& a, const component_sample& b)
	{
		unsigned d = agree;
		if (!a.produced || !b.produced) d |= incomplete;
		if (a.frame != b.frame) d |= frame_differs;
		if (a.heroes != b.heroes || a.hero_sum != b.hero_sum) d |= hero_differs;
		for (unsigned i = 0; i < 8; ++i)
		{
			if (a.have[i] != b.have[i]) { d |= hero_differs; continue; }
			if (a.have[i] && a.transform[i] != b.transform[i]) d |= hero_differs;
		}
		if (a.camera != b.camera) d |= camera_differs;
		if (a.rng_draws != b.rng_draws) d |= rng_differs;
		// The retail hash is the modulo-32 sum; equal hashes with unequal terms (live-060y run 4 frame 30) are still a
		// disagreement: the next period breaks.
		return d;
	}
	// The additive identity the retail producer uses; a sample must reconcile or its parse is wrong.
	inline bool reconciles(const component_sample& s) { return s.produced && (s.hero_sum + s.camera + s.rng_draws) == s.hash; }

	// A 0x25 mesh reply (stock 0x604e93 -> 0x607210) binds the record of the participant being added (manager+0x221c)
	// to the reply's sender. The fix completes every mesh at once (one 0x1f each), so the "participant being added" is
	// only the last one completed and a reply from ANOTHER participant would rebind that record to the wrong peer and
	// free its own (4-player run Q1a: two records on one peer, the third peer unbound, the 0x5b handler's null record).
	enum class mesh_reply { stock, already_bound, refused };
	inline mesh_reply decide_mesh_reply(bool sender_bound_to_a_record, bool target_record, bool target_peer_elsewhere)
	{
		if (sender_bound_to_a_record) return mesh_reply::already_bound;   // nothing to rebind (and nothing to free)
		if (target_record && target_peer_elsewhere) return mesh_reply::refused; // not this record's participant
		return mesh_reply::stock;                                          // no record, or one still without a peer
	}

	// AR6 (fable-ar6): the pause-code guard at the lockstep's controller apply. The record of the first lockstep frames of an
	// epoch (frame u16 < pause_guard_frames; every epoch starts at frame 0 - session start, every area load, every cohort
	// restart) is applied WITHOUT UI code 0x13 (the pause code the mapper 0x61a810 raises for action 0x11, ESC/Start). The
	// controller apply 0x550fb0 copies the record's code mask into cur (+0x2f8) after moving cur to prev (+0x374); the menu
	// manager (0x5dae30 -> 0x5d4970 -> vt+0x18 0x550b70 = (cur ^ prev) & cur) opens the pause menu on a rising edge of bit
	// 0x13, and in an online session that is the native online pause 0x60c230(1) (every record Not Ready, released only by
	// a 0x4b Ready from every participant). live-b2a/060zl/060zn single-lost-newcomer shapes: the lobby guest's frame-0
	// record carried the bit, the peer FIRST at frame 0 stalled at the gate with the edge latched and paused natively while
	// the other peer applied frames 0..k back to back and never saw it - a one-sided pause nobody can release. The guard is a
	// function of the record alone (frame, mask), so every peer applies the same filtered record: consumed inputs, checksums
	// and the RNG stay equal; the ring slot itself is untouched (delta encoding and resends see the stock bytes). A real
	// ESC inside the first two frames (67 ms at 30 fps) of an epoch is dropped alike on every peer; its hold reaches frame 2.
	constexpr unsigned pause_code = 0x13;
	constexpr std::uint32_t pause_code_bit = 1u << pause_code;
	constexpr unsigned pause_guard_frames = 2;

	// AR6 Layer 1 (fable-ar6): the sender 0x559700 sends a FULL record (stock kind 0) instead of a delta while the local
	// block's next frame (+0x317c, reset to 0 at every ring reset: session start, zone load, cohort restart) is below
	// whole_input_frames. The single-lost-newcomer shape loses the first records to a ~1 s reset skew between the two
	// survivors (live-b2a/060zl/060zn: ~30 frames): the early resetter's first FULL records are dropped by the late one
	// (frame < next), and what it sends after are DELTAS the late peer decodes against no resolved previous record (ring
	// reset cleared +0x3148), building a garbage UI-code mask with bit 0x13 on an uninitialised stack record (live-b2a
	// O-2a: the host applied the guest index as 0x001af378 while the guest's own index was 0 - candidate A). Keeping the
	// first ~1 s of records FULL means the late peer always has the real mask, so it cannot decode garbage and both apply
	// the same inputs. A full record is the stock wire format, so a stock or unpatched peer decodes it unchanged.
	constexpr unsigned whole_input_frames = 32; // ~1 s at 30 fps, above every observed reset skew
	enum class send_form { stock, force_full };
	inline send_form restart_whole_input(unsigned next_frame) { return next_frame < whole_input_frames ? send_form::force_full : send_form::stock; }

	inline std::uint32_t applied_code_mask(std::uint32_t mask, unsigned frame)
	{
		return frame < pause_guard_frames ? (mask & ~pause_code_bit) : mask;
	}

	// AR7 (fable-ar7): the clock step a participant's head record contributes to a lockstep frame (0x559222..0x559268; the
	// frame's step is the max over the participants, 0x559282, set into the clock at 0x559546). The record's step word is
	// the producer's frame time in 0.1 ms units capped at 1.0 s (0x2710); bit 15 marks a placeholder record (produced while
	// the input manager is in placeholder mode, +0xc68b != 0). The stock rule is the SAME on every peer (b2c review, re-read
	// against the retail bytes): the sender writes the step word [record+2] verbatim for every wire kind (kind 0: 0x5597e9
	// mov cx,[ebp+2] -> 0x5597fe; deltas 0x559968, 0x559984, 0x5599a5, 0x5599cf, 0x5599f4, 0x559a1a, 0x559a3f, 0x559a5d) -
	// the `and eax, 0xffff7fff` at 0x5597f1 (and 0x55995f, 0x5599c6, 0x559a1e, 0x559c5b) strips bit 15 of the CODE MASK
	// dword [record+4], not of the step word; the decoder stores the word unchanged (0x559fd6, 0x55a1f1, 0x55a238); and
	// the run loop 0x559200..0x559268 applies one rule to every participant's head record with no own-participant test.
	// So a flagged record counts 0.025 s (0x3ccccccd) on EVERY peer in stock: the step is already symmetric. AR7 FINDINGS 1.4
	// ("owner-relative") was a misread of 0x5597f1; the X-2a drift it explained was the one-label offset AR8 found.
	// [Online] SymmetricClockStep (default OFF) only changes what a flagged record counts - its measured frame time, up to
	// 1.0 s (0.1 s per frame after the 0x46ef25 dt clamp), instead of 0.025 s - on every peer alike: no desync either way,
	// but with it ON game time runs ahead through a load stall (AI timers, spawns and Blob's limit advance faster). It is
	// kept as an opt-in experiment only; every peer of a session must still agree (a mixed session would be asymmetric).
	constexpr std::uint16_t step_placeholder_bit = 0x8000u, step_word_cap = 0x2710u;
	constexpr float step_placeholder_seconds = 0.025f; // 0x3ccccccd, what a flagged record counts in stock (on every peer)
	constexpr float step_unit_seconds = 1e-4f;         // 0x680518
	inline float step_seconds(std::uint16_t word) { return float(word & 0x7fffu) * step_unit_seconds; }
	inline float stock_clock_step(std::uint16_t word) { return (word & step_placeholder_bit) ? step_placeholder_seconds : step_seconds(word); }
	inline float symmetric_clock_step(std::uint16_t word) { return step_seconds(word); }
	// What the wire carries for a record: the step word verbatim (bit 15 included), the code mask with bit 15 stripped.
	inline std::uint16_t wire_step_word(std::uint16_t word) { return word; }
	inline std::uint32_t wire_code_mask(std::uint32_t mask) { return mask & 0xffff7fffu; }
	// Belt (FINDINGS 2.2, [Online] CanonicalClockStep, default OFF): while an online session is live, the frame's step is
	// 1/N (N = the session's agreed tick, the host's linked rate or the stock 30) whatever the records carry, so game time
	// is 2.0 + k/N on every peer by construction. A = 1/N is non-zero, so the stock set_one_shot_step(A) still runs.
	inline float canonical_clock_step(unsigned rate) { return rate ? 1.0f / float(rate) : 0.0f; }
	constexpr unsigned step_observe_frames = 41; // `jip step:` lines for frames 0..40 of every epoch (JoinDiagnostics=1)
	constexpr unsigned step_observe_lines = 2048;
	// The apply's mask argument must be a lockstep ring record's code-mask field (synchronizer 0x79fbb0: four blocks of 0x3184,
	// 300 slots of 0x28 at +0x268, record bytes at slot +4, frame u16 at record +0, code mask at record +4): the guard reads the
	// frame at mask - 4 only for such a pointer; any other caller of vt+0x24 (offline play, menus) is left alone.
	inline bool ring_record_mask(std::uintptr_t mask_at, std::uintptr_t synchronizer = 0x79fbb0u)
	{
		if (mask_at < synchronizer + 0x268u + 8u) return false;
		const auto in_block = (mask_at - synchronizer) % 0x3184u, block = (mask_at - synchronizer) / 0x3184u;
		if (block >= 4u || in_block < 0x268u + 8u) return false;
		const auto in_slots = in_block - 0x268u;
		return in_slots / 0x28u < 300u && in_slots % 0x28u == 8u;
	}

	// AR8 (fable-ar8): the input-stream epoch marker. A single lockstep record (kinds 1..5, 0x66..0x68, 0x6f) carries NO
	// frame label on the wire (0x5597db sends mask, axes, step and body only); the receiver 0x559eb0 labels it with the
	// block's received-record counter (+0x317e, 0x559ee7..0x559f01: label = counter++), which 0x558740 zeroes together with
	// the ring at every reset. A cohort restart resets each peer's rings at ITS OWN release (0x613f68 -> 0x559150, all four
	// blocks) and again at its start (0x6140a5 -> 0x559e00). Any record a peer sent between its start and its final reset is
	// wiped from the sender's own ring but, when the receiver parses it after the receiver's own reset (its parser was busy
	// loading), it is COUNTED: fable-ar7 X7-2a/X7B-2a/2a2 - the host's block for the guest held one such record at label 0,
	// so every later record of the guest sat one label late on the host (host label f = guest label f-1), the two PCs applied
	// the guest's inputs one frame apart and the cohort split at the guest's first stick input (frame 540/570). The marker:
	// right after each of its two reset sites the sender posts input_epoch_copies records whose code mask is
	// input_epoch_mark_prefix | nonce through the native lockstep-event path (0x55a370 -> 0x559700: produced into the own
	// ring, sent at once) and then wipes its own block again (0x558740), so the markers travel in the input stream, ahead of
	// its first real record, and occupy no label anywhere. The receiver's store hook (0x559560) recognises a marker, resets
	// that participant's block (stale records dropped, counter back to 0) and stores nothing; a repeated nonce (the extra
	// copies) only undoes the decoder's counter increment. Every real record after the marker is counted from 0 on every
	// receiver, exactly as the sender labelled it. The native event codes (0xfff31fff, 0xfff77fe0, 0xfff33fe0, 0xfff11fe0)
	// and real UI-code masks never carry the 0xfffa prefix. [Online] RestartInputEpoch (sender side); receivers always honour
	// a marker, so a peer with the key off still accepts one (it then stores no stray record either way).
	// The nonce is 15 bits: the sender strips bit 15 of the code mask for every wire kind (0x5597f1 `and eax, 0xffff7fff`,
	// 0x55995f, 0x5599c6, 0x559a1e), so a 16-bit nonce 0x8000 would arrive as 0 (not a marker: stored as a stray label 0)
	// and 0x8xxx as 0x0xxx; keeping bit 15 clear makes the marker identical in the sender's own ring and on every receiver.
	constexpr std::uint32_t input_epoch_mark_prefix = 0xfffa0000u;
	constexpr std::uint16_t input_epoch_nonce_mask = 0x7fffu;
	constexpr unsigned input_epoch_copies = 2;
	constexpr std::uint32_t native_event_codes[]{0xfff31fffu, 0xfff77fe0u, 0xfff33fe0u, 0xfff11fe0u};
	inline bool input_epoch_mark(std::uint32_t mask) { return (mask & 0xffff0000u) == input_epoch_mark_prefix && (mask & 0xffffu) != 0u; }
	inline std::uint32_t input_epoch_mark_for(std::uint16_t nonce) { const std::uint16_t n = std::uint16_t(nonce & input_epoch_nonce_mask); return input_epoch_mark_prefix | (n ? n : 1u); }
	inline std::uint16_t input_epoch_nonce(std::uint32_t mask) { return std::uint16_t(mask & input_epoch_nonce_mask); }
	// A fresh 15-bit nonce for this PC's next epoch: never 0 and never the previous one (a receiver ignores a repeat).
	inline std::uint16_t next_input_epoch_nonce(std::uint32_t seed, std::uint16_t previous)
	{
		std::uint16_t n = std::uint16_t((seed ^ (seed >> 16) ^ 0x5bd1u) & input_epoch_nonce_mask);
		if (!n || n == previous) n = std::uint16_t((previous + 0x1e37u) & input_epoch_nonce_mask);
		if (!n || n == previous) n = std::uint16_t(previous == 1u ? 2u : 1u);
		return n;
	}
	// late (b2c-fix-crash, live-b2c X-2c N-2c): a new nonce that reaches the block after this PC's simulation has already
	// consumed records from it (the ring's read index moved since the block's last reset). The marker says "count my
	// records from 0 from here", but the consumed record(s) were applied to frames this PC has already run: resetting the
	// block (read/write/next := 0) would re-label the sender's NEXT record as 0 and misalign every later frame (X-2c: the
	// guest's frame 0 ran a second before the host's starved pump flushed the start marker; `read 1 write 1 next 1`, reset,
	// native arbitration dropped the guest 0.13 s later). The marker is ignored instead: the block keeps its records and
	// labels, the nonce is remembered (its second copy is a repeat), the decoder's counter increment is given back - the
	// stock labelling for that epoch, never worse. The 300-slot ring's read index is 0 again after exactly 300 consumed
	// records (10 s at 30 frames); a marker that late has long been overtaken by the native timeouts.
	enum class input_epoch_action { store, reset, ignore, late };
	inline input_epoch_action input_epoch_receive(std::uint32_t mask, std::uint16_t last_nonce, std::uint32_t consumed = 0)
	{
		if (!input_epoch_mark(mask)) return input_epoch_action::store;
		if (input_epoch_nonce(mask) == last_nonce) return input_epoch_action::ignore;
		return consumed ? input_epoch_action::late : input_epoch_action::reset;
	}
	// AS (b2c-fix-crash, live-b2c X-4p N-CRASH): the 0x23 mesh-pair report 0x607390(adding, sender) clears the pair's bits
	// in both records' pending-mesh masks and, when the pair is the one the manager waits for (+0x2220), ends that wait
	// (+0x2220/+0x2238 := 0, settle timer +0x2228 := now + 1.0). Stock looks neither record up for null. With a record
	// missing (the seat was removed - p060fix/AR removes a newcomer lost before the restart at once; stock would have waited
	// for its 10 s watchdog) the stock function runs only when both exist; otherwise the fix applies the same two effects
	// to the record(s) that exist: nothing of the removed seat is left to clear, the surviving record's bit for it goes
	// (the manager's own 15 s pending-pair fallback 0x607dc0 would clear it the same way without dropping anyone), and the
	// manager's wait for exactly this pair ends as it would have a tick earlier. 2- and 3-player sessions never reach the
	// missing branch (every report in live-b2c arrived while the seat's record existed).
	enum class mesh_pair_report { stock, partial, none };
	inline mesh_pair_report mesh_pair_decide(bool a_present, bool b_present)
	{
		if (a_present && b_present) return mesh_pair_report::stock;
		return a_present || b_present ? mesh_pair_report::partial : mesh_pair_report::none;
	}
	inline std::uint32_t mesh_pair_mask(std::uint32_t a, std::uint32_t b) { return (1u << (a & 0x1fu)) | (1u << (b & 0x1fu)); } // 0x6073d7/0x6073e0: shl by cl
	// The handler's index of a payload half (0x604dcf..0x604de4 / 0x604deb..0x604e01): the first set bit of the low 4, else 4.
	inline std::uint32_t mesh_pair_index(std::uint32_t half)
	{
		for (std::uint32_t i = 0; i < 4; ++i) if (half & (1u << i)) return i;
		return 4;
	}
	// The store's kind argument (0x559560's third argument, the decoder 0x559eb0's `push`) says which fields of the decoder's
	// stack record (label +0, step +2, mask +4, axes +8/+0xa, body +0xc) the decoder wrote for this call; the others hold
	// whatever the previous decode at the same stack depth left there (the deltas are applied against the previous record
	// later, 0x558c00). Kinds with a written mask: 0 = a full record (0x559fb8, opcodes 0/1, every record of a batch
	// 0x55a219, and the caller 0x55a290's own record), 1 = mask only (0x55a1c5, opcodes 3/0x69), 3 = mask + axes01 (0x55a0a6,
	// opcodes 5/0x6a), 5 = mask + axes23 (0x55a152, opcodes 0x67/0x6c). Kinds 2 (axes01), 4 (axes23), 6 (both axes),
	// 8 (step only, opcodes 2/7) and 0x10 (body, 0x6f/0x70) never touch the mask slot: a marker mask left there by another
	// peer's previous message must not be read as this peer's marker (b2c review finding 2: it would wipe this peer's block).
	// A marker itself is always a full record (kind 0): it is posted right after a ring reset (write - read < 3 at 0x5597d2,
	// so the sender takes the full path 0x5597db), and AR6 keeps the first 32 frames full anyway.
	inline bool input_epoch_store_kind_has_mask(std::uint32_t kind) { return kind == 0u || kind == 1u || kind == 3u || kind == 5u; }
	inline input_epoch_action input_epoch_store_action(std::uint32_t kind, std::uint32_t mask, std::uint16_t last_nonce, std::uint32_t consumed = 0)
	{
		if (!input_epoch_store_kind_has_mask(kind)) return input_epoch_action::store;
		return input_epoch_receive(mask, last_nonce, consumed);
	}
	// The receiver's nonce memory (the last accepted nonce per participant index, 0 = none). It must not outlive the blocks
	// it describes: a stored nonce from an earlier session or an earlier occupant of the index would make a new peer's
	// marker with the same nonce (1 in 32767) a "repeat" (ignored: no reset, the one-label offset is back). Forgotten at
	// this PC's every all-blocks ring reset (the release 0x613f68 and the start 0x6140b3 - every cohort restart and every
	// session start pass one, and a new occupant of an index only arrives through a cohort restart) and at the session's end.
	struct input_epoch_memory
	{
		std::uint16_t last[4]{};
		void forget() { for (auto& n : last) n = 0; }
		bool empty() const { for (const auto n : last) if (n) return false; return true; }
	};
	// The decoder incremented the block's counter for a single record before the store sees it; an ignored repeat gives it back.
	inline std::uint16_t input_epoch_counter_after_ignore(std::uint16_t counter) { return counter ? std::uint16_t(counter - 1u) : counter; }
	// Pending (stored, unconsumed) records of a block from its read/write indices (300-slot ring).
	inline unsigned input_ring_pending(std::uint32_t read, std::uint32_t write) { return read < 300u && write < 300u ? (write >= read ? write - read : write + 300u - read) : 0u; }

	// b2d-fix-jip (live-b2d N-LATE4 / N-2c-LATE): the restart-release barrier. The marker is one unreliable single record
	// and the receiver labels single records by arrival, so what a peer stores from P between ITS OWN ring reset and P's
	// marker fixes P's labels for the whole epoch. A record P sent before its restart, still in flight across the ~1 s
	// reset skew (X-4p-a: m11 -> host; X-2cR, X-2cR2, X-2cL: host -> guest), was counted as P's label 0, the gate passed
	// on it and P's marker was then late (ignored) on THAT peer, while the peers whose parsers saw the marker first reset:
	// the same real record carried label 0 there and label 1 here and the cohort split at frame 30/60 (3 of 3
	// lost-newcomer restarts of live-b2d). The barrier makes every peer take the same decision: from this PC's own
	// all-blocks reset, P's records are HELD (copied aside with the decoder's label, not stored) until P's marker, which
	// drops them (pre-restart records: the block resets and P's real records are counted from 0 on every peer alike) -
	// or until a cap, after which the held records are stored in the order they came (the stock labelling, as before the
	// barrier; a marker after that is late, as before). A REAL record can precede P's marker only through the native
	// reliable resend (0x5c -> 0x5d batches, labelled on the wire: live-b2d P-LJ3, the newcomer's marker 16.6 s behind its
	// resent frame 0): dropped at a marker inside the cap it is requested again and resent by label; past the cap it is
	// stored as it came and the late marker is ignored, on every peer alike. Observed start-reset-to-marker skew: 1.0 to
	// 2.4 s in every live-b2d restart (passed or failed); the cap stays well below the native 10 s-class watchdogs.
	// Armed only while this PC itself sends markers ([Online] RestartInputEpoch, the shipped default), so the key stays a
	// complete A/B control; only indices occupied at this reset are expected, and this PC's own index (the own-record
	// store 0x55a2a6) is never held. An empty seat must not keep an expectation until a newcomer later fills it.
	constexpr unsigned input_epoch_hold_cap_ms = 5000;
	constexpr unsigned input_epoch_hold_records = 96; // per participant; the pre-restart window is a few records (96 = 3.2 s at 30 fps)
	constexpr unsigned input_epoch_record_bytes = 0x24; // the decoder's record as the store copies it (0x5595cd: 9 dwords)
	enum class input_epoch_hold { store, hold, flush };
	struct input_epoch_barrier
	{
		bool expecting[4]{};
		unsigned held[4]{};
		std::uint32_t armed_at = 0;
		void arm(int own, std::uint32_t occupied, std::uint32_t now) { for (unsigned i = 0; i < 4; ++i) { expecting[i] = (occupied & (1u << i)) && int(i) != own; held[i] = 0; } armed_at = now; }
		void disarm() { for (unsigned i = 0; i < 4; ++i) { expecting[i] = false; held[i] = 0; } }
		bool any() const { for (const auto e : expecting) if (e) return true; return false; }
		// A record that is not a marker, for `index`, at `now`: store (not expected), hold (copy it aside, skip the native
		// store) or flush (the cap passed or the hold is full: store the held records in order, then this one).
		input_epoch_hold record(std::uint32_t index, std::uint32_t now, unsigned cap_ms = input_epoch_hold_cap_ms, unsigned max_held = input_epoch_hold_records)
		{
			if (index > 3 || !expecting[index]) return input_epoch_hold::store;
			if (now - armed_at >= cap_ms || held[index] >= max_held) { expecting[index] = false; return input_epoch_hold::flush; }
			++held[index];
			return input_epoch_hold::hold;
		}
		// A new-nonce marker for `index` ends the expectation; the held records are dropped (returns how many).
		unsigned marker(std::uint32_t index) { if (index > 3 || !expecting[index]) return 0; expecting[index] = false; return take(index); }
		unsigned take(std::uint32_t index) { if (index > 3) return 0; const auto n = held[index]; held[index] = 0; return n; }
	};
}
