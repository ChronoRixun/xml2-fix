#pragma once

// Online restore (docs/online-restore.md, gameplay rule 63 OnlineRestore): the shared Load transaction's wire format
// (link kind 16 inside GNS framing 4), its validation, the host's and a guest's state machines and the re-pick
// decision. Pure: no Windows, GNS, file or game calls, no clocks (callers pass milliseconds), so
// test/online_restore_test.cpp drives every byte and transition. Nothing in here loads, freezes, seats or resumes
// anything: it decides, and the integration (rounds 11-14) acts on phase() / take() and reports native facts back.

#include "online_party_core.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace online_restore_core
{
	// ---- rule 63 OnlineRestore, revision 1: {on, wire, link kind, 0}. Registered by the first round that sends. ----
	constexpr std::uint16_t rule_id = 63, rule_revision = 1;
	constexpr std::uint8_t link_kind = 16, wire = 1;
	constexpr std::array<std::uint32_t, 4> rule_effective{1u, wire, link_kind, 0u};

	constexpr std::size_t header_size = 19, message_max = 256, members_max = 4, digest_size = 32, desc_max = 63,
		area_max = 31, roster_max = 32;
	constexpr std::uint32_t image_max = 0x200000, source_max = 16u << 20;
	constexpr std::uint16_t hero_open = 0xffff, hero_watch = 0xfffe, story_none = 0xffff;
	constexpr std::uint8_t slot_none = 0xff, purpose_load = 1;
	// Host clock budgets (ms). JIP's 45/90/120 s stage budgets are the reference; round 11 measures real loads.
	constexpr std::uint32_t vote_ms = 60000, prepare_ms = 30000, stall_ms = 30000, distribute_ms = 180000,
		load_ms = 120000, repick_ms = 90000, seat_ms = 30000, resume_ms = 30000, resend_ms = 1000,
		guest_silence_ms = 10000, progress_min_ms = 250, refusal_window_ms = 10000, reply_min_ms = 1000,
		// A bound guest (STAGED or later) that hears nothing from the host for this long ends itself (never resumes).
		// Longer than the native load budget: the host's integration must keep pumping, a load may be slow.
		bound_silence_ms = 150000, unconfirmed_resend_ms = 60000;
	constexpr unsigned refusal_limit = 16;

	using key = online_party_core::key;
	using digest = std::array<std::uint8_t, digest_size>;
	inline bool zero(const digest& d) { for (auto b : d) if (b) return false; return true; }
	inline bool zero(const key& k) { for (auto b : k) if (b) return false; return true; }

	enum class type : std::uint8_t { propose = 1, vote, queued, cancel, prepare, progress, staged, fail, commit, loaded, repick, pick, assign, seated,
		resume, freeze, rollback, end, tally, count };
	// Decisions come only from the host; replies only from a member of the open operation.
	inline bool decision(type t)
	{
		switch (t)
		{
		case type::propose: case type::queued: case type::cancel: case type::prepare: case type::commit: case type::repick: case type::assign:
		case type::resume: case type::freeze: case type::rollback: case type::end: case type::tally: return true;
		default: return false;
		}
	}
	enum class unsafe : std::uint8_t { none, combat, cutscene, conversation, joining, loading, travel, menu, count };
	enum class reason : std::uint8_t { none, host_cancel, declined, vote_timeout, member_left, roster_changed, guest_abort, stage_failed, digest_mismatch,
		membership, host_prepare_failed, prepare_timeout, transfer_stalled, transfer_timeout, protocol, load_failed, load_timeout, check_mismatch,
		rollback_failed, seat_timeout, resume_timeout, host_lost, host_silent, superseded, count };
	enum class phase : std::uint8_t { idle, voting, queued, preparing, distributing, staged, committed, loaded, repick, seating, seated, resuming, playing,
		cancelled, frozen, rolling_back, ended, count };
	inline const char* name(reason r)
	{
		static const char* const n[] = {"none", "host_cancel", "declined", "vote_timeout", "member_left", "roster_changed", "guest_abort", "stage_failed",
			"digest_mismatch", "membership", "host_prepare_failed", "prepare_timeout", "transfer_stalled", "transfer_timeout", "protocol", "load_failed",
			"load_timeout", "check_mismatch", "rollback_failed", "seat_timeout", "resume_timeout", "host_lost", "host_silent", "superseded"};
		return r < reason::count ? n[unsigned(r)] : "?";
	}
	inline const char* name(phase p)
	{
		static const char* const n[] = {"idle", "voting", "queued", "preparing", "distributing", "staged", "committed", "loaded", "repick", "seating",
			"seated", "resuming", "playing", "cancelled", "frozen", "rolling_back", "ended"};
		return p < phase::count ? n[unsigned(p)] : "?";
	}
	// Before the commit line nothing destructive happened anywhere.
	inline bool precommit(phase p) { return p == phase::voting || p == phase::queued || p == phase::preparing || p == phase::distributing || p == phase::staged; }
	inline bool terminal(phase p) { return p == phase::idle || p == phase::cancelled || p == phase::playing || p == phase::ended; }

	enum class refusal : std::uint8_t {
		none, size, kind, wire, type, code, session, op, purpose, count, key, index, length, digest, text, answer, unsafe, reason, slot, flag, bytes,
		phase, gen, check, hero, story, previous, sequence, duplicate,
		// model-level
		foreign, unauthorized, stale, out_of_phase, repeat, conflict, busy, membership, taken, closed, invalid_request, count_ };
	inline const char* name(refusal r)
	{
		static const char* const n[] = {"none", "size", "kind", "wire", "type", "code", "session", "op", "purpose", "count", "key", "index", "length",
			"digest", "text", "answer", "unsafe", "reason", "slot", "flag", "bytes", "phase", "gen", "check", "hero", "story", "previous", "sequence", "duplicate",
			"foreign", "unauthorized", "stale", "out_of_phase", "repeat", "conflict", "busy", "membership", "taken", "closed", "invalid_request"};
		return r < refusal::count_ ? n[unsigned(r)] : "?";
	}

	struct member
	{
		key k{};
		std::uint8_t index = 0; // native session index; the host is 0
		bool operator==(const member& o) const { return k == o.k && index == o.index; }
	};

	// One message of any type; only the fields of its type are meaningful.
	struct message
	{
		type t = type::propose;
		char code[8]{};
		std::uint32_t session = 0, op = 0;
		// propose
		std::uint8_t purpose = purpose_load;
		std::vector<member> members;
		std::uint32_t image_len = 0, source_len = 0, saved_seconds = 0;
		digest image{}, source{};
		std::string desc, area;
		// vote 1 yes / 2 no; tally yes mask
		std::uint8_t answer = 0, yes_mask = 0;
		unsafe why = unsafe::none;
		reason because = reason::none;
		std::uint8_t slot = slot_none;
		std::uint32_t frame = 0;    // prepare: freeze frame
		std::uint8_t flag = 0;      // prepare: rollback requested; assign: final
		std::uint32_t bytes = 0;    // progress
		digest rollback{};          // staged / rollback
		phase at = phase::idle;     // fail: the member's phase
		std::uint32_t gen = 0, check = 0, previous_op = 0;
		std::vector<std::uint16_t> heroes; // repick roster / assign per slot
		std::uint16_t story = story_none, hero = hero_open;
		std::uint16_t seq = 0; // pick: the member's own pick counter (newest wins on the unreliable lane)
		std::uint8_t story_owner = slot_none;
	};

	namespace detail
	{
		using online_party_core::detail::put16;
		using online_party_core::detail::put32;
		using online_party_core::detail::get16;
		using online_party_core::detail::get32;
		inline void put(std::vector<std::uint8_t>& o, const void* p, std::size_t n) { o.insert(o.end(), static_cast<const std::uint8_t*>(p), static_cast<const std::uint8_t*>(p) + n); }
		inline bool text_ok(const std::string& s, std::size_t max) { return s.size() <= max && online_party_core::valid_name(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()); }
		// Exact body size per fixed type (0 = variable).
		inline std::size_t fixed_body(type t)
		{
			switch (t)
			{
			case type::vote: case type::queued: case type::tally: return 1;
			case type::cancel: case type::fail: case type::end: return 2;
			case type::prepare: return 5;
			case type::progress: case type::commit: case type::seated: case type::resume: return 4;
			case type::staged: return 4 + 2 * digest_size;
			case type::loaded: return 8;
			case type::pick: return 8;
			case type::freeze: return 6;
			case type::rollback: return 8 + digest_size;
			default: return 0;
			}
		}
	}

	// Encoders write what they are given (a sender's own, already validated data); parse refuses anything outside the
	// format. encode(parse(x)) == x for every accepted x.
	inline std::vector<std::uint8_t> encode(const message& m)
	{
		using namespace detail;
		std::vector<std::uint8_t> o;
		o.reserve(64);
		o.push_back(link_kind);
		o.push_back(wire);
		o.push_back(std::uint8_t(m.t));
		put(o, m.code, 8);
		put32(o, m.session);
		put32(o, m.op);
		switch (m.t)
		{
		case type::propose:
			o.push_back(m.purpose);
			o.push_back(std::uint8_t(m.members.size()));
			for (const auto& r : m.members) { put(o, r.k.data(), r.k.size()); o.push_back(r.index); }
			put32(o, m.image_len); put(o, m.image.data(), digest_size);
			put32(o, m.source_len); put(o, m.source.data(), digest_size);
			put32(o, m.saved_seconds);
			o.push_back(std::uint8_t(m.desc.size())); put(o, m.desc.data(), m.desc.size());
			o.push_back(std::uint8_t(m.area.size())); put(o, m.area.data(), m.area.size());
			break;
		case type::vote: o.push_back(m.answer); break;
		case type::tally: o.push_back(m.yes_mask); break;
		case type::queued: o.push_back(std::uint8_t(m.why)); break;
		case type::cancel: case type::end: o.push_back(std::uint8_t(m.because)); o.push_back(m.slot); break;
		case type::prepare: put32(o, m.frame); o.push_back(m.flag); break;
		case type::progress: put32(o, m.bytes); break;
		case type::staged: put32(o, m.image_len); put(o, m.image.data(), digest_size); put(o, m.rollback.data(), digest_size); break;
		case type::fail: o.push_back(std::uint8_t(m.at)); o.push_back(std::uint8_t(m.because)); break;
		case type::commit: case type::seated: case type::resume: put32(o, m.gen); break;
		case type::loaded: put32(o, m.gen); put32(o, m.check); break;
		case type::repick:
			put32(o, m.gen);
			o.push_back(std::uint8_t(m.heroes.size()));
			for (auto h : m.heroes) put16(o, h);
			put16(o, m.story);
			o.push_back(m.story_owner);
			break;
		case type::pick: put32(o, m.gen); put16(o, m.seq); put16(o, m.hero); break;
		case type::assign:
			put32(o, m.gen);
			o.push_back(m.flag);
			o.push_back(std::uint8_t(m.heroes.size()));
			for (auto h : m.heroes) put16(o, h);
			break;
		case type::freeze: put32(o, m.gen); o.push_back(std::uint8_t(m.because)); o.push_back(m.slot); break;
		case type::rollback: put32(o, m.previous_op); put32(o, m.gen); put(o, m.rollback.data(), digest_size); break;
		default: break;
		}
		return o;
	}

	// Context-free structure only: sizes, enums, encodings, duplicates. Room/session/op/phase/sender: the machines.
	inline refusal parse(const std::uint8_t* d, std::size_t n, message& out)
	{
		using namespace detail;
		out = message{};
		if (!d || n < header_size || n > message_max) return refusal::size;
		if (d[0] != link_kind) return refusal::kind;
		if (d[1] != wire) return refusal::wire;
		if (d[2] == 0 || d[2] >= std::uint8_t(type::count)) return refusal::type;
		out.t = type(d[2]);
		std::memcpy(out.code, d + 3, 8);
		if (!online_party_core::valid_code(out.code)) return refusal::code;
		out.session = get32(d + 11);
		if (!out.session) return refusal::session;
		out.op = get32(d + 15);
		if (!out.op) return refusal::op;
		const std::uint8_t* b = d + header_size;
		const std::size_t body = n - header_size;
		if (const std::size_t fixed = fixed_body(out.t); fixed && body != fixed) return refusal::size;
		auto known_reason = [](std::uint8_t r) { return r > 0 && r < std::uint8_t(reason::count); };
		switch (out.t)
		{
		case type::propose:
		{
			std::size_t at = 0;
			if (body < 2) return refusal::size;
			out.purpose = b[at++];
			if (out.purpose != purpose_load) return refusal::purpose;
			const std::size_t count = b[at++];
			if (count < 1 || count > members_max) return refusal::count;
			if (body < at + count * 9 + 4 + digest_size + 4 + digest_size + 4 + 1) return refusal::size;
			for (std::size_t i = 0; i < count; ++i, at += 9)
			{
				member r;
				std::memcpy(r.k.data(), b + at, 8);
				r.index = b[at + 8];
				if (zero(r.k)) return refusal::key;
				if (r.index > 3 || (i == 0) != (r.index == 0)) return refusal::index;
				for (const auto& e : out.members)
				{
					if (e.k == r.k) return refusal::duplicate;
					if (e.index == r.index) return refusal::index;
				}
				out.members.push_back(r);
			}
			out.image_len = get32(b + at); at += 4;
			if (!out.image_len || out.image_len > image_max) return refusal::length;
			std::memcpy(out.image.data(), b + at, digest_size); at += digest_size;
			out.source_len = get32(b + at); at += 4;
			if (!out.source_len || out.source_len > source_max) return refusal::length;
			std::memcpy(out.source.data(), b + at, digest_size); at += digest_size;
			if (zero(out.image) || zero(out.source)) return refusal::digest;
			out.saved_seconds = get32(b + at); at += 4;
			const std::size_t desc = b[at++];
			if (desc > desc_max) return refusal::text;
			if (body < at + desc + 1) return refusal::size;
			out.desc.assign(reinterpret_cast<const char*>(b + at), desc); at += desc;
			const std::size_t area = b[at++];
			if (area > area_max) return refusal::text;
			if (body != at + area) return refusal::size;
			out.area.assign(reinterpret_cast<const char*>(b + at), area);
			if (!text_ok(out.desc, desc_max) || !text_ok(out.area, area_max)) return refusal::text;
			return refusal::none;
		}
		case type::vote:
			out.answer = b[0];
			return out.answer == 1 || out.answer == 2 ? refusal::none : refusal::answer;
		case type::tally:
			out.yes_mask = b[0];
			return (out.yes_mask & 1) && !(out.yes_mask & 0xf0) ? refusal::none : refusal::flag;
		case type::queued:
			if (b[0] == 0 || b[0] >= std::uint8_t(unsafe::count)) return refusal::unsafe;
			out.why = unsafe(b[0]);
			return refusal::none;
		case type::cancel: case type::end:
			if (!known_reason(b[0])) return refusal::reason;
			out.because = reason(b[0]);
			out.slot = b[1];
			return out.slot < members_max || out.slot == slot_none ? refusal::none : refusal::slot;
		case type::prepare:
			out.frame = get32(b);
			out.flag = b[4];
			return out.flag <= 1 ? refusal::none : refusal::flag;
		case type::progress:
			out.bytes = get32(b);
			return out.bytes <= image_max ? refusal::none : refusal::bytes;
		case type::staged:
			out.image_len = get32(b);
			std::memcpy(out.image.data(), b + 4, digest_size);
			std::memcpy(out.rollback.data(), b + 4 + digest_size, digest_size);
			if (!out.image_len || out.image_len > image_max) return refusal::length;
			return zero(out.image) ? refusal::digest : refusal::none;
		case type::fail:
			if (b[0] >= std::uint8_t(phase::count)) return refusal::phase;
			out.at = phase(b[0]);
			if (!known_reason(b[1])) return refusal::reason;
			out.because = reason(b[1]);
			return refusal::none;
		case type::commit: case type::seated: case type::resume:
			out.gen = get32(b);
			return out.gen ? refusal::none : refusal::gen;
		case type::loaded:
			out.gen = get32(b);
			out.check = get32(b + 4);
			if (!out.gen) return refusal::gen;
			return out.check ? refusal::none : refusal::check;
		case type::repick:
		{
			if (body < 5) return refusal::size;
			out.gen = get32(b);
			if (!out.gen) return refusal::gen;
			const std::size_t count = b[4];
			if (count < 1 || count > roster_max) return refusal::count;
			if (body != 5 + count * 2 + 3) return refusal::size;
			for (std::size_t i = 0; i < count; ++i)
			{
				const std::uint16_t h = get16(b + 5 + 2 * i);
				if (h >= hero_watch) return refusal::hero;
				if (std::find(out.heroes.begin(), out.heroes.end(), h) != out.heroes.end()) return refusal::duplicate;
				out.heroes.push_back(h);
			}
			out.story = get16(b + 5 + 2 * count);
			out.story_owner = b[5 + 2 * count + 2];
			if (out.story != story_none && std::find(out.heroes.begin(), out.heroes.end(), out.story) == out.heroes.end()) return refusal::story;
			if (out.story_owner != slot_none && (out.story_owner >= members_max || out.story == story_none)) return refusal::slot;
			return refusal::none;
		}
		case type::pick:
			out.gen = get32(b);
			out.seq = get16(b + 4);
			out.hero = get16(b + 6);
			if (!out.gen) return refusal::gen;
			if (!out.seq) return refusal::sequence;
			return out.hero == hero_open ? refusal::hero : refusal::none;
		case type::assign:
		{
			if (body < 6) return refusal::size;
			out.gen = get32(b);
			if (!out.gen) return refusal::gen;
			out.flag = b[4];
			if (out.flag > 1) return refusal::flag;
			const std::size_t count = b[5];
			if (count < 1 || count > members_max) return refusal::count;
			if (body != 6 + 2 * count) return refusal::size;
			for (std::size_t i = 0; i < count; ++i)
			{
				const std::uint16_t h = get16(b + 6 + 2 * i);
				if (h == hero_open && out.flag) return refusal::hero;
				if (h < hero_watch && std::find(out.heroes.begin(), out.heroes.end(), h) != out.heroes.end()) return refusal::duplicate;
				out.heroes.push_back(h);
			}
			return refusal::none;
		}
		case type::freeze:
			out.gen = get32(b);
			if (!out.gen) return refusal::gen;
			if (!known_reason(b[4])) return refusal::reason;
			out.because = reason(b[4]);
			out.slot = b[5];
			return out.slot < members_max || out.slot == slot_none ? refusal::none : refusal::slot;
		case type::rollback:
			out.previous_op = get32(b);
			out.gen = get32(b + 4);
			std::memcpy(out.rollback.data(), b + 8, digest_size);
			if (!out.previous_op || out.previous_op >= out.op) return refusal::previous;
			if (!out.gen) return refusal::gen;
			return zero(out.rollback) ? refusal::digest : refusal::none;
		default: return refusal::type;
		}
	}

	// What the host's integration validated and translated before proposing (round 11/14).
	struct proposal
	{
		std::uint32_t image_len = 0, source_len = 0, saved_seconds = 0;
		digest image{}, source{};
		std::string desc, area;
	};
	inline refusal check(const proposal& p)
	{
		if (!p.image_len || p.image_len > image_max || !p.source_len || p.source_len > source_max) return refusal::length;
		if (zero(p.image) || zero(p.source)) return refusal::digest;
		if (!detail::text_ok(p.desc, desc_max) || !detail::text_ok(p.area, area_max)) return refusal::text;
		return refusal::none;
	}
	// The loaded save's hero roster as the PC's own native load reports it (round 12).
	struct roster_info
	{
		std::vector<std::uint16_t> heroes;
		std::uint16_t story = story_none;
		key story_owner{}; // the saved owner's room identity when the LC metadata names one; zero = unknown
	};
	inline bool valid(const roster_info& r)
	{
		if (r.heroes.empty() || r.heroes.size() > roster_max) return false;
		for (std::size_t i = 0; i < r.heroes.size(); ++i)
		{
			if (r.heroes[i] >= hero_watch) return false;
			for (std::size_t j = 0; j < i; ++j) if (r.heroes[j] == r.heroes[i]) return false;
		}
		return r.story == story_none || std::find(r.heroes.begin(), r.heroes.end(), r.story) != r.heroes.end();
	}

	// ---- the re-pick decision (docs/online-restore.md section 7; owner answer 9.1) ----
	struct repick_state
	{
		std::vector<std::uint16_t> heroes;
		std::uint16_t story = story_none;
		std::uint8_t owner = slot_none, count = 0;
		std::array<std::uint16_t, members_max> assign{hero_open, hero_open, hero_open, hero_open};
		std::array<bool, members_max> present{};

		bool story_mode() const { return story != story_none; }
		bool in_roster(std::uint16_t h) const { return std::find(heroes.begin(), heroes.end(), h) != heroes.end(); }
		int holder(std::uint16_t h) const
		{
			for (std::uint8_t s = 0; s < count; ++s) if (present[s] && assign[s] == h) return s;
			return -1;
		}
		void start(std::uint8_t members, const std::array<bool, members_max>& here, const std::vector<std::uint16_t>& roster, std::uint16_t story_hero, std::uint8_t story_owner)
		{
			count = members;
			present = here;
			heroes = roster;
			story = story_hero;
			owner = story_owner != slot_none && story_owner < members && here[story_owner] ? story_owner : slot_none;
			for (std::uint8_t s = 0; s < members_max; ++s) assign[s] = s < count && present[s] ? hero_open : hero_watch;
			// A present saved owner takes the story hero; everyone else watches.
			if (story_mode() && owner != slot_none) assign[owner] = story;
		}
		refusal pick(std::uint8_t slot, std::uint16_t h)
		{
			if (slot >= count || !present[slot]) return refusal::unauthorized;
			if (h == hero_watch)
			{
				assign[slot] = hero_watch; // releases whatever the slot held (a saved owner may pass the story hero on)
				if (slot == owner) owner = slot_none;
				return refusal::none;
			}
			if (!in_roster(h) || (story_mode() && h != story)) return refusal::hero;
			const int held = holder(h);
			if (held >= 0 && held != slot) return refusal::taken;
			assign[slot] = h;
			return refusal::none;
		}
		// A member departing after everyone loaded the same world: its hero (and a story hero it owned) is free.
		void depart(std::uint8_t slot)
		{
			if (slot >= count) return;
			present[slot] = false;
			assign[slot] = hero_watch;
			if (slot == owner) owner = slot_none;
		}
		bool free_hero_left() const
		{
			for (auto h : heroes) if (holder(h) < 0) return true;
			return false;
		}
		bool complete() const
		{
			if (story_mode()) return holder(story) >= 0;
			bool open = false;
			for (std::uint8_t s = 0; s < count; ++s) if (present[s] && assign[s] == hero_open) open = true;
			return !open || !free_hero_left();
		}
		// The final assignment: deterministic fill (timeout, or completion with heroes exhausted). host = slot 0.
		void finish()
		{
			if (story_mode())
			{
				if (holder(story) < 0 && present[0]) assign[0] = story;
				for (std::uint8_t s = 0; s < count; ++s) if (assign[s] != story) assign[s] = hero_watch;
				return;
			}
			for (std::uint8_t s = 0; s < count; ++s)
			{
				if (!present[s] || assign[s] != hero_open) continue;
				assign[s] = hero_watch;
				for (auto h : heroes) if (holder(h) < 0) { assign[s] = h; break; }
			}
			// Somebody must play: if every member chose to watch, the host takes the first roster hero (review fix 7).
			bool played = false;
			for (std::uint8_t s = 0; s < count; ++s) if (present[s] && assign[s] < hero_watch) played = true;
			if (!played && present[0] && !heroes.empty()) assign[0] = heroes[0];
		}
		std::vector<std::uint16_t> row() const { return std::vector<std::uint16_t>(assign.begin(), assign.begin() + count); }
	};

	struct outgoing
	{
		std::uint8_t slot = slot_none; // slot_none: every other member of the operation
		std::vector<std::uint8_t> bytes;
	};

	namespace detail
	{
		struct refusal_gate
		{
			std::uint32_t times[refusal_limit]{};
			unsigned next = 0, total = 0;
			// True when this refusal is the sixteenth within the window.
			bool add(std::uint32_t now)
			{
				times[next] = now;
				next = (next + 1) % refusal_limit;
				if (++total < refusal_limit) return false;
				return now - times[next] < refusal_window_ms;
			}
		};
		inline bool same_members(const std::vector<member>& a, const std::vector<member>& b)
		{
			if (a.size() != b.size()) return false;
			for (const auto& m : a) if (std::find(b.begin(), b.end(), m) == b.end()) return false;
			return true;
		}
		inline bool same_keys(std::vector<key> a, std::vector<key> b)
		{
			std::sort(a.begin(), a.end());
			std::sort(b.begin(), b.end());
			return a == b;
		}
	}

	// ======================================= the host =======================================
	class host
	{
	public:
		host(const char* code, std::uint32_t session, const key& self) : session_(session), self_(self) { std::memcpy(code_, code, 8); }

		// ---- observations from the integration ----
		// The native active set (keys + native indices) and the room's pending PCs, whenever either changes.
		void roster(const std::vector<member>& active, const std::vector<key>& pending, std::uint32_t now)
		{
			active_ = active;
			pending_ = pending;
			if (terminal(phase_)) return;
			int missing = -1;
			for (std::uint8_t s = 0; s < members_.size(); ++s)
				if (!departed_[s] && std::find(active.begin(), active.end(), members_[s]) == active.end()) { missing = s; break; }
			bool newcomer = false;
			for (const auto& a : active)
				if (std::find(members_.begin(), members_.end(), a) == members_.end()) newcomer = true;
			switch (phase_)
			{
			case phase::voting: case phase::preparing: case phase::distributing:
				if (missing >= 0) cancel_op(reason::member_left, std::uint8_t(missing), now);
				else if (newcomer) cancel_op(reason::roster_changed, slot_none, now);
				break;
			case phase::queued:
				// Coordinator decision 3: only a change of ACTIVE members drops a queued Load; a PC arriving to wait
				// (or leaving the wait) does not - it stays held outside, as while voting.
				if (missing >= 0) cancel_op(reason::member_left, std::uint8_t(missing), now);
				else if (newcomer) cancel_op(reason::roster_changed, slot_none, now);
				break;
			case phase::committed:
				if (missing >= 0) freeze(reason::member_left, std::uint8_t(missing), now);
				else if (newcomer) freeze(reason::roster_changed, slot_none, now);
				break;
			case phase::rolling_back:
				if (missing >= 0) end(reason::member_left, std::uint8_t(missing), now);
				break;
			case phase::repick: case phase::seating: case phase::resuming:
				// Past the consistency line: an ordinary departure. Newcomers are held outside (admission_hold).
				for (std::uint8_t s = 0; s < members_.size(); ++s)
					if (!departed_[s] && std::find(active.begin(), active.end(), members_[s]) == active.end()) depart(s, now);
				break;
			default: break;
			}
		}
		// The host's safety observation for the current tick; freeze_frame is where every PC would pause.
		void safety(bool safe, unsafe why, std::uint32_t freeze_frame)
		{
			safe_ = safe;
			why_ = safe ? unsafe::none : (why == unsafe::none || why >= unsafe::count ? unsafe::menu : why);
			freeze_frame_ = freeze_frame;
		}

		// ---- host actions ----
		refusal propose(const proposal& p, std::uint32_t now)
		{
			if (phase_ == phase::ended) return refusal::closed;
			if (!terminal(phase_)) return refusal::busy;
			if (const auto r = check(p); r != refusal::none) return r;
			std::vector<member> m;
			for (const auto& a : active_) if (a.k == self_ && a.index == 0) m.push_back(a);
			if (m.size() != 1 || active_.size() > members_max) return refusal::membership;
			std::vector<member> rest;
			for (const auto& a : active_)
			{
				if (a.k == self_) continue;
				if (zero(a.k) || a.index == 0 || a.index > 3) return refusal::membership;
				for (const auto& e : rest) if (e.k == a.k || e.index == a.index) return refusal::membership;
				rest.push_back(a);
			}
			std::sort(rest.begin(), rest.end(), [](const member& a, const member& b) { return a.index < b.index; });
			m.insert(m.end(), rest.begin(), rest.end());
			open(++op_counter_, m, now);
			proposal_ = p;
			votes_[0] = 1;
			message msg = base(type::propose);
			msg.members = members_;
			msg.image_len = p.image_len; msg.image = p.image; msg.source_len = p.source_len; msg.source = p.source;
			msg.saved_seconds = p.saved_seconds; msg.desc = p.desc; msg.area = p.area;
			decisions_.clear();
			decide(msg);
			decide(tally());
			if (all_yes()) voted(now);
			return refusal::none;
		}
		refusal cancel(std::uint32_t now)
		{
			if (phase_ != phase::voting && phase_ != phase::queued && phase_ != phase::preparing && phase_ != phase::distributing) return refusal::out_of_phase;
			cancel_op(reason::host_cancel, 0, now);
			return refusal::none;
		}
		// The host staged its own image and (when supported) captured rollback at the freeze frame.
		void host_ready(bool ok, const digest& rollback, std::uint32_t now)
		{
			if (phase_ != phase::preparing) return;
			if (!ok) { cancel_op(reason::host_prepare_failed, 0, now); return; }
			own_rollback_ = rollback;
			staged_[0] = true;
			set_phase(phase::distributing, now);
			last_progress_ = now;
			maybe_commit(now);
		}
		// started_gen: the restore generation the native load was started under (generation() at COMMIT / ROLLBACK); a
		// late completion of an older load (e.g. the commit load finishing during a rollback) is ignored.
		void host_loaded(std::uint32_t started_gen, bool ok, std::uint32_t check_value, const roster_info& r, std::uint32_t now)
		{
			if (phase_ != phase::committed && phase_ != phase::rolling_back) return;
			if (started_gen != gen_) return;
			if (loaded_[0]) return;
			if (!ok || !check_value || (phase_ == phase::committed && !valid(r)))
			{
				if (phase_ == phase::committed) freeze(reason::load_failed, 0, now); else end(reason::rollback_failed, 0, now);
				return;
			}
			loaded_[0] = true;
			checks_[0] = check_value;
			loaded_roster_ = r;
			maybe_all_loaded(now);
		}
		refusal host_pick(std::uint16_t hero, std::uint32_t now) { return pick_for(0, hero, now); }
		void host_seated(std::uint32_t now)
		{
			if (phase_ != phase::seating || seated_[0]) return;
			seated_[0] = true;
			maybe_resume(now);
		}
		// The first fresh shared input frame after RESUME: Playing.
		void first_frame(std::uint32_t now)
		{
			// After a rollback the outcome keeps the failure that caused it (the party is back in the original world).
			if (phase_ == phase::resuming) finish_op(phase::playing, rollback_op_ ? outcome_ : reason::none, rollback_op_ ? outcome_slot_ : slot_none, now);
		}

		refusal receive(const key& from, const std::uint8_t* d, std::size_t n, std::uint32_t now)
		{
			message m;
			const int slot = slot_of(from);
			if (const auto r = parse(d, n, m); r != refusal::none) return refused(slot, r, now, d, n);
			if (std::memcmp(m.code, code_, 8) || m.session != session_) return refused(slot, refusal::foreign, now, d, n);
			if (decision(m.t) || slot < 0) return refused(slot, refusal::unauthorized, now, d, n);
			if (departed_[slot])
			{
				// It left past the consistency line: nothing it says counts; answer with its own END (it may have lost it).
				if (!departed_msg_[slot].empty() && now - last_reply_[slot] >= reply_min_ms)
				{
					last_reply_[slot] = now;
					out_.push_back({std::uint8_t(slot), departed_msg_[slot]});
				}
				return refusal::stale;
			}
			if (m.op != op_ || terminal(phase_))
			{
				// About the finished operation: answer with its outcome (rate-limited) - the member missed it. An END
				// also answers a member still on the operation a rollback replaced (op < op_).
				const bool about = m.op == op_ || (phase_ == phase::ended && m.op < op_);
				if (about && !outcome_msg_.empty() && now - last_reply_[slot] >= reply_min_ms)
				{
					last_reply_[slot] = now;
					out_.push_back({std::uint8_t(slot), outcome_msg_});
				}
				return refusal::stale;
			}
			auto& last = last_rx_[slot][unsigned(m.t)];
			const std::vector<std::uint8_t> bytes(d, d + n);
			if (last == bytes) return refusal::repeat;
			const auto r = handle(std::uint8_t(slot), m, now);
			if (r == refusal::none) last = bytes;
			else return refused(slot, r, now, d, n);
			return r;
		}

		void tick(std::uint32_t now)
		{
			const std::uint32_t in = now - since_;
			switch (phase_)
			{
			case phase::voting: if (in >= vote_ms) { cancel_op(reason::vote_timeout, slot_none, now); return; } break;
			case phase::queued: if (safe_) { prepare(now); return; } break;
			case phase::preparing: if (in >= prepare_ms) { cancel_op(reason::prepare_timeout, 0, now); return; } break;
			case phase::distributing:
				if (now - last_progress_ >= stall_ms) { cancel_op(reason::transfer_stalled, laggard(), now); return; }
				if (in >= distribute_ms) { cancel_op(reason::transfer_timeout, laggard(), now); return; }
				break;
			case phase::committed: if (in >= load_ms) { freeze(reason::load_timeout, unloaded(), now); return; } break;
			case phase::rolling_back: if (in >= load_ms) { end(reason::load_timeout, unloaded(), now); return; } break;
			case phase::repick: if (in >= repick_ms) { finalize(now); return; } break;
			case phase::seating:
				if (in >= seat_ms)
				{
					// Everyone holds the same world: members not seated in time depart (review fix 6); the host must seat.
					if (!seated_[0]) { end(reason::seat_timeout, 0, now); return; }
					for (std::uint8_t s = 1; s < members_.size(); ++s) if (!departed_[s] && !seated_[s]) depart(s, now, reason::seat_timeout);
					return;
				}
				break;
			case phase::resuming: if (in >= resume_ms) { end(reason::resume_timeout, slot_none, now); return; } break;
			default: return;
			}
			if (now - resent_ >= resend_ms)
			{
				resent_ = now;
				for (const auto& b : decisions_) broadcast(b);
			}
		}

		// ---- what the integration acts on ----
		phase now() const { return phase_; }
		reason outcome() const { return outcome_; }
		std::uint8_t outcome_slot() const { return outcome_slot_; }
		std::uint32_t op() const { return op_; }
		std::uint32_t generation() const { return gen_; }
		// From PROPOSE until the operation ends: no native admission / JIP bring-in starts; pending PCs wait outside.
		bool admission_hold() const { return !terminal(phase_); }
		bool rollback_available() const { return rollback_ok_; }
		bool rolled_back() const { return rollback_op_; }
		std::uint32_t freeze_frame() const { return frozen_at_; }
		const std::vector<member>& members() const { return members_; }
		const proposal& proposed() const { return proposal_; }
		std::uint8_t yes_mask() const { std::uint8_t m = 0; for (std::uint8_t s = 0; s < members_.size(); ++s) if (votes_[s] == 1) m |= std::uint8_t(1u << s); return m; }
		std::uint32_t vote_left_ms(std::uint32_t now) const { return phase_ == phase::voting && now - since_ < vote_ms ? vote_ms - (now - since_) : 0; }
		unsafe waiting_for() const { return phase_ == phase::queued ? why_ : unsafe::none; }
		const repick_state& picks() const { return repick_; }
		bool departed(std::uint8_t slot) const { return slot < members_max && departed_[slot]; }
		std::vector<outgoing> take() { std::vector<outgoing> o; o.swap(out_); return o; }

	private:
		char code_[8]{};
		std::uint32_t session_ = 0;
		key self_{};
		std::vector<member> active_, members_;
		std::vector<key> pending_;
		bool safe_ = false;
		unsafe why_ = unsafe::menu;
		std::uint32_t freeze_frame_ = 0, frozen_at_ = 0;
		std::uint32_t op_counter_ = 0, op_ = 0, gen_counter_ = 0, gen_ = 0;
		phase phase_ = phase::idle;
		reason outcome_ = reason::none;
		std::uint8_t outcome_slot_ = slot_none;
		proposal proposal_;
		std::array<std::uint8_t, members_max> votes_{};
		std::array<bool, members_max> staged_{}, loaded_{}, seated_{}, departed_{};
		std::array<digest, members_max> rollbacks_{};
		std::array<std::uint32_t, members_max> checks_{}, progress_{}, last_reply_{};
		std::array<std::array<std::vector<std::uint8_t>, unsigned(type::count)>, members_max> last_rx_{};
		std::array<detail::refusal_gate, members_max> gates_{};
		digest own_rollback_{};
		bool rollback_ok_ = false, rollback_op_ = false;
		roster_info loaded_roster_;
		repick_state repick_;
		std::uint32_t since_ = 0, resent_ = 0, last_progress_ = 0;
		std::vector<std::vector<std::uint8_t>> decisions_;
		std::vector<std::uint8_t> outcome_msg_, repick_msg_;
		std::array<std::uint16_t, members_max> pick_seq_{};
		std::array<std::vector<std::uint8_t>, members_max> departed_msg_{}, last_refused_{};
		std::vector<outgoing> out_;

		int slot_of(const key& k) const
		{
			for (std::size_t s = 0; s < members_.size(); ++s) if (s && members_[s].k == k) return int(s);
			return -1;
		}
		message base(type t) const
		{
			message m;
			m.t = t;
			std::memcpy(m.code, code_, 8);
			m.session = session_;
			m.op = op_;
			return m;
		}
		message tally() const { message m = base(type::tally); m.yes_mask = yes_mask(); return m; }
		void set_phase(phase p, std::uint32_t now) { phase_ = p; since_ = now; }
		void open(std::uint32_t op, const std::vector<member>& m, std::uint32_t now)
		{
			op_ = op;
			members_ = m;
			outcome_ = reason::none;
			outcome_slot_ = slot_none;
			outcome_msg_.clear();
			votes_ = {}; staged_ = {}; loaded_ = {}; seated_ = {}; departed_ = {};
			rollbacks_ = {}; checks_ = {}; progress_ = {}; last_reply_ = {}; last_rx_ = {}; departed_msg_ = {}; last_refused_ = {};
			for (auto& g : gates_) g = detail::refusal_gate{};
			own_rollback_ = {};
			rollback_ok_ = rollback_op_ = false;
			resent_ = now;
			set_phase(phase::voting, now);
		}
		// Send a decision to every other member and keep it for the periodic resend.
		void decide(const message& m, bool keep = true)
		{
			auto b = encode(m);
			if (keep) decisions_.push_back(b);
			broadcast(b);
		}
		void restate(const message& m) { decisions_.clear(); decide(m); }
		// One copy per member still in the operation: a departed member gets nothing but its own END (re-review Q2/Q3).
		void broadcast(const std::vector<std::uint8_t>& b)
		{
			for (std::uint8_t s = 1; s < members_.size(); ++s) if (!departed_[s]) out_.push_back({s, b});
		}
		bool all_yes() const { for (std::size_t s = 0; s < members_.size(); ++s) if (votes_[s] != 1) return false; return true; }
		bool every(const std::array<bool, members_max>& f) const
		{
			for (std::size_t s = 0; s < members_.size(); ++s) if (!departed_[s] && !f[s]) return false;
			return true;
		}
		std::uint8_t laggard() const
		{
			for (std::uint8_t s = 1; s < members_.size(); ++s) if (!staged_[s]) return s;
			return slot_none;
		}
		std::uint8_t unloaded() const
		{
			for (std::uint8_t s = 0; s < members_.size(); ++s) if (!departed_[s] && !loaded_[s]) return s;
			return slot_none;
		}
		refusal refused(int slot, refusal r, std::uint32_t now, const std::uint8_t* d, std::size_t n)
		{
			if (slot < 0 || r == refusal::repeat || r == refusal::stale) return r;
			// A member resends its last message once a second (and on every repeated decision): the same refused bytes
			// again are not a new refusal (re-review Q1). Only different refused messages fill the gate.
			std::vector<std::uint8_t> bytes(d, d + n);
			if (bytes == last_refused_[slot]) return r;
			last_refused_[slot] = std::move(bytes);
			if (gates_[slot].add(now) && precommit(phase_)) cancel_op(reason::protocol, std::uint8_t(slot), now);
			return r;
		}
		void finish_op(phase p, reason why, std::uint8_t slot, std::uint32_t now)
		{
			set_phase(p, now);
			outcome_ = why;
			outcome_slot_ = slot;
			decisions_.clear();
		}
		void cancel_op(reason why, std::uint8_t slot, std::uint32_t now)
		{
			message m = base(type::cancel);
			m.because = why;
			m.slot = slot;
			finish_op(phase::cancelled, why, slot, now);
			decide(m, false);
			outcome_msg_ = encode(m);
		}
		void end(reason why, std::uint8_t slot, std::uint32_t now)
		{
			message m = base(type::end);
			m.because = why;
			m.slot = slot;
			finish_op(phase::ended, why, slot, now);
			decide(m, false);
			outcome_msg_ = encode(m);
		}
		void voted(std::uint32_t now)
		{
			if (safe_) { prepare(now); return; }
			set_phase(phase::queued, now);
			message q = base(type::queued);
			q.why = why_;
			decide(q);
		}
		void prepare(std::uint32_t now)
		{
			set_phase(phase::preparing, now);
			frozen_at_ = freeze_frame_;
			message m = base(type::prepare);
			m.frame = freeze_frame_;
			m.flag = 1;
			restate(m);
		}
		void maybe_commit(std::uint32_t now)
		{
			if (phase_ != phase::distributing || !every(staged_)) return;
			rollback_ok_ = !zero(own_rollback_);
			for (std::size_t s = 1; s < members_.size(); ++s) if (rollbacks_[s] != own_rollback_) rollback_ok_ = false;
			gen_ = ++gen_counter_;
			set_phase(phase::committed, now);
			message m = base(type::commit);
			m.gen = gen_;
			restate(m);
		}
		void freeze(reason why, std::uint8_t slot, std::uint32_t now)
		{
			message f = base(type::freeze);
			f.gen = gen_;
			f.because = why;
			f.slot = slot;
			decide(f, false);
			if (slot != slot_none && slot != 0 && why == reason::member_left) departed_[slot] = true;
			if (!rollback_ok_) { end(why, slot, now); return; }
			// A new operation on the remaining members, no vote: each restores its own identical verified capture.
			const std::uint32_t previous = op_;
			op_ = ++op_counter_;
			gen_ = ++gen_counter_;
			loaded_ = {}; checks_ = {}; last_rx_ = {};
			rollback_op_ = true;
			outcome_ = why;
			outcome_slot_ = slot;
			set_phase(phase::rolling_back, now);
			message r = base(type::rollback);
			r.previous_op = previous;
			r.gen = gen_;
			r.rollback = own_rollback_;
			restate(r);
		}
		void maybe_all_loaded(std::uint32_t now)
		{
			if (!every(loaded_)) return;
			for (std::size_t s = 1; s < members_.size(); ++s)
				if (!departed_[s] && checks_[s] != checks_[0])
				{
					if (phase_ == phase::committed) freeze(reason::check_mismatch, std::uint8_t(s), now); else end(reason::check_mismatch, std::uint8_t(s), now);
					return;
				}
			if (phase_ == phase::rolling_back)
			{
				// The original seats come back with the original world: no re-pick.
				set_phase(phase::resuming, now);
				message m = base(type::resume);
				m.gen = gen_;
				restate(m);
				return;
			}
			set_phase(phase::repick, now);
			std::array<bool, members_max> here{};
			std::uint8_t owner = slot_none;
			for (std::uint8_t s = 0; s < members_.size(); ++s)
			{
				here[s] = !departed_[s];
				if (loaded_roster_.story != story_none && !zero(loaded_roster_.story_owner) && members_[s].k == loaded_roster_.story_owner) owner = s;
			}
			repick_.start(std::uint8_t(members_.size()), here, loaded_roster_.heroes, loaded_roster_.story, owner);
			message m = base(type::repick);
			m.gen = gen_;
			m.heroes = loaded_roster_.heroes;
			m.story = loaded_roster_.story;
			m.story_owner = repick_.owner;
			pick_seq_ = {};
			restate(m);
			repick_msg_ = decisions_.front();
			publish_picks(now);
		}
		void publish_picks(std::uint32_t now)
		{
			if (repick_.complete()) { finalize(now); return; }
			message a = base(type::assign);
			a.gen = gen_;
			a.flag = 0;
			a.heroes = repick_.row();
			if (decisions_.size() > 1) decisions_.pop_back();
			decide(a);
		}
		void finalize(std::uint32_t now)
		{
			repick_.finish();
			set_phase(phase::seating, now);
			message a = base(type::assign);
			a.gen = gen_;
			a.flag = 1;
			a.heroes = repick_.row();
			restate(a);
			// A member that missed REPICK refuses the final ASSIGN until it has REPICK: keep resending both (fix 3).
			decisions_.insert(decisions_.begin(), repick_msg_);
			for (std::size_t s = 0; s < members_.size(); ++s) if (departed_[s]) seated_[s] = true;
			maybe_resume(now);
		}
		void maybe_resume(std::uint32_t now)
		{
			if (phase_ != phase::seating || !every(seated_)) return;
			set_phase(phase::resuming, now);
			message m = base(type::resume);
			m.gen = gen_;
			restate(m);
		}
		void depart(std::uint8_t slot, std::uint32_t now, reason why = reason::member_left)
		{
			if (!slot || departed_[slot]) return;
			departed_[slot] = true;
			// Tell it (a member still connected must end its own play, never resume) - targeted, kept for its resends.
			message e = base(type::end);
			e.because = why;
			e.slot = slot;
			departed_msg_[slot] = encode(e);
			out_.push_back({slot, departed_msg_[slot]});
			repick_.depart(slot); // its hero is free: watch in the row from now on
			if (phase_ == phase::repick) publish_picks(now);
			else if (phase_ == phase::seating) maybe_resume(now);
		}
		refusal pick_for(std::uint8_t slot, std::uint16_t hero, std::uint32_t now)
		{
			if (phase_ != phase::repick) return refusal::out_of_phase;
			// A refused pick changes nothing and sends nothing (the member already has the latest ASSIGN, and the
			// periodic resend repeats it): answering a refusal would ping-pong with the member's reply resend.
			if (const auto r = repick_.pick(slot, hero); r != refusal::none) return r;
			publish_picks(now);
			return refusal::none;
		}
		refusal handle(std::uint8_t slot, const message& m, std::uint32_t now)
		{
			switch (m.t)
			{
			case type::vote:
				// A withdrawal sent while queued can arrive after the first safe tick: still precommit, so it cancels.
				if (m.answer == 2 && (phase_ == phase::voting || phase_ == phase::queued || phase_ == phase::preparing || phase_ == phase::distributing))
				{
					cancel_op(reason::declined, slot, now);
					return refusal::none;
				}
				if (phase_ != phase::voting && phase_ != phase::queued) return refusal::out_of_phase;
				if (phase_ == phase::queued) return refusal::conflict; // already Yes; only a withdrawal is new
				votes_[slot] = 1;
				decisions_.pop_back();
				decide(tally());
				if (all_yes()) voted(now);
				return refusal::none;
			case type::progress:
				if (phase_ != phase::distributing) return refusal::out_of_phase;
				if (m.bytes > proposal_.image_len) return refusal::bytes;
				if (m.bytes > progress_[slot]) { progress_[slot] = m.bytes; last_progress_ = now; }
				return refusal::none;
			case type::staged:
				// A receiver may finish before the host's own host_ready: record it (re-review Q1); COMMIT still waits
				// for distributing and every member.
				if (phase_ != phase::distributing && phase_ != phase::preparing) return refusal::out_of_phase;
				if (staged_[slot]) return refusal::conflict;
				if (m.image_len != proposal_.image_len || m.image != proposal_.image) { cancel_op(reason::digest_mismatch, slot, now); return refusal::none; }
				staged_[slot] = true;
				rollbacks_[slot] = m.rollback;
				last_progress_ = now;
				maybe_commit(now);
				return refusal::none;
			case type::fail:
			{
				const reason why = m.because == reason::none ? reason::guest_abort : m.because;
				if (precommit(phase_)) cancel_op(why, slot, now);
				else if (phase_ == phase::committed) freeze(why, slot, now);
				else if (phase_ == phase::repick || phase_ == phase::seating || phase_ == phase::resuming) depart(slot, now); // fix 6
				else end(why, slot, now);
				return refusal::none;
			}
			case type::loaded:
				if (phase_ != phase::committed && phase_ != phase::rolling_back) return refusal::out_of_phase;
				if (m.gen != gen_) return refusal::stale;
				if (loaded_[slot]) return refusal::conflict;
				loaded_[slot] = true;
				checks_[slot] = m.check;
				maybe_all_loaded(now);
				return refusal::none;
			case type::pick:
				if (m.gen != gen_) return refusal::stale;
				if (phase_ != phase::repick) return refusal::out_of_phase;
				if (m.seq <= pick_seq_[slot]) return refusal::stale; // reordered: an older choice never overrides a newer one
				pick_seq_[slot] = m.seq;
				return pick_for(slot, m.hero, now);
			case type::seated:
				if (phase_ != phase::seating) return refusal::out_of_phase;
				if (m.gen != gen_) return refusal::stale;
				seated_[slot] = true;
				maybe_resume(now);
				return refusal::none;
			default: return refusal::unauthorized;
			}
		}
	};

	// ======================================= a guest =======================================
	class guest
	{
	public:
		guest(const char* code, std::uint32_t session, const key& self, const key& host_key) : session_(session), self_(self), host_(host_key) { std::memcpy(code_, code, 8); }

		// This PC's own view of the native active set (keys + native indices).
		void roster(const std::vector<member>& active) { active_ = active; }

		refusal receive(const key& from, const std::uint8_t* d, std::size_t n, std::uint32_t now)
		{
			message m;
			if (const auto r = parse(d, n, m); r != refusal::none) return r;
			if (from != host_ || !decision(m.t)) return refusal::unauthorized;
			if (std::memcmp(m.code, code_, 8) || m.session != session_) return refusal::foreign;
			if (phase_ == phase::ended) return refusal::closed;
			last_host_ = now;
			const bool newer = m.op > op_;
			if (m.op < op_ || (newer && m.t != type::propose && m.t != type::rollback && m.t != type::end)) return refusal::stale;
			const std::vector<std::uint8_t> bytes(d, d + n);
			auto& last = last_rx_[unsigned(m.t)];
			if (!newer && last == bytes)
			{
				// The host repeats a decision: it may have missed this PC's answer.
				if (!last_sent_.empty()) out_.push_back({0, last_sent_});
				return refusal::repeat;
			}
			const auto r = handle(m, newer, now);
			if (r == refusal::none) last = bytes;
			return r;
		}

		// ---- this PC's player / integration ----
		refusal vote(bool yes, std::uint32_t now)
		{
			if (phase_ == phase::queued && !yes) return answer(2, now);
			if (phase_ != phase::voting) return refusal::out_of_phase;
			if (my_vote_ == 1 && yes) return refusal::repeat;
			return answer(yes ? 1 : 2, now);
		}
		// The player backs out before this PC staged (after STAGED it is bound; section 2).
		refusal abort(std::uint32_t now)
		{
			if (phase_ == phase::voting || phase_ == phase::queued) return answer(2, now);
			if (phase_ != phase::preparing) return refusal::closed;
			fail(reason::guest_abort, now);
			local_cancel(reason::guest_abort, now);
			return refusal::none;
		}
		void progress(std::uint32_t bytes, std::uint32_t now)
		{
			if (phase_ != phase::preparing || bytes <= sent_progress_ || bytes > proposal_.image_len || (sent_progress_ && now - progress_at_ < progress_min_ms)) return;
			sent_progress_ = bytes;
			progress_at_ = now;
			message m = base(type::progress);
			m.bytes = bytes;
			out_.push_back({0, encode(m)});
		}
		// Bytes complete, digest and structure validated, rollback captured (zero = none) - or the staging failed.
		void staged(bool ok, std::uint32_t len, const digest& image, const digest& rollback, std::uint32_t now)
		{
			if (phase_ != phase::preparing) return;
			if (!ok) { fail(reason::stage_failed, now); local_cancel(reason::stage_failed, now); return; }
			if (len != proposal_.image_len || image != proposal_.image) { fail(reason::digest_mismatch, now); local_cancel(reason::digest_mismatch, now); return; }
			message m = base(type::staged);
			m.image_len = len;
			m.image = image;
			m.rollback = rollback_requested_ ? rollback : digest{};
			my_rollback_ = m.rollback;
			reply(m);
			phase_ = phase::staged;
		}
		// started_gen: generation() when the native load was started (see host::host_loaded).
		void loaded(std::uint32_t started_gen, bool ok, std::uint32_t check_value, const roster_info& r, std::uint32_t now)
		{
			if (phase_ != phase::committed && phase_ != phase::rolling_back) return;
			if (started_gen != gen_) return;
			if (!ok || !check_value || (phase_ == phase::committed && !valid(r)))
			{
				fail(phase_ == phase::committed ? reason::load_failed : reason::rollback_failed, now);
				phase_ = phase::frozen;
				return;
			}
			roster_ = r;
			message m = base(type::loaded);
			m.gen = gen_;
			m.check = check_value;
			reply(m);
			phase_ = rolling_ ? phase::seated : phase::loaded; // a rollback skips the re-pick: next is RESUME
		}
		refusal pick(std::uint16_t hero, std::uint32_t now)
		{
			(void)now;
			if (phase_ != phase::repick) return refusal::out_of_phase;
			if (hero == hero_open || (hero != hero_watch && (std::find(picks_.heroes.begin(), picks_.heroes.end(), hero) == picks_.heroes.end() ||
				(picks_.story != story_none && hero != picks_.story)))) return refusal::hero;
			if (pick_seq_ == 0xffff) return refusal::closed; // capped: never wraps to the refused sequence 0
			message m = base(type::pick);
			m.gen = gen_;
			m.seq = ++pick_seq_;
			m.hero = hero;
			reply(m);
			return refusal::none;
		}
		void seated(std::uint32_t now)
		{
			(void)now;
			if (phase_ != phase::seating) return;
			message m = base(type::seated);
			m.gen = gen_;
			reply(m);
			phase_ = phase::seated;
		}
		void first_frame(std::uint32_t now) { (void)now; if (phase_ == phase::resuming) phase_ = phase::playing; }
		// Native host loss (0x612c40 -> 0x6123e0): the session is over whatever the phase.
		void host_lost(std::uint32_t now)
		{
			(void)now;
			if (phase_ == phase::ended) return;
			world_replaced_ = world_replaced_ || !(precommit(phase_) || phase_ == phase::idle || phase_ == phase::cancelled);
			phase_ = phase::ended;
			outcome_ = reason::host_lost;
			outcome_slot_ = 0;
		}
		void tick(std::uint32_t now)
		{
			// Silence before this PC is bound: back out (the host cancels when it hears, or the session ends).
			if ((phase_ == phase::voting || phase_ == phase::queued || phase_ == phase::preparing) && now - last_host_ >= guest_silence_ms)
			{
				fail(reason::host_silent, now);
				local_cancel(reason::host_silent, now);
			}
			// A bound PC never leaves on its own - except after a long total silence, when it ends (never resumes).
			if (bound(phase_) && now - last_host_ >= bound_silence_ms)
			{
				phase_ = phase::ended;
				outcome_ = reason::host_silent;
				outcome_slot_ = 0;
				return;
			}
			// Resend the last answer once a second while the operation is open (and for a while after a local cancel
			// the host has not confirmed): CANCEL / END / FREEZE are sent once, the host answers a resend with them.
			const bool open = !terminal(phase_) || (phase_ == phase::cancelled && unconfirmed_ && now - cancelled_at_ < unconfirmed_resend_ms);
			if (open && !last_sent_.empty() && now - resent_at_ >= resend_ms)
			{
				resent_at_ = now;
				out_.push_back({0, last_sent_});
			}
		}

		// ---- what the integration acts on ----
		phase now() const { return phase_; }
		reason outcome() const { return outcome_; }
		std::uint8_t outcome_slot() const { return outcome_slot_; }
		std::uint32_t op() const { return op_; }
		std::uint32_t generation() const { return gen_; }
		std::uint8_t slot() const { return self_slot_; }
		bool rollback_requested() const { return rollback_requested_; }
		std::uint32_t freeze_frame() const { return freeze_frame_; }
		// Input production for the shared frame is allowed only after RESUME (local load completion is not enough).
		bool may_resume_input() const { return phase_ == phase::resuming || phase_ == phase::playing; }
		bool world_replaced() const { return world_replaced_; }
		const std::vector<member>& members() const { return members_; }
		const proposal& proposed() const { return proposal_; }
		std::uint8_t yes_mask() const { return yes_mask_; }
		std::uint32_t vote_left_ms(std::uint32_t now) const { return phase_ == phase::voting && now - proposed_at_ < vote_ms ? vote_ms - (now - proposed_at_) : 0; }
		unsafe waiting_for() const { return phase_ == phase::queued ? why_ : unsafe::none; }
		const repick_state& picks() const { return picks_; }
		std::vector<outgoing> take() { std::vector<outgoing> o; o.swap(out_); return o; }

	private:
		char code_[8]{};
		std::uint32_t session_ = 0;
		key self_{}, host_{};
		std::vector<member> active_, members_;
		phase phase_ = phase::idle;
		reason outcome_ = reason::none;
		std::uint8_t outcome_slot_ = slot_none, self_slot_ = slot_none, my_vote_ = 0, yes_mask_ = 0;
		std::uint32_t op_ = 0, gen_ = 0, freeze_frame_ = 0, proposed_at_ = 0, last_host_ = 0, progress_at_ = 0, sent_progress_ = 0;
		bool rollback_requested_ = false, rolling_ = false, world_replaced_ = false;
		unsafe why_ = unsafe::none;
		proposal proposal_;
		digest my_rollback_{};
		roster_info roster_;
		repick_state picks_;
		std::array<std::vector<std::uint8_t>, unsigned(type::count)> last_rx_{};
		std::vector<std::uint8_t> last_sent_;
		std::vector<outgoing> out_;
		std::uint32_t resent_at_ = 0, cancelled_at_ = 0;
		std::uint16_t pick_seq_ = 0;
		bool unconfirmed_ = false;

		static bool bound(phase p)
		{
			return p == phase::staged || p == phase::committed || p == phase::loaded || p == phase::frozen || p == phase::rolling_back || p == phase::repick ||
				p == phase::seating || p == phase::seated || p == phase::resuming;
		}
		message base(type t) const
		{
			message m;
			m.t = t;
			std::memcpy(m.code, code_, 8);
			m.session = session_;
			m.op = op_;
			return m;
		}
		void reply(const message& m)
		{
			last_sent_ = encode(m);
			out_.push_back({0, last_sent_});
		}
		void fail(reason why, std::uint32_t now)
		{
			(void)now;
			message m = base(type::fail);
			m.at = phase_;
			m.because = why;
			reply(m);
		}
		void local_cancel(reason why, std::uint32_t now)
		{
			phase_ = phase::cancelled;
			outcome_ = why;
			outcome_slot_ = self_slot_;
			unconfirmed_ = true; // until the host's CANCEL / END arrives
			cancelled_at_ = now;
		}
		refusal answer(std::uint8_t a, std::uint32_t now)
		{
			(void)now;
			my_vote_ = a;
			message m = base(type::vote);
			m.answer = a;
			reply(m);
			if (a == 2) local_cancel(reason::declined, now);
			return refusal::none;
		}
		refusal accept_proposal(const message& m, std::uint32_t now)
		{
			// A higher op while an older one is open means the host finished the older one precommit (a lost
			// CANCEL), or this PC missed RESUME's echo while already resuming: both leave this PC's world consistent.
			if (!terminal(phase_) && !precommit(phase_) && phase_ != phase::resuming) return refusal::busy;
			if (phase_ == phase::resuming) phase_ = phase::playing;
			op_ = m.op;
			last_rx_ = {};
			members_ = m.members;
			proposal_.image_len = m.image_len; proposal_.image = m.image; proposal_.source_len = m.source_len; proposal_.source = m.source;
			proposal_.saved_seconds = m.saved_seconds; proposal_.desc = m.desc; proposal_.area = m.area;
			my_vote_ = 0;
			yes_mask_ = 1;
			rolling_ = false;
			rollback_requested_ = false;
			my_rollback_ = {};
			sent_progress_ = 0;
			progress_at_ = 0;
			unconfirmed_ = false;
			proposed_at_ = now;
			outcome_ = reason::none;
			outcome_slot_ = slot_none;
			last_sent_.clear();
			self_slot_ = slot_none;
			for (std::uint8_t s = 0; s < members_.size(); ++s) if (members_[s].k == self_) self_slot_ = s;
			phase_ = phase::voting;
			// Exact active membership: the host at slot 0 / index 0, this PC included, the same set this PC sees.
			if (members_[0].k != host_ || self_slot_ == slot_none || !detail::same_members(members_, active_))
			{
				fail(reason::membership, now);
				local_cancel(reason::membership, now);
			}
			return refusal::none;
		}
		refusal handle(const message& m, bool newer, std::uint32_t now)
		{
			switch (m.t)
			{
			case type::propose:
				if (!newer) return refusal::conflict;
				return accept_proposal(m, now);
			case type::tally:
				if (phase_ != phase::voting && phase_ != phase::queued) return refusal::out_of_phase;
				if (m.yes_mask >> members_.size()) return refusal::flag;
				yes_mask_ = m.yes_mask;
				return refusal::none;
			case type::queued:
				if (phase_ != phase::voting && phase_ != phase::queued) return refusal::out_of_phase;
				if (my_vote_ != 1) return refusal::out_of_phase; // queued means everyone said Yes
				phase_ = phase::queued;
				why_ = m.why;
				return refusal::none;
			case type::cancel:
				if (!precommit(phase_) && phase_ != phase::cancelled) return refusal::out_of_phase;
				phase_ = phase::cancelled;
				unconfirmed_ = false;
				outcome_ = m.because;
				outcome_slot_ = m.slot;
				return refusal::none;
			case type::prepare:
				if ((phase_ != phase::voting && phase_ != phase::queued) || my_vote_ != 1) return refusal::out_of_phase;
				phase_ = phase::preparing;
				freeze_frame_ = m.frame;
				rollback_requested_ = m.flag != 0;
				return refusal::none;
			case type::commit:
				if (phase_ != phase::staged) return refusal::out_of_phase;
				if (m.gen <= gen_) return refusal::stale;
				gen_ = m.gen;
				phase_ = phase::committed;
				world_replaced_ = true;
				return refusal::none;
			case type::freeze:
				// A PC that staged but missed COMMIT learns of the commit generation here (its own world is unchanged).
				if (phase_ == phase::staged && m.gen > gen_) gen_ = m.gen;
				else if (m.gen != gen_) return refusal::stale;
				else if (phase_ != phase::committed && phase_ != phase::loaded && phase_ != phase::frozen) return refusal::out_of_phase;
				phase_ = phase::frozen;
				outcome_ = m.because;
				outcome_slot_ = m.slot;
				return refusal::none;
			case type::rollback:
				// From frozen, or straight from staged / committed / loaded when FREEZE (sent once) was lost.
				if (!newer || m.previous_op != op_ || m.gen <= gen_ ||
					(phase_ != phase::frozen && phase_ != phase::staged && phase_ != phase::committed && phase_ != phase::loaded)) return refusal::out_of_phase;
				if (zero(my_rollback_) || my_rollback_ != m.rollback)
				{
					// Refuse under the rollback's own op so the host hears it at once (fix 5); stay frozen.
					op_ = m.op;
					gen_ = m.gen;
					last_rx_ = {};
					phase_ = phase::frozen;
					fail(reason::rollback_failed, now);
					return refusal::digest;
				}
				op_ = m.op;
				gen_ = m.gen;
				last_rx_ = {};
				rolling_ = true;
				phase_ = phase::rolling_back;
				return refusal::none;
			case type::repick:
				if (phase_ != phase::loaded) return refusal::out_of_phase;
				if (m.gen != gen_) return refusal::stale;
				if (m.heroes != roster_.heroes || m.story != roster_.story)
				{
					// This PC loaded a different roster: its world differs. It leaves (the host takes the FAIL as its
					// departure, fix 6); the others hold the same world and continue.
					fail(reason::check_mismatch, now);
					phase_ = phase::ended;
					outcome_ = reason::check_mismatch;
					outcome_slot_ = self_slot_;
					return refusal::check;
				}
				{
					std::array<bool, members_max> here{};
					for (std::size_t s = 0; s < members_.size(); ++s) here[s] = true;
					picks_.start(std::uint8_t(members_.size()), here, m.heroes, m.story, m.story_owner);
				}
				pick_seq_ = 0;
				phase_ = phase::repick;
				return refusal::none;
			case type::assign:
				if (m.gen != gen_) return refusal::stale;
				if (phase_ != phase::repick && !(phase_ == phase::seating && m.flag)) return refusal::out_of_phase;
				if (m.heroes.size() != members_.size()) return refusal::count;
				for (std::size_t s = 0; s < m.heroes.size(); ++s)
				{
					const auto h = m.heroes[s];
					if (h < hero_watch && !picks_.in_roster(h)) return refusal::hero;
					if (h < hero_watch && picks_.story_mode() && h != picks_.story) return refusal::hero;
				}
				for (std::size_t s = 0; s < m.heroes.size(); ++s) picks_.assign[s] = m.heroes[s];
				if (m.flag) phase_ = phase::seating;
				return refusal::none;
			case type::resume:
				if (m.gen != gen_) return refusal::stale;
				if (phase_ != phase::seated) return refusal::out_of_phase;
				phase_ = phase::resuming;
				return refusal::none;
			case type::end:
				world_replaced_ = world_replaced_ || !(precommit(phase_) || phase_ == phase::idle || phase_ == phase::cancelled);
				unconfirmed_ = false;
				phase_ = phase::ended;
				outcome_ = m.because;
				outcome_slot_ = m.slot;
				return refusal::none;
			default: return refusal::unauthorized;
			}
		}
	};
}
