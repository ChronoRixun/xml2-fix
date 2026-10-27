#pragma once
#include <array>
#include <cstdint>
#include <cstring>

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
