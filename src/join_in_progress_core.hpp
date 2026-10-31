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
	// A second newcomer admitted at the same zone load rides the campaign transfer the first one started (the native
	// sender images every participant and the 0x26 broadcast restarts the cohort once every image is in); only a
	// sender that is not this late restart's refuses the image.
	inline bool image_rides_running_transfer(bool sender_running, bool late_restart) { return sender_running && late_restart; }
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
}
