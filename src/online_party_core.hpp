#pragma once

// Online Party presentation data (docs/online-party.md): the member report (link kind 7) and the host table (kind 8)
// carried on authenticated room tunnels, their validation and rate limits, and the model that turns local
// observations plus accepted claims into the four lobby cards and the status roster. Pure: no Windows, GNS or game
// calls, so test/online_party_test.cpp drives every byte. Nothing in here can admit, ready, kick, start, seat or
// simulate: native readiness/admission stay native facts and are inputs only.

#include "online_room_core.hpp"
#include "online_status_core.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace online_party_core
{
	// ---- the framing (docs/online-party.md sections 1, 5, 6) ----
	constexpr std::uint8_t kind_report = 7, kind_table = 8, wire = 1;
	constexpr std::size_t key_size = 8, code_size = 8, token_max = 32, name_max = 31, rtt_rows_max = 3, members_max = 4;
	constexpr std::size_t rtt_row_size = 8 + 2 + 1, table_row_size = 8 + 1 + 1 + 1 + 2 + 1 + 1;
	constexpr std::size_t report_fixed = 1 + 1 + code_size + 4 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 2; // 23
	constexpr std::size_t report_max = 128, table_fixed = 1 + 1 + code_size + 4 + 2 + 1, table_max = 80;
	static_assert(report_fixed + token_max + name_max + rtt_rows_max * rtt_row_size <= report_max);
	static_assert(table_fixed + members_max * table_row_size <= table_max);
	constexpr std::uint16_t rtt_none = 0xffff, rtt_ceiling = 60000, hero_unknown = 0xffff;
	constexpr std::uint8_t index_unknown = 0xff, percent_none = 0xff;

	using key = std::array<std::uint8_t, key_size>;
	// "legends:<16 lowercase hex>" (online_room_core::valid_identity) <-> its 8 bytes.
	inline bool key_from_identity(const std::string& id, key& out)
	{
		if (!online_room_core::valid_identity(id)) return false;
		for (std::size_t i = 0; i < key_size; ++i)
		{
			auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
			out[i] = std::uint8_t(nibble(id[8 + 2 * i]) * 16 + nibble(id[9 + 2 * i]));
		}
		return true;
	}

	enum class phase : std::uint8_t { connecting, checking_build, in_lobby, ready, waiting_next_area, receiving_campaign, loading, playing, watching_story, left, count };
	enum class route : std::uint8_t { unknown, direct, relay, count };
	enum class build : std::uint8_t { checking, other, same, agreed, count };
	// The status file's names (docs/online-party.md section 9) and the card's words (section 8).
	inline const char* name(phase p)
	{
		static const char* const names[] = {"connecting", "checking_build", "in_lobby", "ready", "waiting_next_area", "receiving_campaign", "loading", "playing", "watching_story", "left"};
		return p < phase::count ? names[unsigned(p)] : "connecting";
	}
	inline const char* label(phase p)
	{
		static const char* const labels[] = {"Connecting", "Checking build", "In lobby", "Ready", "Waiting for next area", "Receiving campaign", "Loading", "Playing", "Watching story", "Left"};
		return p < phase::count ? labels[unsigned(p)] : "";
	}
	inline const char* name(route r) { return r == route::direct ? "direct" : r == route::relay ? "relay" : nullptr; }
	inline const char* name(build b)
	{
		static const char* const names[] = {"checking", "other", "same", "agreed"};
		return b < build::count ? names[unsigned(b)] : "checking";
	}

	inline bool code_char(char c)
	{
		static const char alphabet[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
		return c && std::strchr(alphabet, c) != nullptr;
	}
	inline bool valid_code(const char* code)
	{
		for (std::size_t i = 0; i < code_size; ++i) if (!code_char(code[i])) return false;
		return true;
	}
	inline bool token_byte(std::uint8_t c) { return c >= 0x21 && c <= 0x7e; }
	// A screen name: valid UTF-8 without control characters (online_status_core::valid_utf8_name; review 1607424 B1).
	inline bool valid_name(const std::uint8_t* p, std::size_t n) { return online_status_core::valid_utf8_name(std::string(reinterpret_cast<const char*>(p), n)); }

	struct rtt_row { key who{}; std::uint16_t ms = rtt_none; route how = route::unknown; };
	struct report
	{
		char code[code_size]{};
		std::uint32_t seq = 0;
		phase now = phase::connecting;
		std::uint8_t percent = percent_none, index = index_unknown;
		bool agreed = false; // flags bit 0: the sender's admission by the host's native/GCAP checks
		std::string token, name;
		std::vector<rtt_row> rtt;
	};
	struct table_row
	{
		key who{};
		std::uint8_t index = index_unknown;
		phase now = phase::connecting;
		std::uint8_t percent = percent_none;
		std::uint16_t ms = rtt_none; // to the host
		route how = route::unknown;
		build stage = build::checking;
	};
	struct table
	{
		char code[code_size]{};
		std::uint32_t seq = 0;
		std::uint16_t generation = 0;
		std::vector<table_row> rows;
	};

	enum class refusal
	{
		none, size, kind, wire, code, sequence, phase, percent, index, flags, token, name, rtt, hero, rows, duplicate,
		unknown_member, host_row, permission, room, rate, muted, count
	};
	inline const char* name(refusal r)
	{
		static const char* const names[] = {"none", "size", "kind", "wire", "code", "sequence", "phase", "percent", "index", "flags", "token", "name", "rtt",
		                                    "hero", "rows", "duplicate", "unknown member", "host row", "permission", "room", "rate", "muted"};
		return r < refusal::count ? names[unsigned(r)] : "?";
	}

	namespace detail
	{
		inline void put16(std::vector<std::uint8_t>& o, std::uint16_t v) { o.push_back(std::uint8_t(v)); o.push_back(std::uint8_t(v >> 8)); }
		inline void put32(std::vector<std::uint8_t>& o, std::uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(std::uint8_t(v >> (8 * i))); }
		inline std::uint16_t get16(const std::uint8_t* p) { return std::uint16_t(p[0] | (p[1] << 8)); }
		inline std::uint32_t get32(const std::uint8_t* p) { return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24); }
		inline bool percent_ok(phase p, std::uint8_t percent) { return p == phase::receiving_campaign ? percent <= 100 || percent == percent_none : percent == percent_none; }
	}

	// Encoders clip to the format's limits (a sender's own data); parse refuses anything outside them.
	inline std::vector<std::uint8_t> encode(const report& r)
	{
		std::vector<std::uint8_t> o;
		o.reserve(report_max);
		o.push_back(kind_report);
		o.push_back(wire);
		o.insert(o.end(), r.code, r.code + code_size);
		detail::put32(o, r.seq);
		o.push_back(std::uint8_t(r.now));
		o.push_back(r.percent);
		o.push_back(r.index);
		o.push_back(r.agreed ? 1 : 0);
		const auto token = r.token.substr(0, token_max);
		o.push_back(std::uint8_t(token.size()));
		o.insert(o.end(), token.begin(), token.end());
		const auto name = r.name.substr(0, name_max);
		o.push_back(std::uint8_t(name.size()));
		o.insert(o.end(), name.begin(), name.end());
		const auto rows = std::min(r.rtt.size(), rtt_rows_max);
		o.push_back(std::uint8_t(rows));
		for (std::size_t i = 0; i < rows; ++i)
		{
			o.insert(o.end(), r.rtt[i].who.begin(), r.rtt[i].who.end());
			detail::put16(o, r.rtt[i].ms);
			o.push_back(std::uint8_t(r.rtt[i].how));
		}
		detail::put16(o, hero_unknown);
		return o;
	}
	inline std::vector<std::uint8_t> encode(const table& t)
	{
		std::vector<std::uint8_t> o;
		o.reserve(table_max);
		o.push_back(kind_table);
		o.push_back(wire);
		o.insert(o.end(), t.code, t.code + code_size);
		detail::put32(o, t.seq);
		detail::put16(o, t.generation);
		const auto rows = std::min(t.rows.size(), members_max);
		o.push_back(std::uint8_t(rows));
		for (std::size_t i = 0; i < rows; ++i)
		{
			const auto& w = t.rows[i];
			o.insert(o.end(), w.who.begin(), w.who.end());
			o.push_back(w.index);
			o.push_back(std::uint8_t(w.now));
			o.push_back(w.percent);
			detail::put16(o, w.ms);
			o.push_back(std::uint8_t(w.how));
			o.push_back(std::uint8_t(w.stage));
		}
		return o;
	}

	// Context-free structure: sizes, enums, encodings, duplicates. Room, sequence, roster and sender checks: model.
	inline refusal parse(const std::uint8_t* d, std::size_t n, report& out)
	{
		out = {};
		if (!d || n < report_fixed || n > report_max) return refusal::size;
		if (d[0] != kind_report) return refusal::kind;
		if (d[1] != wire) return refusal::wire;
		std::memcpy(out.code, d + 2, code_size);
		if (!valid_code(out.code)) return refusal::code;
		out.seq = detail::get32(d + 10);
		if (!out.seq) return refusal::sequence;
		if (d[14] >= std::uint8_t(phase::count)) return refusal::phase;
		out.now = phase(d[14]);
		out.percent = d[15];
		if (!detail::percent_ok(out.now, out.percent)) return refusal::percent;
		out.index = d[16];
		if (out.index > 3 && out.index != index_unknown) return refusal::index;
		if (d[17] & ~1u) return refusal::flags;
		out.agreed = (d[17] & 1) != 0;
		std::size_t at = 18;
		const std::size_t token = d[at++];
		if (token > token_max || at + token + 1 > n) return token > token_max ? refusal::token : refusal::size;
		for (std::size_t i = 0; i < token; ++i) if (!token_byte(d[at + i])) return refusal::token;
		out.token.assign(reinterpret_cast<const char*>(d + at), token);
		at += token;
		const std::size_t name = d[at++];
		if (name > name_max || at + name + 1 > n) return name > name_max ? refusal::name : refusal::size;
		if (!valid_name(d + at, name)) return refusal::name;
		out.name.assign(reinterpret_cast<const char*>(d + at), name);
		at += name;
		const std::size_t rows = d[at++];
		if (rows > rtt_rows_max) return refusal::rtt;
		if (at + rows * rtt_row_size + 2 != n) return refusal::size;
		for (std::size_t i = 0; i < rows; ++i, at += rtt_row_size)
		{
			rtt_row r;
			std::memcpy(r.who.data(), d + at, key_size);
			r.ms = detail::get16(d + at + key_size);
			if (r.ms > rtt_ceiling && r.ms != rtt_none) return refusal::rtt;
			if (d[at + key_size + 2] >= std::uint8_t(route::count)) return refusal::rtt;
			r.how = route(d[at + key_size + 2]);
			for (const auto& e : out.rtt) if (e.who == r.who) return refusal::duplicate;
			out.rtt.push_back(r);
		}
		if (detail::get16(d + at) != hero_unknown) return refusal::hero;
		return refusal::none;
	}
	inline refusal parse(const std::uint8_t* d, std::size_t n, table& out)
	{
		out = {};
		if (!d || n < table_fixed + table_row_size || n > table_max) return refusal::size;
		if (d[0] != kind_table) return refusal::kind;
		if (d[1] != wire) return refusal::wire;
		std::memcpy(out.code, d + 2, code_size);
		if (!valid_code(out.code)) return refusal::code;
		out.seq = detail::get32(d + 10);
		if (!out.seq) return refusal::sequence;
		out.generation = detail::get16(d + 14);
		const std::size_t rows = d[16];
		if (!rows || rows > members_max) return refusal::rows;
		if (n != table_fixed + rows * table_row_size) return refusal::size;
		for (std::size_t i = 0; i < rows; ++i)
		{
			const auto* p = d + table_fixed + i * table_row_size;
			table_row w;
			std::memcpy(w.who.data(), p, key_size);
			w.index = p[8];
			if (w.index > 3 && w.index != index_unknown) return refusal::index;
			if (p[9] >= std::uint8_t(phase::count)) return refusal::phase;
			w.now = phase(p[9]);
			w.percent = p[10];
			if (!detail::percent_ok(w.now, w.percent)) return refusal::percent;
			w.ms = detail::get16(p + 11);
			if (w.ms > rtt_ceiling && w.ms != rtt_none) return refusal::rtt;
			if (p[13] >= std::uint8_t(route::count)) return refusal::rtt;
			w.how = route(p[13]);
			if (p[14] >= std::uint8_t(build::count)) return refusal::rows;
			w.stage = build(p[14]);
			for (const auto& e : out.rows)
				if (e.who == w.who || (w.index != index_unknown && e.index == w.index)) return refusal::duplicate;
			out.rows.push_back(w);
		}
		return refusal::none;
	}

	// ---- the sender's own phase, from facts only (section 5) ----
	struct phase_inputs
	{
		bool left = false, hosting = false, host_connected = false; // room-level
		bool participant = false;  // this PC is a native session participant (lobby or game)
		bool native_ready = false; // its native ready flag (0x614240), a guest's only
		bool jip_pending = false;  // admitted but outside the lockstep until the next area (rule 26)
		bool loading = false;      // a zone load runs
		bool inputs_flowing = false; // input_flow::flowing: real shared input, not `in_game`
	};
	inline phase local_phase(const phase_inputs& f)
	{
		if (f.left) return phase::left;
		if (f.participant)
		{
			if (f.jip_pending) return f.loading ? phase::loading : phase::waiting_next_area;
			if (f.loading) return phase::loading;
			if (f.inputs_flowing) return phase::playing;
			return f.native_ready && !f.hosting ? phase::ready : phase::in_lobby;
		}
		if (f.hosting) return phase::connecting; // the room is open, the native host session not set up yet
		return f.host_connected ? phase::checking_build : phase::connecting;
	}

	// Playing needs every participant's lockstep input ring to have advanced recently: a frame counter that moved on its
	// own (`in_game` includes the lobby) is not enough. Samples are the ring's next expected frame per session index.
	class input_flow
	{
		std::uint16_t next_[members_max]{};
		std::uint32_t moved_[members_max]{};
		bool seen_[members_max]{}, moved_once_[members_max]{};
	public:
		static constexpr std::uint32_t window_ms = 1000;
		void reset() { *this = {}; }
		void forget(int index) { if (index >= 0 && index < int(members_max)) { seen_[index] = moved_once_[index] = false; } }
		void sample(int index, bool ok, std::uint16_t next, std::uint32_t now)
		{
			if (index < 0 || index >= int(members_max)) return;
			if (!ok) { forget(index); return; }
			if (seen_[index] && next != next_[index]) { moved_[index] = now; moved_once_[index] = true; }
			seen_[index] = true;
			next_[index] = next;
		}
		bool flowing(const int* indices, std::size_t count, std::uint32_t now) const
		{
			if (!count) return false;
			for (std::size_t i = 0; i < count; ++i)
			{
				const int x = indices[i];
				if (x < 0 || x >= int(members_max) || !moved_once_[x] || now - moved_[x] > window_ms) return false;
			}
			return true;
		}
	};

	// ---- per-tunnel receive policy (section 7) ----
	struct receive_gate
	{
		online_room_core::token_bucket reports{2, 4}, tables{4, 6}, bytes{1024, 2048};
		std::uint32_t last_report = 0, last_table = 0;
		unsigned refused = 0;
		std::uint32_t window_at = 0;
		bool muted = false, window_open = false;
		static constexpr unsigned mute_after = 16;
		static constexpr std::uint32_t window_ms = 10000;
		// Rate and byte budget before parsing; a muted tunnel's party messages are ignored for the connection's life.
		refusal admit(std::uint8_t kind, std::size_t size, std::uint32_t now)
		{
			if (muted) return refusal::muted;
			auto& bucket = kind == kind_table ? tables : reports;
			if (!bucket.allow(now) || !bytes.allow(now, size)) return refuse(refusal::rate, now);
			return refusal::none;
		}
		// 16 refusals within 10 s mute the tunnel; an older count starts over (review N1). An out-of-order sequence is
		// expected on an unreliable lane and never counts.
		refusal refuse(refusal why, std::uint32_t now)
		{
			if (why == refusal::none || why == refusal::muted || why == refusal::sequence) return why;
			if (!window_open || now - window_at > window_ms) { window_open = true; window_at = now; refused = 0; }
			if (++refused >= mute_after) muted = true;
			return why;
		}
	};

	// ---- the send side: content changes at most every min_ms, plus a refresh every refresh_ms ----
	class pacer
	{
		std::vector<std::uint8_t> last_;
		std::uint32_t at_ = 0;
		bool sent_ = false;
		std::uint32_t min_ms_, refresh_ms_;
	public:
		pacer(std::uint32_t min_ms, std::uint32_t refresh_ms) : min_ms_(min_ms), refresh_ms_(refresh_ms) {}
		// `content` is the message with its sequence zeroed.
		bool due(const std::vector<std::uint8_t>& content, std::uint32_t now) const
		{
			if (!sent_) return true;
			const auto since = now - at_;
			if (since < min_ms_) return false;
			return content != last_ || since >= refresh_ms_;
		}
		void sent(const std::vector<std::uint8_t>& content, std::uint32_t now) { last_ = content; at_ = now; sent_ = true; }
		void reset() { last_.clear(); sent_ = false; at_ = 0; }
	};
	constexpr std::uint32_t report_min_ms = 1000, table_min_ms = 500, refresh_ms = 5000, claim_stale_ms = 15000;
	inline std::vector<std::uint8_t> without_sequence(std::vector<std::uint8_t> m)
	{
		if (m.size() >= 14) std::memset(m.data() + 10, 0, 4);
		return m;
	}

	// ---- the lobby's card split (online_room::party): facts for native rows, and broker-only pending cards ----
	struct card;
	struct lobby_split_result { std::vector<const card*> rows, pending; };
	inline lobby_split_result lobby_split(const std::vector<card>& cards, std::size_t native_count, std::size_t roster_size,
	                                     const std::vector<int>* native_indices = nullptr);

	// ---- the model ----
	// What this PC observes itself about one other broker-roster member (game thread, every tick).
	struct member_facts
	{
		bool tunnel = false;  // the P2P connection is up
		bool bound = false;   // the GNS transport is bound on it (admitted)
		int index = -1;       // its native session index by this PC's own mapping (-1 none)
		int rtt = -1;         // this PC's measured RTT in ms (-1 unknown)
		route how = route::unknown;
		bool ready = false, ready_known = false; // its native ready flag as this PC's session holds it
		std::string native_name; // its native participant name (admitted only)
	};
	enum class rtt_source { none, local, to_host };
	struct card
	{
		key who{};
		bool self = false, host = false, admitted = false;
		int index = -1;
		phase now = phase::connecting;
		bool phase_known = false;
		std::uint8_t percent = percent_none;
		int rtt = -1;
		rtt_source from = rtt_source::none;
		route how = route::unknown;
		build stage = build::checking;
		bool ready = false, ready_known = false;
		std::string name; // native name when admitted, else the reported one (raw bytes; callers sanitize for display)
	};

	class model
	{
		struct member
		{
			key who{};
			member_facts facts;
			bool has_report = false;
			report last;
			std::uint32_t report_at = 0;
			receive_gate gate;
		};
		char code_[code_size]{};
		bool open_ = false;
		key self_{}, host_{};
		std::vector<member> members_; // the active broker roster minus this PC, in roster order
		bool has_table_ = false;
		table table_;
		std::uint32_t table_at_ = 0;
		std::uint32_t generation_ = 0;
		std::uint64_t last_digest_ = 0;
		unsigned refused_[unsigned(refusal::count)]{};
		member* find(const key& k) { for (auto& m : members_) if (m.who == k) return &m; return nullptr; }
		const member* find(const key& k) const { for (auto& m : members_) if (m.who == k) return &m; return nullptr; }
		refusal count(refusal r) { if (r != refusal::none && r < refusal::count) ++refused_[unsigned(r)]; return r; }
	public:
		// A new room (or none): every claim, gate and sequence is forgotten.
		void open(const char* code, const key& self, const key& host)
		{
			members_.clear();
			has_table_ = false;
			table_ = {};
			std::memcpy(code_, code, code_size);
			open_ = valid_code(code_);
			self_ = self;
			host_ = host;
			++generation_;
		}
		void close() { members_.clear(); has_table_ = false; table_ = {}; open_ = false; ++generation_; }
		bool is_open() const { return open_; }
		const key& self() const { return self_; }
		const key& host() const { return host_; }
		const char* code() const { return code_; }
		bool hosting() const { return open_ && self_ == host_; }
		std::uint32_t generation() const { return generation_; }
		unsigned refused(refusal r) const { return r < refusal::count ? refused_[unsigned(r)] : 0; }
		bool has_table() const { return has_table_; }
		std::uint16_t table_generation() const { return table_.generation; }

		// The broker's active roster (self included or not, at most four). Members that left lose all state; a key that
		// returns later starts again with no sequence memory (its tunnel is new too).
		void roster(const std::vector<key>& active)
		{
			std::vector<member> next;
			for (const auto& k : active)
			{
				if (k == self_ || next.size() >= members_max - 1) continue;
				bool dup = false;
				for (const auto& m : next) dup = dup || m.who == k;
				if (dup) continue;
				if (auto* old = find(k)) next.push_back(std::move(*old));
				else { member m; m.who = k; next.push_back(std::move(m)); }
			}
			members_ = std::move(next);
			if (has_table_)
			{
				// Rows of members no longer in the roster are presentation garbage now.
				auto& rows = table_.rows;
				rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const table_row& w) { return w.who != self_ && w.who != host_ && !find(w.who); }), rows.end());
			}
		}
		// This PC's observation of one member; an index another member already holds locally is refused for both.
		void observe(const key& k, const member_facts& f)
		{
			if (auto* m = find(k)) m->facts = f;
		}
		// After all observe() calls of a tick: two members may never share one local index (a stale mapping).
		void settle_indices()
		{
			for (auto& a : members_)
				for (auto& b : members_)
					if (&a != &b && a.facts.index >= 0 && a.facts.index == b.facts.index) { a.facts.index = -1; b.facts.index = -1; }
		}
		const member_facts* facts(const key& k) const { const auto* m = find(k); return m ? &m->facts : nullptr; }
		// The newest report sequence accepted from this member on its current connection (0: none yet). Rule 64's removal
		// notice echoes it, so the removed PC can tell a notice of this connection from an earlier one (online_removal_core).
		std::uint32_t heard(const key& k) const { const auto* m = find(k); return m && m->has_report ? m->gate.last_report : 0; }
		// A report from this member was accepted on its current connection within `ms` (rule 64: the removed member is
		// responsive, not stalled in a zone load).
		bool heard_within(const key& k, std::uint32_t now, std::uint32_t ms) const { const auto* m = find(k); return m && m->has_report && now - m->report_at < ms; }
		// A new connection to this member (its process may have restarted, its sequences start again at 1): its
		// sequence memory, rate state and claims are dropped. Messages of the old connection cannot arrive on the new one.
		void reconnected(const key& k)
		{
			if (auto* m = find(k)) { m->gate = {}; m->has_report = false; m->last = {}; }
		}

		refusal on_report(const key& from, const std::uint8_t* data, std::size_t size, std::uint32_t now)
		{
			auto* m = find(from);
			if (!open_ || !m) return count(refusal::unknown_member);
			if (const auto r = m->gate.admit(kind_report, size, now); r != refusal::none) return count(r);
			report r;
			if (const auto why = parse(data, size, r); why != refusal::none) return count(m->gate.refuse(why, now));
			if (std::memcmp(r.code, code_, code_size) != 0) return count(m->gate.refuse(refusal::room, now));
			if (r.seq <= m->gate.last_report) return count(m->gate.refuse(refusal::sequence, now));
			for (const auto& row : r.rtt)
				if (row.who == from) return count(m->gate.refuse(refusal::unknown_member, now));
			r.rtt.erase(std::remove_if(r.rtt.begin(), r.rtt.end(), [&](const rtt_row& row) { return row.who != self_ && !find(row.who); }), r.rtt.end());
			m->gate.last_report = r.seq;
			m->last = std::move(r);
			m->has_report = true;
			m->report_at = now;
			return refusal::none;
		}
		// `from_index`: the sender's native index by this PC's own mapping (-1: no native session maps it yet).
		refusal on_table(const key& from, int from_index, const std::uint8_t* data, std::size_t size, std::uint32_t now)
		{
			auto* m = find(from);
			if (!open_ || !m) return count(refusal::unknown_member);
			if (const auto r = m->gate.admit(kind_table, size, now); r != refusal::none) return count(r);
			// Only the broker's host for this room, and only while no native session says it is anyone but index 0.
			if (from != host_ || (from_index >= 0 && from_index != 0)) return count(m->gate.refuse(refusal::permission, now));
			table t;
			if (const auto why = parse(data, size, t); why != refusal::none) return count(m->gate.refuse(why, now));
			if (std::memcmp(t.code, code_, code_size) != 0) return count(m->gate.refuse(refusal::room, now));
			if (t.seq <= m->gate.last_table) return count(m->gate.refuse(refusal::sequence, now));
			unsigned host_rows = 0;
			for (const auto& w : t.rows)
			{
				if (w.who != self_ && w.who != from && !find(w.who)) continue; // checked, then dropped below
				if (w.who == from) { ++host_rows; if (w.index != 0 && w.index != index_unknown) return count(m->gate.refuse(refusal::host_row, now)); }
				else if (w.index == 0) return count(m->gate.refuse(refusal::host_row, now));
			}
			if (host_rows != 1) return count(m->gate.refuse(refusal::host_row, now));
			t.rows.erase(std::remove_if(t.rows.begin(), t.rows.end(), [&](const table_row& w) { return w.who != self_ && w.who != from && !find(w.who); }), t.rows.end());
			m->gate.last_table = t.seq;
			table_ = std::move(t);
			has_table_ = true;
			table_at_ = now;
			return refusal::none;
		}

		// The cards: this PC first is NOT implied - order is native index (admitted), then pending members in roster
		// order. `self_phase`/`self_index`/`self_name`: this PC's own facts. `token`: this PC's version token.
		std::vector<card> cards(phase self_phase, int self_index, const std::string& self_name, bool self_ready, bool self_ready_known,
		                        const std::string& token, std::uint32_t now) const
		{
			std::vector<card> out;
			card me;
			me.who = self_;
			me.self = true;
			me.host = hosting();
			me.index = self_index;
			me.admitted = self_index >= 0;
			me.now = self_phase;
			me.phase_known = true;
			me.stage = build::same;
			me.name = self_name;
			me.ready = self_ready;
			me.ready_known = self_ready_known;
			if (open_) out.push_back(me);
			for (const auto& m : members_)
			{
				card c;
				c.who = m.who;
				c.host = m.who == host_;
				const bool fresh = m.has_report && now - m.report_at <= claim_stale_ms;
				const table_row* row = nullptr;
				if (has_table_ && now - table_at_ <= claim_stale_ms)
					for (const auto& w : table_.rows) if (w.who == m.who) row = &w;
				c.index = m.facts.index;
				// A host-reported index only for a member this PC cannot map itself, and only when free locally.
				if (c.index < 0 && row && row->index != index_unknown && m.facts.tunnel == false)
				{
					bool used = self_index == int(row->index);
					for (const auto& o : members_) used = used || (&o != &m && o.facts.index == int(row->index));
					if (!used) c.index = row->index;
				}
				c.admitted = m.facts.index >= 0;
				// Phase: the member's own claim, else the host's view, else local facts; local facts bound it below.
				if (fresh) { c.now = m.last.now; c.percent = m.last.percent; c.phase_known = true; }
				else if (row) { c.now = row->now; c.percent = row->percent; c.phase_known = true; }
				else if (m.facts.index >= 0) { c.now = phase::in_lobby; c.phase_known = true; }
				else if (m.facts.tunnel) { c.now = phase::checking_build; c.phase_known = true; }
				else { c.now = phase::connecting; c.phase_known = true; }
				if (c.admitted && (c.now == phase::connecting || c.now == phase::checking_build || c.now == phase::left)) c.now = phase::in_lobby;
				if (!c.admitted && m.facts.tunnel && c.now == phase::connecting) c.now = phase::checking_build;
				// Readiness is the native column's: a claimed `ready` never shows without it, and it shows with it.
				if (c.now == phase::ready && !(m.facts.ready_known && m.facts.ready)) c.now = c.admitted ? phase::in_lobby : m.facts.tunnel ? phase::checking_build : phase::connecting;
				if (c.admitted && m.facts.ready_known && m.facts.ready && c.now == phase::in_lobby) c.now = phase::ready;
				// RTT: own measurement, else the host's measurement to that member (never for the host itself).
				if (m.facts.rtt >= 0) { c.rtt = m.facts.rtt; c.from = rtt_source::local; c.how = m.facts.how; }
				else if (row && !c.host && row->ms != rtt_none && !hosting()) { c.rtt = row->ms; c.from = rtt_source::to_host; c.how = row->how; }
				// Build: an observed admission, else the token the member reported, else the host's admission row.
				if (c.admitted && m.facts.bound) c.stage = build::agreed;
				else if (fresh && !m.last.token.empty()) c.stage = m.last.token == token ? build::same : build::other;
				else if (row && row->stage == build::agreed) c.stage = build::agreed;
				c.ready = m.facts.ready;
				c.ready_known = m.facts.ready_known && c.admitted;
				c.name = c.admitted && !m.facts.native_name.empty() ? m.facts.native_name : fresh ? m.last.name : std::string();
				out.push_back(std::move(c));
			}
			std::stable_sort(out.begin(), out.end(), [](const card& a, const card& b) {
				const int ka = a.index >= 0 ? a.index : 100, kb = b.index >= 0 ? b.index : 100;
				return ka < kb;
			});
			return out;
		}
		std::size_t roster_size() const { return open_ ? members_.size() + 1 : 0; }
		// A digest of what the status roster shows (no RTT); the generation moves when it changes.
		bool note_cards(const std::vector<card>& cards)
		{
			std::uint64_t h = 1469598103934665603ull;
			auto mix = [&](std::uint64_t v) { h ^= v; h *= 1099511628211ull; };
			for (const auto& c : cards)
			{
				for (auto b : c.who) mix(b);
				mix(std::uint64_t(c.index + 1)); mix(unsigned(c.now)); mix(c.host); mix(unsigned(c.stage)); mix(unsigned(c.how));
				mix(c.ready_known ? (c.ready ? 2 : 1) : 0);
				for (char ch : c.name) mix(std::uint8_t(ch));
			}
			if (h == last_digest_) return false;
			last_digest_ = h;
			++generation_;
			return true;
		}

		// This PC's own report (sequence filled by the caller) and, hosting, its table.
		report own_report(phase now, int index, bool agreed, const std::string& token, const std::string& name) const
		{
			report r;
			std::memcpy(r.code, code_, code_size);
			r.now = now;
			r.index = index >= 0 && index <= 3 ? std::uint8_t(index) : index_unknown;
			r.agreed = agreed;
			r.token = token.substr(0, token_max);
			for (auto& c : r.token) if (!token_byte(std::uint8_t(c))) c = '?';
			r.name = online_status_core::utf8_clean(name, name_max);
			for (const auto& m : members_)
			{
				if (r.rtt.size() >= rtt_rows_max) break;
				if (m.facts.rtt < 0) continue;
				r.rtt.push_back({m.who, std::uint16_t(std::min(m.facts.rtt, int(rtt_ceiling))), m.facts.how});
			}
			return r;
		}
		table own_table(const std::vector<card>& cards, std::uint16_t generation) const
		{
			table t;
			std::memcpy(t.code, code_, code_size);
			t.generation = generation;
			for (const auto& c : cards)
			{
				if (t.rows.size() >= members_max) break;
				table_row w;
				w.who = c.who;
				w.index = c.index >= 0 && c.index <= 3 && (c.self ? c.index == 0 : c.index != 0) ? std::uint8_t(c.index) : index_unknown;
				w.now = c.now;
				w.percent = detail::percent_ok(c.now, c.percent) ? c.percent : percent_none;
				w.ms = c.self || c.from != rtt_source::local ? rtt_none : std::uint16_t(std::min(c.rtt, int(rtt_ceiling)));
				w.how = c.self ? route::unknown : c.how;
				w.stage = c.self ? build::agreed : c.stage;
				t.rows.push_back(w);
			}
			return t;
		}
		// The host table's member-set/index fingerprint: the host bumps its table generation when it changes.
		static std::uint64_t membership(const std::vector<card>& cards)
		{
			std::uint64_t h = 1469598103934665603ull;
			for (const auto& c : cards) { for (auto b : c.who) { h ^= b; h *= 1099511628211ull; } h ^= std::uint64_t(c.index + 7); h *= 1099511628211ull; }
			return h;
		}
	};

	// Match facts to this PC's native rows by index. Remaining room members have
	// presentation-only cards, capped at roster size minus native rows, so nobody
	// is drawn twice. A remote index alone cannot hide a late guest's host.
	inline lobby_split_result lobby_split(const std::vector<card>& cards, std::size_t native_count, std::size_t roster_size,
	                                     const std::vector<int>* native_indices)
	{
		lobby_split_result out;
		const std::size_t room = std::min<std::size_t>(3, roster_size > native_count ? roster_size - native_count : 0);
		for (const auto& c : cards)
		{
			// A host-table index does not create that participant on a waiting guest.
			// Match this PC's real native rows when supplied, not just a remote index.
			const bool listed = c.index >= 0 && (!native_indices || std::find(native_indices->begin(), native_indices->end(), c.index) != native_indices->end());
			if (listed) { if (out.rows.size() < 4) out.rows.push_back(&c); }
			else if (!c.self && out.pending.size() < room) out.pending.push_back(&c);
		}
		return out;
	}
}
