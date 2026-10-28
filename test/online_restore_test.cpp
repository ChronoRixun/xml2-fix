// Online restore (docs/online-restore.md, rule 63): the link kind 16 parsers under bounds and seeded fuzzing, and the
// host / guest transaction machines driven over a simulated room (loss, delay, duplication, departures, timeouts,
// host loss, queue-until-safe, rollback, re-pick). Pure: online_restore_core.hpp only. Optional argument: fuzz rounds.
#include "online_restore_core.hpp"
#include "gns_transport_core.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <tuple>
#include <vector>

namespace
{
	int passed = 0, failed = 0;
	void check(bool ok, const std::string& what)
	{
		std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
		(ok ? passed : failed)++;
	}
	namespace R = online_restore_core;
	using P = R::phase;
	using F = R::refusal;
	using T = R::type;
	const char* const CODE = "K7QN2X9M";
	const char* const OTHER = "ABCD2345";
	constexpr std::uint32_t SESSION = 0x51;
	R::key K(std::uint8_t n) { R::key k{}; k.fill(n); return k; }
	R::digest D(std::uint8_t n) { R::digest d{}; d.fill(n); return d; }
	std::string S(P p) { return R::name(p); }

	R::proposal sample_proposal()
	{
		R::proposal p;
		p.image_len = 0x200000;
		p.image = D(0xa1);
		p.source_len = 0x31000;
		p.source = D(0xb2);
		p.saved_seconds = 7322;
		p.desc = "Act 2 - Weapon X";
		p.area = "Muir Island";
		return p;
	}
	R::roster_info sample_roster(std::uint16_t story = R::story_none, R::key owner = R::key{})
	{
		R::roster_info r;
		r.heroes = {10, 11, 12, 13, 14, 15};
		r.story = story;
		r.story_owner = owner;
		return r;
	}
	R::message header(T t, std::uint32_t op, const char* code = CODE, std::uint32_t session = SESSION)
	{
		R::message m;
		m.t = t;
		std::memcpy(m.code, code, 8);
		m.session = session;
		m.op = op;
		return m;
	}

	// A room: the host is slot 0 (K(1), native index 0); guest slot s is K(s+1), native index s.
	struct room
	{
		int n;
		R::host h;
		std::vector<R::guest> g; // g[s-1] is slot s
		std::vector<R::member> active;
		std::vector<R::key> pending;
		std::uint32_t t = 1000;
		bool gone[4]{};
		std::function<bool(int from, int to, const std::vector<std::uint8_t>&)> drop; // from/to: 0 host, s guest
		bool twice = false;                                                         // deliver everything twice
		std::vector<std::tuple<int, int, std::vector<std::uint8_t>>> held;          // delayed by hold()
		std::function<bool(int from, int to, const std::vector<std::uint8_t>&)> hold;
		int host_refusals = 0, guest_refusals = 0;

		explicit room(int members) : n(members), h(CODE, SESSION, K(1))
		{
			for (int s = 0; s < n; ++s) active.push_back({K(std::uint8_t(s + 1)), std::uint8_t(s)});
			for (int s = 1; s < n; ++s) g.emplace_back(CODE, SESSION, K(std::uint8_t(s + 1)), K(1));
			sync();
			h.safety(true, R::unsafe::none, 500);
		}
		R::guest& G(int slot) { return g[std::size_t(slot - 1)]; }
		void sync()
		{
			h.roster(active, pending, t);
			for (auto& x : g) x.roster(active);
			pump();
		}
		void leave(int slot)
		{
			gone[slot] = true;
			for (auto it = active.begin(); it != active.end(); ++it)
				if (it->k == K(std::uint8_t(slot + 1))) { active.erase(it); break; }
			sync();
		}
		void deliver(int from, int to, const std::vector<std::uint8_t>& b)
		{
			if (gone[from] || gone[to]) return;
			if (drop && drop(from, to, b)) return;
			if (hold && hold(from, to, b)) { held.emplace_back(from, to, b); return; }
			for (int i = 0; i < (twice ? 2 : 1); ++i)
			{
				if (to == 0) { if (const auto r = h.receive(K(std::uint8_t(from + 1)), b.data(), b.size(), t); r != F::none && r != F::repeat && r != F::stale) ++host_refusals; }
				else if (const auto r = G(to).receive(K(1), b.data(), b.size(), t); r != F::none && r != F::repeat && r != F::stale) ++guest_refusals;
			}
		}
		void release()
		{
			auto h2 = held;
			held.clear();
			auto keep = hold;
			hold = nullptr;
			for (auto& [f, to, b] : h2) deliver(f, to, b);
			hold = keep;
			pump();
		}
		void pump()
		{
			for (int round = 0; round < 64; ++round)
			{
				bool any = false;
				for (auto& o : h.take())
				{
					any = true;
					for (int s = 1; s < n; ++s) if (o.slot == R::slot_none || o.slot == s) deliver(0, s, o.bytes);
				}
				for (int s = 1; s < n; ++s)
					for (auto& o : G(s).take()) { any = true; deliver(s, 0, o.bytes); }
				if (!any) return;
			}
		}
		void step(std::uint32_t ms)
		{
			for (std::uint32_t i = 0; i < ms; i += 100)
			{
				t += 100;
				h.tick(t);
				for (int s = 1; s < n; ++s) if (!gone[s]) G(s).tick(t);
				pump();
			}
		}
		F propose() { const auto r = h.propose(sample_proposal(), t); pump(); return r; }
		void vote_all(bool yes = true) { for (int s = 1; s < n; ++s) G(s).vote(yes, t); pump(); }
		void ready(const R::digest& rollback = D(0x77))
		{
			h.host_ready(true, rollback, t);
			pump();
		}
		void stage_all(const R::digest& rollback = D(0x77), int skip = -1)
		{
			const auto p = sample_proposal();
			for (int s = 1; s < n; ++s) if (s != skip && !gone[s]) G(s).staged(true, p.image_len, p.image, rollback, t);
			pump();
		}
		void load_all(const R::roster_info& r = sample_roster(), int skip = -1, std::uint32_t check_value = 77)
		{
			h.host_loaded(h.generation(), true, check_value, r, t);
			for (int s = 1; s < n; ++s) if (s != skip && !gone[s]) G(s).loaded(G(s).generation(), true, check_value, r, t);
			pump();
		}
		void seat_all(int skip = -1)
		{
			h.host_seated(t);
			for (int s = 1; s < n; ++s) if (s != skip && !gone[s]) G(s).seated(t);
			pump();
		}
		// Drive to a phase of the HOST.
		void to(P target)
		{
			if (target == P::queued) h.safety(false, R::unsafe::combat, 0);
			propose(); if (h.now() == target) return;
			vote_all(); if (h.now() == target) return;
			ready(); if (h.now() == target) return;
			stage_all(); if (h.now() == target) return;
			load_all(); if (h.now() == target) return;
			for (int s = 0; s < n; ++s) { if (s) G(s).pick(std::uint16_t(10 + s), t); else h.host_pick(10, t); }
			pump(); if (h.now() == target) return;
			seat_all(); if (h.now() == target) return;
			h.first_frame(t);
			for (int s = 1; s < n; ++s) G(s).first_frame(t);
		}
		bool guests_are(P p)
		{
			for (int s = 1; s < n; ++s) if (!gone[s] && G(s).now() != p) return false;
			return true;
		}
	};

	// ---------------------------------------------------------------------------------------------- parsers
	std::vector<R::message> samples()
	{
		std::vector<R::message> v;
		auto m = header(T::propose, 3);
		m.members = {{K(1), 0}, {K(2), 1}, {K(3), 2}, {K(4), 3}};
		m.image_len = 0x200000; m.image = D(1); m.source_len = 0x1000000; m.source = D(2); m.saved_seconds = 0xffffffff;
		m.desc = std::string(63, 'x'); m.area = std::string(31, 'y');
		v.push_back(m);
		m = header(T::propose, 4); m.members = {{K(1), 0}}; m.image_len = 1; m.image = D(3); m.source_len = 1; m.source = D(4); v.push_back(m);
		m = header(T::vote, 5); m.answer = 1; v.push_back(m);
		m = header(T::vote, 5); m.answer = 2; v.push_back(m);
		m = header(T::tally, 5); m.yes_mask = 0x0b; v.push_back(m);
		m = header(T::queued, 5); m.why = R::unsafe::cutscene; v.push_back(m);
		m = header(T::cancel, 5); m.because = R::reason::declined; m.slot = 2; v.push_back(m);
		m = header(T::cancel, 5); m.because = R::reason::vote_timeout; m.slot = R::slot_none; v.push_back(m);
		m = header(T::prepare, 5); m.frame = 12345; m.flag = 1; v.push_back(m);
		m = header(T::progress, 5); m.bytes = 0x100000; v.push_back(m);
		m = header(T::staged, 5); m.image_len = 0x200000; m.image = D(1); m.rollback = D(9); v.push_back(m);
		m = header(T::staged, 5); m.image_len = 7; m.image = D(1); v.push_back(m);
		m = header(T::fail, 5); m.at = P::preparing; m.because = R::reason::stage_failed; v.push_back(m);
		m = header(T::commit, 5); m.gen = 1; v.push_back(m);
		m = header(T::loaded, 5); m.gen = 1; m.check = 0xdeadbeef; v.push_back(m);
		m = header(T::repick, 5); m.gen = 1; m.heroes = {1, 2, 3}; m.story = 2; m.story_owner = 1; v.push_back(m);
		m = header(T::repick, 5); m.gen = 1; for (std::uint16_t i = 0; i < 32; ++i) m.heroes.push_back(std::uint16_t(100 + i)); v.push_back(m);
		m = header(T::pick, 5); m.gen = 1; m.seq = 1; m.hero = 3; v.push_back(m);
		m = header(T::pick, 5); m.gen = 1; m.seq = 0xffff; m.hero = R::hero_watch; v.push_back(m);
		m = header(T::assign, 5); m.gen = 1; m.flag = 0; m.heroes = {1, R::hero_open, R::hero_watch, R::hero_watch}; v.push_back(m);
		m = header(T::assign, 5); m.gen = 1; m.flag = 1; m.heroes = {1, 2}; v.push_back(m);
		m = header(T::seated, 5); m.gen = 2; v.push_back(m);
		m = header(T::resume, 5); m.gen = 2; v.push_back(m);
		m = header(T::freeze, 5); m.gen = 2; m.because = R::reason::load_failed; m.slot = 1; v.push_back(m);
		m = header(T::rollback, 6); m.previous_op = 5; m.gen = 3; m.rollback = D(9); v.push_back(m);
		m = header(T::end, 6); m.because = R::reason::rollback_failed; m.slot = R::slot_none; v.push_back(m);
		return v;
	}
	F parse_bytes(const std::vector<std::uint8_t>& b) { R::message m; return R::parse(b.data(), b.size(), m); }
	std::vector<std::uint8_t> enc(const R::message& m) { return R::encode(m); }
	template<class Edit> F mutated(R::message m, Edit e) { e(m); return parse_bytes(enc(m)); }

	void parser_tests()
	{
		std::size_t largest = 0;
		bool every = true;
		for (const auto& m : samples())
		{
			const auto b = enc(m);
			R::message back;
			const auto r = R::parse(b.data(), b.size(), back);
			every = every && r == F::none && enc(back) == b;
			if (r != F::none) std::printf("  sample type %u refused: %s\n", unsigned(m.t), R::name(r));
			largest = std::max(largest, b.size());
			// Exact length: one byte short / long is refused.
			auto shorter = b; shorter.pop_back();
			auto longer = b; longer.push_back(0);
			every = every && parse_bytes(shorter) != F::none && parse_bytes(longer) != F::none;
		}
		check(every, "parse: every sample of all 19 types round-trips exactly; one byte short or long is refused");
		check(largest == 229 && largest <= R::message_max && largest <= gns_transport_core::max_game_body,
		      "parse: the largest message (a full propose) is 229 B, inside the 256 B cap and the GNS game-body bound");
		const auto base = samples();
		const auto& prop = base[0];
		check(mutated(base[2], [](R::message&) {}) == F::none, "parse: a vote sample is accepted");
		{
			auto b = enc(base[2]);
			b[0] = 7; check(parse_bytes(b) == F::kind, "parse: another link kind (7 party report) is refused");
			b = enc(base[2]); b[1] = 2; check(parse_bytes(b) == F::wire, "parse: wire 2 is refused");
			b = enc(base[2]); b[2] = 0; check(parse_bytes(b) == F::type, "parse: type 0 is refused");
			b = enc(base[2]); b[2] = 20; check(parse_bytes(b) == F::type, "parse: type 20 (beyond tally) is refused");
			b.assign(18, 16); check(parse_bytes(b) == F::size, "parse: a short header is refused");
			b.assign(257, 0); b[0] = 16; b[1] = 1; b[2] = 1; check(parse_bytes(b) == F::size, "parse: more than 256 bytes are refused before anything else");
			R::message sink;
			check(R::parse(nullptr, 20, sink) == F::size, "parse: a null buffer is refused");
		}
		check(mutated(base[2], [](R::message& m) { std::memcpy(m.code, "K7QN2X9I", 8); }) == F::code, "parse: a room code outside the 31-symbol alphabet is refused");
		check(mutated(base[2], [](R::message& m) { m.session = 0; }) == F::session, "parse: session 0 is refused");
		check(mutated(base[2], [](R::message& m) { m.op = 0; }) == F::op, "parse: op 0 is refused");
		check(mutated(prop, [](R::message& m) { m.purpose = 2; }) == F::purpose, "propose: only purpose 1 (load) exists");
		check(mutated(prop, [](R::message& m) { m.members.clear(); }) == F::count, "propose: zero members are refused");
		check(mutated(prop, [](R::message& m) { m.members.push_back({K(5), 3}); }) == F::count, "propose: five members are refused");
		check(mutated(prop, [](R::message& m) { m.members[2].k = R::key{}; }) == F::key, "propose: an all-zero member key is refused");
		check(mutated(prop, [](R::message& m) { m.members[2].k = K(2); }) == F::duplicate, "propose: a member twice is refused");
		check(mutated(prop, [](R::message& m) { m.members[2].index = 1; }) == F::index, "propose: two members on one native index are refused");
		check(mutated(prop, [](R::message& m) { m.members[0].index = 1; m.members[1].index = 0; }) == F::index, "propose: slot 0 must be native index 0 (the host)");
		check(mutated(prop, [](R::message& m) { m.members[3].index = 4; }) == F::index, "propose: native index 4 is refused");
		check(mutated(prop, [](R::message& m) { m.members[1].index = 0; }) == F::index, "propose: native index 0 outside slot 0 is refused");
		check(mutated(prop, [](R::message& m) { m.image_len = 0; }) == F::length, "propose: an empty image is refused");
		check(mutated(prop, [](R::message& m) { m.image_len = 0x200001; }) == F::length, "propose: an image over the W2 2 MiB payload is refused");
		check(mutated(prop, [](R::message& m) { m.source_len = 0; }) == F::length, "propose: an empty source is refused");
		check(mutated(prop, [](R::message& m) { m.source_len = (16u << 20) + 1; }) == F::length, "propose: a source over 16 MiB is refused");
		check(mutated(prop, [](R::message& m) { m.image = R::digest{}; }) == F::digest, "propose: an all-zero image digest is refused");
		check(mutated(prop, [](R::message& m) { m.source = R::digest{}; }) == F::digest, "propose: an all-zero source digest is refused");
		{
			auto b = enc(prop);
			// desc length byte sits after the fixed propose fields
			const std::size_t at = R::header_size + 2 + 4 * 9 + 4 + 32 + 4 + 32 + 4;
			b[at] = 64; check(parse_bytes(b) == F::text, "propose: a 64-byte description is refused by its length byte");
			b = enc(prop); b[at + 1 + 63] = 32; check(parse_bytes(b) == F::text, "propose: a 32-byte area is refused by its length byte");
		}
		check(mutated(prop, [](R::message& m) { m.desc = "bad\x01name"; }) == F::text, "propose: a control character in the description is refused");
		check(mutated(prop, [](R::message& m) { m.area = "\xc3\x28"; }) == F::text, "propose: invalid UTF-8 in the area is refused");
		check(mutated(prop, [](R::message& m) { m.desc = "\xe2\x80\x94 ok \xc3\xa9"; }) == F::none, "propose: valid UTF-8 text is accepted");
		check(mutated(base[2], [](R::message& m) { m.answer = 0; }) == F::answer && mutated(base[2], [](R::message& m) { m.answer = 3; }) == F::answer, "vote: only 1 (yes) and 2 (no)");
		check(mutated(base[4], [](R::message& m) { m.yes_mask = 2; }) == F::flag && mutated(base[4], [](R::message& m) { m.yes_mask = 0x11; }) == F::flag, "tally: the host's bit is set, no bit beyond slot 3");
		check(mutated(base[5], [](R::message& m) { m.why = R::unsafe::none; }) == F::unsafe && mutated(base[5], [](R::message& m) { m.why = R::unsafe::count; }) == F::unsafe, "queued: an unsafe reason 1-7");
		check(mutated(base[6], [](R::message& m) { m.because = R::reason::none; }) == F::reason && mutated(base[6], [](R::message& m) { m.because = R::reason::count; }) == F::reason, "cancel: a known nonzero reason");
		check(mutated(base[6], [](R::message& m) { m.slot = 4; }) == F::slot, "cancel: slot 0-3 or none");
		check(mutated(base[8], [](R::message& m) { m.flag = 2; }) == F::flag, "prepare: the rollback flag is 0 or 1");
		check(mutated(base[9], [](R::message& m) { m.bytes = 0x200001; }) == F::bytes, "progress: no more than the image cap");
		check(mutated(base[10], [](R::message& m) { m.image_len = 0; }) == F::length && mutated(base[10], [](R::message& m) { m.image_len = 0x200001; }) == F::length, "staged: the image length is 1..2 MiB");
		check(mutated(base[10], [](R::message& m) { m.image = R::digest{}; }) == F::digest, "staged: an all-zero image digest is refused (an all-zero rollback digest means none)");
		check(mutated(base[12], [](R::message& m) { m.at = P::count; }) == F::phase && mutated(base[12], [](R::message& m) { m.because = R::reason::none; }) == F::reason, "fail: a known phase and reason");
		check(mutated(base[13], [](R::message& m) { m.gen = 0; }) == F::gen, "commit: generation 0 is refused");
		check(mutated(base[14], [](R::message& m) { m.check = 0; }) == F::check && mutated(base[14], [](R::message& m) { m.gen = 0; }) == F::gen, "loaded: a nonzero generation and check");
		check(mutated(base[15], [](R::message& m) { m.heroes.clear(); m.story = R::story_none; m.story_owner = R::slot_none; }) == F::count, "repick: an empty roster is refused");
		check(mutated(base[16], [](R::message& m) { m.heroes.push_back(999); }) == F::count, "repick: 33 heroes are refused");
		check(mutated(base[15], [](R::message& m) { m.heroes[0] = R::hero_watch; }) == F::hero, "repick: the watch/open markers are not hero ids");
		check(mutated(base[15], [](R::message& m) { m.heroes[2] = 1; }) == F::duplicate, "repick: a hero listed twice is refused");
		check(mutated(base[15], [](R::message& m) { m.story = 9; }) == F::story, "repick: a story hero outside the roster is refused");
		check(mutated(base[15], [](R::message& m) { m.story = R::story_none; }) == F::slot, "repick: a story owner without a story hero is refused");
		check(mutated(base[15], [](R::message& m) { m.story_owner = 4; }) == F::slot, "repick: story owner slot 4 is refused");
		check(mutated(base[17], [](R::message& m) { m.hero = R::hero_open; }) == F::hero, "pick: 'open' is not a pick");
		check(mutated(base[17], [](R::message& m) { m.seq = 0; }) == F::sequence, "pick: sequence 0 is refused (fix 8)");
		check(mutated(base[19], [](R::message& m) { m.flag = 2; }) == F::flag, "assign: final is 0 or 1");
		check(mutated(base[19], [](R::message& m) { m.flag = 1; }) == F::hero, "assign: a final assignment has no open slot");
		check(mutated(base[20], [](R::message& m) { m.heroes = {2, 2}; }) == F::duplicate, "assign: one hero for two slots is refused");
		check(mutated(base[20], [](R::message& m) { m.heroes = {R::hero_watch, R::hero_watch}; }) == F::none, "assign: several watchers are fine");
		check(mutated(base[20], [](R::message& m) { m.heroes = {1, 2, 3, 4, 5}; }) == F::count, "assign: five slots are refused");
		check(mutated(base[23], [](R::message& m) { m.because = R::reason::none; }) == F::reason && mutated(base[23], [](R::message& m) { m.slot = 7; }) == F::slot, "freeze: a known reason and slot");
		check(mutated(base[24], [](R::message& m) { m.previous_op = 6; }) == F::previous && mutated(base[24], [](R::message& m) { m.previous_op = 0; }) == F::previous, "rollback: the previous op is older than this one");
		check(mutated(base[24], [](R::message& m) { m.rollback = R::digest{}; }) == F::digest, "rollback: a nonzero rollback digest");
	}

	void fuzz(unsigned rounds)
	{
		std::mt19937 rng(0x0063u);
		const auto base = samples();
		unsigned accepted = 0, refused = 0;
		bool canonical = true;
		for (unsigned i = 0; i < rounds; ++i)
		{
			std::vector<std::uint8_t> b;
			const unsigned how = rng() % 5;
			if (how == 0)
			{
				b.resize(rng() % 300);
				for (auto& c : b) c = std::uint8_t(rng());
				if (b.size() > 2 && (rng() & 1)) { b[0] = 16; b[1] = 1; }
			}
			else
			{
				b = enc(base[rng() % base.size()]);
				const unsigned edits = 1 + rng() % 4;
				for (unsigned e = 0; e < edits; ++e)
				{
					const unsigned what = rng() % 4;
					if (what == 0 && !b.empty()) b[rng() % b.size()] ^= std::uint8_t(1u << (rng() % 8));
					else if (what == 1 && !b.empty()) b[rng() % b.size()] = std::uint8_t(rng());
					else if (what == 2 && !b.empty()) b.resize(rng() % b.size());
					else b.push_back(std::uint8_t(rng()));
				}
			}
			R::message m;
			const auto r = R::parse(b.data(), b.size(), m);
			if (r == F::none) { ++accepted; if (R::encode(m) != b) canonical = false; }
			else ++refused;
		}
		std::printf("  fuzz: %u rounds, %u accepted, %u refused\n", rounds, accepted, refused);
		check(canonical && accepted > 0 && refused > 0, "fuzz: seeded mutations never crash the parser and every accepted message re-encodes to the same bytes");

		// The machines under garbage from keys outside the operation (and not the host): nothing moves.
		room r(3);
		r.propose();
		for (unsigned i = 0; i < rounds / 10; ++i)
		{
			std::vector<std::uint8_t> b = enc(base[rng() % base.size()]);
			b[rng() % b.size()] ^= std::uint8_t(1u << (rng() % 8));
			const R::key from = K(std::uint8_t(5 + rng() % 4));
			r.h.receive(from, b.data(), b.size(), r.t);
			r.G(1).receive(from, b.data(), b.size(), r.t);
			r.h.take(); r.G(1).take();
		}
		check(r.h.now() == P::voting && r.h.yes_mask() == 1 && r.G(1).now() == P::voting, "fuzz: mutated messages from outside keys move neither the host nor a guest");
		// And from the members themselves, at several phases: no crash, phases stay valid (run under ASan too).
		const P starts[] = {P::voting, P::queued, P::distributing, P::committed, P::repick, P::seating};
		bool sane = true;
		for (int round = 0; round < 24; ++round)
		{
			room q(4);
			q.to(starts[round % 6]);
			for (unsigned i = 0; i < rounds / 50; ++i)
			{
				std::vector<std::uint8_t> b = enc(base[rng() % base.size()]);
				b[rng() % b.size()] ^= std::uint8_t(1u << (rng() % 8));
				const int from = int(rng() % 4);
				if (from == 0) q.G(1 + int(rng() % 3)).receive(K(1), b.data(), b.size(), q.t);
				else q.h.receive(K(std::uint8_t(from + 1)), b.data(), b.size(), q.t);
				q.t += 7;
				q.h.tick(q.t);
				q.pump();
			}
			sane = sane && q.h.now() < P::count && q.G(1).now() < P::count && q.G(3).now() < P::count;
		}
		check(sane, "fuzz: 24 rooms under member-key mutations at six phases run without fault");
	}

	// ---------------------------------------------------------------------------------------------- transaction
	void happy_paths()
	{
		for (int n = 1; n <= 4; ++n)
		{
			room r(n);
			const std::string tag = std::to_string(n) + " member(s): ";
			check(!r.h.admission_hold(), tag + "no hold before a proposal");
			check(r.propose() == F::none, tag + "the host proposes");
			check(r.h.admission_hold(), tag + "admission is held from PROPOSE on");
			check(n == 1 ? r.h.now() == P::preparing : r.h.now() == P::voting && r.guests_are(P::voting), tag + "everyone sees the proposal (a solo host's own Yes completes it)");
			if (n > 1) check(r.G(1).members().size() == std::size_t(n) && r.G(1).proposed().desc == "Act 2 - Weapon X" && r.G(1).vote_left_ms(r.t) == R::vote_ms, tag + "a guest shows the save text, the names and a 60 s countdown");
			r.vote_all();
			check(r.h.now() == P::preparing && r.guests_are(P::preparing) && r.h.yes_mask() == (1u << n) - 1, tag + "all Yes at a safe point: preparing everywhere");
			check(n == 1 || r.G(1).freeze_frame() == 500, tag + "the freeze frame reaches every guest");
			r.ready();
			check(r.h.now() == (n == 1 ? P::committed : P::distributing), tag + "the host's own image and rollback are ready");
			if (n > 1)
			{
				for (int s = 1; s < n; ++s) r.G(s).progress(0x100000, r.t);
				r.pump();
				r.stage_all(D(0x77), n - 1);
				check(r.h.now() == P::distributing, tag + "no COMMIT while one member has not staged");
				r.G(n - 1).staged(true, 0x200000, D(0xa1), D(0x77), r.t);
				r.pump();
			}
			check(r.h.now() == P::committed && r.guests_are(P::committed) && r.h.generation() == 1 && r.h.rollback_available(), tag + "every member staged: COMMIT generation 1, rollback verified");
			check(n == 1 || !r.G(1).may_resume_input(), tag + "a committed guest may not resume input");
			r.load_all(sample_roster(), n - 1);
			check(n == 1 ? r.h.now() == P::repick : r.h.now() == P::committed, tag + "one member not loaded: no re-pick, nobody resumes");
			if (n > 1) { r.G(n - 1).loaded(r.G(n - 1).generation(), true, 77, sample_roster(), r.t); r.pump(); }
			check(r.h.now() == P::repick && r.guests_are(P::repick), tag + "every member loaded with the same check: re-pick");
			for (int s = 1; s < n; ++s) check(r.G(s).pick(std::uint16_t(10 + s), r.t) == F::none, tag + "guest " + std::to_string(s) + " picks a hero");
			r.pump();
			check(r.h.now() == P::repick, tag + "the host has not picked: no final assignment");
			r.h.host_pick(10, r.t);
			r.pump();
			check(r.h.now() == P::seating && r.guests_are(P::seating), tag + "every pick in: final ASSIGN, seating");
			bool rows = true;
			for (int s = 0; s < n; ++s) rows = rows && r.h.picks().assign[std::size_t(s)] == 10 + s && (n == 1 || r.G(1).picks().assign[std::size_t(s)] == 10 + s);
			check(rows, tag + "everyone holds the same assignment");
			r.seat_all(n - 1);
			check(n == 1 ? r.h.now() == P::resuming : r.h.now() == P::seating && !r.G(1).may_resume_input(), tag + "no RESUME without every member seated");
			if (n > 1) { r.G(n - 1).seated(r.t); r.pump(); }
			check(r.h.now() == P::resuming && r.guests_are(P::resuming) && (n == 1 || r.G(1).may_resume_input()), tag + "every member seated: RESUME; input may be produced");
			r.h.first_frame(r.t);
			for (int s = 1; s < n; ++s) r.G(s).first_frame(r.t);
			check(r.h.now() == P::playing && r.guests_are(P::playing) && !r.h.admission_hold() && r.h.outcome() == R::reason::none, tag + "the first shared frame: Playing, admission released");
			check(r.propose() == F::none && r.h.op() == 2, tag + "a new operation can follow (op 2)");
		}
	}

	void vote_permutations()
	{
		// Every answer (yes / no / silent) of every guest, in every arrival order, for 1-4 members.
		int cases = 0;
		bool ok = true;
		for (int n = 1; n <= 4; ++n)
		{
			const int guests = n - 1;
			int combos = 1;
			for (int i = 0; i < guests; ++i) combos *= 3;
			std::vector<int> order(static_cast<std::size_t>(guests));
			for (int c = 0; c < combos; ++c)
			{
				std::vector<int> answer(static_cast<std::size_t>(guests));
				for (int i = 0, v = c; i < guests; ++i, v /= 3) answer[std::size_t(i)] = v % 3; // 0 yes, 1 no, 2 silent
				for (int i = 0; i < guests; ++i) order[std::size_t(i)] = i + 1;
				do
				{
					++cases;
					room r(n);
					r.propose();
					int first_no = -1;
					bool early = false;
					for (int s : order)
					{
						if (r.h.now() == P::preparing) early = true;
						const int a = answer[std::size_t(s - 1)];
						if (a == 2) continue;
						if (a == 1 && first_no < 0) first_no = s;
						r.G(s).vote(a == 0, r.t);
						r.pump();
					}
					bool all_yes = true, any_no = false;
					for (int a : answer) { all_yes = all_yes && a == 0; any_no = any_no || a == 1; }
					if (all_yes) ok = ok && r.h.now() == P::preparing && r.guests_are(P::preparing) && !early;
					else if (any_no) ok = ok && r.h.now() == P::cancelled && r.h.outcome() == R::reason::declined && r.h.outcome_slot() == first_no && r.guests_are(P::cancelled);
					else
					{
						ok = ok && r.h.now() == P::voting;
						r.step(R::vote_ms);
						ok = ok && r.h.now() == P::cancelled && r.h.outcome() == R::reason::vote_timeout;
						for (int s = 1; s < n; ++s) ok = ok && r.G(s).now() == P::cancelled;
					}
				} while (std::next_permutation(order.begin(), order.end()));
			}
		}
		std::printf("  vote permutations: %d cases\n", cases);
		check(ok && cases == 1 + 3 + 9 * 2 + 27 * 6, "votes: every yes/no/silent combination in every order - unanimous Yes prepares (never early), the first No cancels by name, silence cancels at 60 s");
	}

	void vote_details()
	{
		{
			room r(3);
			r.propose();
			r.G(1).vote(true, r.t);
			r.pump();
			r.step(59000);
			check(r.h.now() == P::voting && r.G(1).vote_left_ms(r.t) <= 1100, "votes: still voting at 59 s with one Yes");
			r.G(1).vote(false, r.t);
			r.pump();
			check(r.h.now() == P::cancelled && r.h.outcome() == R::reason::declined && r.h.outcome_slot() == 1, "votes: a Yes can be withdrawn while voting");
			check(r.G(1).vote(true, r.t) == F::out_of_phase, "votes: a No is final (no Yes after it)");
		}
		{
			room r(3);
			r.h.safety(false, R::unsafe::cutscene, 0);
			r.propose();
			r.vote_all();
			check(r.h.now() == P::queued && r.guests_are(P::queued) && r.G(2).waiting_for() == R::unsafe::cutscene, "queue: all Yes during a cutscene: queued everywhere, 'Load pending' with the reason");
			check(r.G(2).vote(true, r.t) == F::out_of_phase, "queue: a Yes while queued is not new");
			r.G(2).vote(false, r.t);
			r.pump();
			check(r.h.now() == P::cancelled && r.h.outcome() == R::reason::declined && r.h.outcome_slot() == 2 && r.guests_are(P::cancelled), "queue: a member may withdraw while queued");
		}
		{
			room r(2);
			r.propose();
			// A forged vote: the payload names no voter, the link does. A non-member key, the host's key, a guest decision.
			auto v = header(T::vote, 1); v.answer = 1;
			auto b = enc(v);
			check(r.h.receive(K(9), b.data(), b.size(), r.t) == F::unauthorized, "authority: a vote from a key outside the operation is refused");
			check(r.h.receive(K(1), b.data(), b.size(), r.t) == F::unauthorized, "authority: a vote arriving with the host's own key is refused");
			auto c = header(T::commit, 1); c.gen = 1; b = enc(c);
			check(r.h.receive(K(2), b.data(), b.size(), r.t) == F::unauthorized, "authority: a member sending a decision (commit) is refused");
			check(r.G(1).receive(K(3), b.data(), b.size(), r.t) == F::unauthorized, "authority: a guest refuses a decision from a key other than the host");
			b = enc(v);
			check(r.G(1).receive(K(1), b.data(), b.size(), r.t) == F::unauthorized, "authority: a guest refuses a reply-type message even from the host");
			check(r.h.now() == P::voting && r.h.yes_mask() == 1, "authority: nothing above counted as a Yes");
			auto f = header(T::vote, 1, OTHER); f.answer = 1; b = enc(f);
			check(r.h.receive(K(2), b.data(), b.size(), r.t) == F::foreign, "foreign: a vote for another room code");
			f = header(T::vote, 1, CODE, SESSION + 1); f.answer = 1; b = enc(f);
			check(r.h.receive(K(2), b.data(), b.size(), r.t) == F::foreign, "foreign: a vote for another session generation");
			auto p = header(T::prepare, 1, CODE, SESSION + 1); b = enc(p);
			check(r.G(1).receive(K(1), b.data(), b.size(), r.t) == F::foreign, "foreign: a guest refuses another session's prepare");
			f = header(T::vote, 7); f.answer = 1; b = enc(f);
			check(r.h.receive(K(2), b.data(), b.size(), r.t) == F::stale, "stale: a vote for an op the host never opened");
			check(r.h.propose(sample_proposal(), r.t) == F::busy, "one at a time: the host cannot propose while an operation is open");
			check(r.h.cancel(r.t) == F::none && r.h.now() == P::cancelled && r.h.outcome() == R::reason::host_cancel, "cancel: the host cancels while voting");
			r.pump();
			check(r.G(1).now() == P::cancelled && r.G(1).outcome() == R::reason::host_cancel, "cancel: the guest shows the host's Cancel");
			check(r.h.cancel(r.t) == F::out_of_phase, "cancel: nothing to cancel twice");
			r.propose();
			check(r.h.op() == 2 && r.G(1).op() == 2 && r.G(1).now() == P::voting, "re-propose: a new op, the guest votes again");
			v = header(T::vote, 1); v.answer = 1; b = enc(v);
			check(r.h.receive(K(2), b.data(), b.size(), r.t) == F::stale && r.h.yes_mask() == 1, "stale: a late Yes for the cancelled op does not count for the new one");
		}
		{
			// A guest whose own view of the active set differs refuses the membership; the host cancels.
			room r(3);
			r.G(2).roster({{K(1), 0}, {K(3), 2}});
			r.propose();
			check(r.h.now() == P::cancelled && r.h.outcome() == R::reason::membership && r.h.outcome_slot() == 2, "membership: a guest that sees a different active set refuses; the host cancels for everyone");
			check(r.G(1).now() == P::cancelled, "membership: the other guest is cancelled too");
			room q(2);
			q.active = {{K(1), 1}, {K(2), 0}};
			q.sync();
			check(q.h.propose(sample_proposal(), q.t) == F::membership && q.h.now() == P::idle, "membership: the host must be native index 0 in its own active set");
			q.active = {{K(1), 0}, {K(2), 1}, {K(3), 1}};
			q.sync();
			check(q.h.propose(sample_proposal(), q.t) == F::membership, "membership: two members on one native index refuse the proposal");
			q.active = {{K(1), 0}, {K(2), 1}};
			q.sync();
			auto bad = sample_proposal(); bad.image_len = 0x200001;
			check(q.h.propose(bad, q.t) == F::length, "propose: the host refuses an image over 2 MiB itself");
			bad = sample_proposal(); bad.desc = std::string(64, 'a');
			check(q.h.propose(bad, q.t) == F::text, "propose: the host refuses an over-long description itself");
		}
	}

	void queue_tests()
	{
		{
			room r(3);
			r.h.safety(false, R::unsafe::combat, 0);
			r.propose();
			r.vote_all();
			bool never = true;
			for (int i = 0; i < 600; ++i)
			{
				r.h.safety(false, R::unsafe(1 + i % 7), 0);
				r.step(100);
				never = never && r.h.now() == P::queued && r.guests_are(P::queued);
			}
			check(never, "queue: 60 s of unsafe ticks (every reason) never prepares; no queue timeout");
			r.h.safety(true, R::unsafe::none, 4242);
			r.step(100);
			check(r.h.now() == P::preparing && r.guests_are(P::preparing) && r.G(1).freeze_frame() == 4242, "queue: the first safe tick prepares, without a re-vote, at that tick's freeze frame");
		}
		{
			room r(3);
			r.h.safety(false, R::unsafe::joining, 0);
			r.propose();
			r.vote_all();
			r.pending = {K(7)};
			r.sync();
			check(r.h.now() == P::queued && r.guests_are(P::queued), "queue (decision 3): a PC arriving to WAIT does not drop the queued Load");
			r.active.push_back({K(7), 3});
			r.sync();
			check(r.h.now() == P::cancelled && r.h.outcome() == R::reason::roster_changed && r.guests_are(P::cancelled), "queue: a PC becoming ACTIVE while queued drops the request for everyone");
			check(r.h.propose(sample_proposal(), r.t) == F::none && r.h.op() == 2, "queue: the host must propose again (new op), everyone confirms again");
		}
		{
			room r(3);
			r.pending = {K(7)};
			r.sync();
			r.h.safety(false, R::unsafe::combat, 0);
			r.propose();
			r.vote_all();
			r.pending.clear();
			r.sync();
			check(r.h.now() == P::queued, "queue (decision 3): a waiting PC leaving does not drop it either");
		}
		{
			room r(3);
			r.h.safety(false, R::unsafe::combat, 0);
			r.propose();
			r.vote_all();
			r.leave(2);
			check(r.h.now() == P::cancelled && r.h.outcome() == R::reason::member_left && r.h.outcome_slot() == 2 && r.G(1).now() == P::cancelled, "queue: an active member leaving while queued drops it (named)");
		}
		{
			room r(2);
			r.h.safety(false, R::unsafe::conversation, 0);
			r.propose();
			r.vote_all();
			check(r.h.cancel(r.t) == F::none && r.h.now() == P::cancelled, "queue: the host's Cancel while queued");
			r.pump();
			check(r.G(1).now() == P::cancelled && r.G(1).outcome() == R::reason::host_cancel, "queue: the guest sees the Cancel");
		}
		{
			room r(3);
			r.propose();
			r.pending = {K(7)};
			r.sync();
			check(r.h.now() == P::voting, "voting: a pending arrival waits outside the vote (no cancel)");
			r.vote_all();
			check(r.h.now() == P::preparing && r.h.admission_hold(), "voting: the vote completes; the newcomer stays held");
			r.pending = {K(7), K(8)};
			r.sync();
			check(r.h.now() == P::preparing, "preparing: pending changes are held outside");
		}
	}

	// Drive a 3-member room to a phase, make member 2 leave or a newcomer become active, report the host's result.
	void membership_every_phase()
	{
		const P phases[] = {P::voting, P::queued, P::preparing, P::distributing, P::committed, P::repick, P::seating, P::resuming};
		for (P p : phases)
		{
			{
				room r(3);
				r.to(p);
				const bool at = r.h.now() == p;
				r.leave(2);
				const P after = r.h.now();
				std::string what = "departure in " + S(p) + ": ";
				if (R::precommit(p))
					check(at && after == P::cancelled && r.h.outcome() == R::reason::member_left && r.h.outcome_slot() == 2 && r.G(1).now() == P::cancelled && !r.G(1).world_replaced(),
					      what + "cancelled, named, the world unchanged");
				else if (p == P::committed)
					check(at && after == P::rolling_back && r.G(1).now() == P::rolling_back && r.h.members().size() == 3 && r.h.departed(2), what + "freeze, verified rollback on the remaining members");
				else
					check(at && after == p && r.h.departed(2), what + "an ordinary departure past the consistency line (all loaded the same world)");
			}
			{
				room r(3);
				r.to(p);
				r.active.push_back({K(9), 3});
				r.sync();
				std::string what = "newcomer active in " + S(p) + ": ";
				if (R::precommit(p)) check(r.h.now() == P::cancelled && r.h.outcome() == R::reason::roster_changed, what + "cancelled (admission should have been held)");
				else if (p == P::committed) check(r.h.now() == P::rolling_back && r.h.outcome() == R::reason::roster_changed, what + "freeze");
				else check(r.h.now() == p, what + "held outside, no effect");
			}
		}
		{
			// A departure during re-pick frees its hero; the re-pick completes with the rest.
			room r(3);
			r.to(P::repick);
			r.G(2).pick(13, r.t);
			r.pump();
			r.leave(2);
			check(r.h.now() == P::repick && r.h.picks().assign[2] == R::hero_watch, "re-pick: the departed member's hero is released");
			r.h.host_pick(13, r.t);
			r.G(1).pick(11, r.t);
			r.pump();
			check(r.h.now() == P::seating && r.h.picks().assign[0] == 13 && r.G(1).now() == P::seating, "re-pick: someone else can take the departed member's hero; done with the rest");
			r.seat_all();
			check(r.h.now() == P::resuming && r.G(1).now() == P::resuming, "seating: the departed member is not waited for");
		}
	}

	void staging_failures()
	{
		{
			room r(3);
			r.to(P::distributing);
			auto s = header(T::staged, 1); s.image_len = 0x200000; s.image = D(0xa2); s.rollback = D(0x77);
			auto b = enc(s);
			r.h.receive(K(3), b.data(), b.size(), r.t);
			r.pump();
			check(r.h.now() == P::cancelled && r.h.outcome() == R::reason::digest_mismatch && r.h.outcome_slot() == 2 && r.guests_are(P::cancelled), "digest: a STAGED naming another image digest cancels for everyone");
		}
		{
			room r(3);
			r.to(P::distributing);
			auto s = header(T::staged, 1); s.image_len = 0x1fffff; s.image = D(0xa1); auto b = enc(s);
			r.h.receive(K(2), b.data(), b.size(), r.t);
			check(r.h.now() == P::cancelled && r.h.outcome() == R::reason::digest_mismatch, "digest: a STAGED naming another length cancels");
		}
		{
			room r(3);
			r.to(P::distributing);
			r.G(1).staged(true, 0x200000, D(0xee), D(0x77), r.t);
			r.pump();
			check(r.G(1).now() == P::cancelled && r.h.now() == P::cancelled && r.h.outcome() == R::reason::digest_mismatch && r.h.outcome_slot() == 1, "digest: a guest whose received bytes hash differently refuses; everyone cancels");
		}
		{
			room r(3);
			r.to(P::distributing);
			r.G(2).staged(false, 0, R::digest{}, R::digest{}, r.t);
			r.pump();
			check(r.h.now() == P::cancelled && r.h.outcome() == R::reason::stage_failed && r.G(1).now() == P::cancelled, "staging: a failed staging cancels for everyone");
		}
		{
			room r(3);
			r.to(P::distributing);
			check(r.G(1).abort(r.t) == F::none && r.G(1).now() == P::cancelled, "abort: a guest backs out before staging");
			r.pump();
			check(r.h.now() == P::cancelled && r.h.outcome() == R::reason::guest_abort && r.G(2).now() == P::cancelled, "abort: everyone cancels, world unchanged");
		}
		{
			room r(3);
			r.to(P::distributing);
			r.stage_all(D(0x77), 2);
			check(r.G(1).now() == P::staged && r.G(1).abort(r.t) == F::closed, "abort: after STAGED a guest is bound (no abort)");
			check(r.h.cancel(r.t) == F::none, "cancel: the host can still cancel before COMMIT");
			r.pump();
			check(r.G(1).now() == P::cancelled && r.G(2).now() == P::cancelled && !r.G(1).world_replaced(), "cancel: a staged guest returns to the unchanged world");
		}
		{
			room r(2);
			r.to(P::preparing);
			r.h.host_ready(false, R::digest{}, r.t);
			r.pump();
			check(r.h.now() == P::cancelled && r.h.outcome() == R::reason::host_prepare_failed && r.G(1).now() == P::cancelled, "prepare: the host's own staging failure cancels");
		}
		{
			room r(3);
			r.to(P::distributing);
			auto s = header(T::staged, 1); s.image_len = 0x200000; s.image = D(0xa1); s.rollback = D(0x77);
			auto b = enc(s);
			r.h.receive(K(2), b.data(), b.size(), r.t);
			s.rollback = D(0x78); b = enc(s);
			check(r.h.receive(K(2), b.data(), b.size(), r.t) == F::conflict, "conflict: a second, different STAGED from one member is refused");
		}
	}

	void timeouts()
	{
		{
			room r(2);
			r.to(P::preparing);
			r.step(R::prepare_ms - 200);
			check(r.h.now() == P::preparing, "timeout: own preparation still waiting at 29.8 s");
			r.step(300);
			check(r.h.now() == P::cancelled && r.h.outcome() == R::reason::prepare_timeout && r.G(1).now() == P::cancelled, "timeout: own preparation at 30 s cancels");
		}
		{
			room r(3);
			r.to(P::distributing);
			r.step(R::stall_ms + 100);
			check(r.h.now() == P::cancelled && r.h.outcome() == R::reason::transfer_stalled && r.h.outcome_slot() == 1, "timeout: 30 s without progress cancels (stalled), naming the first member not staged");
		}
		{
			room r(3);
			r.to(P::distributing);
			std::uint32_t bytes = 0;
			bool alive = true;
			for (int i = 0; i < 17; ++i)
			{
				r.step(10000);
				bytes += 0x10000;
				r.G(1).progress(bytes, r.t);
				r.G(2).progress(bytes, r.t);
				r.pump();
				alive = alive && r.h.now() == P::distributing;
			}
			check(alive, "timeout: a slow transfer that keeps advancing is not a stall (170 s)");
			r.step(10000);
			check(r.h.now() == P::cancelled && r.h.outcome() == R::reason::transfer_timeout, "timeout: distribution's 180 s cap cancels");
		}
		{
			room r(3);
			r.to(P::committed);
			r.load_all(sample_roster(), 2);
			r.step(R::load_ms + 100);
			check(r.h.now() == P::rolling_back && r.h.outcome() == R::reason::load_timeout && r.h.outcome_slot() == 2, "timeout: a member not loaded by 120 s freezes everyone (rollback verified -> rolling back)");
			r.step(R::load_ms + 100);
			check(r.h.now() == P::ended && r.guests_are(P::ended), "timeout: the rollback not loaded by 120 s ends the session (never a second rollback)");
		}
		{
			room r(3);
			r.to(P::repick);
			r.G(1).pick(12, r.t);
			r.pump();
			r.step(R::repick_ms + 100);
			check(r.h.now() == P::seating && r.h.picks().assign[0] == 10 && r.h.picks().assign[1] == 12 && r.h.picks().assign[2] == 11 && r.G(2).picks().assign[2] == 11,
			      "timeout: at 90 s unpicked members get the lowest free roster heroes in roster order");
		}
		{
			room r(3);
			r.to(P::seating);
			r.seat_all(2);
			r.step(R::seat_ms + 100);
			check(r.h.now() == P::resuming && r.h.departed(2) && r.G(1).now() == P::resuming && !r.G(2).may_resume_input(),
			      "timeout (fix 6): a member not seated by 30 s departs; the rest resume (everyone holds the same world)");
		}
		{
			room r(2);
			r.to(P::resuming);
			r.step(R::resume_ms + 100);
			check(r.h.now() == P::ended && r.h.outcome() == R::reason::resume_timeout && r.G(1).now() == P::ended, "timeout: no shared frame within 30 s of RESUME ends the session");
		}
		{
			room r(3);
			r.propose();
			r.drop = [](int from, int, const std::vector<std::uint8_t>&) { return from == 0; };
			r.step(R::guest_silence_ms + 100);
			check(r.G(1).now() == P::cancelled && r.G(1).outcome() == R::reason::host_silent, "silence: a guest that hears nothing from the host for 10 s before staging backs out");
			r.drop = nullptr;
			r.pump();
			check(r.h.now() == P::cancelled && r.h.outcome() == R::reason::host_silent, "silence: the host cancels when that FAIL arrives");
		}
		{
			room r(3);
			r.to(P::distributing);
			r.stage_all(D(0x77), 2);
			r.drop = [](int from, int, const std::vector<std::uint8_t>&) { return from == 0; };
			r.step(60000);
			check(r.G(1).now() == P::staged, "silence: a staged guest never leaves on its own (only CANCEL/COMMIT/FREEZE/END/host loss)");
		}
	}

	// Another session generation or an older operation, at every phase: refused both ways, nothing moves.
	void generations_every_phase()
	{
		const P phases[] = {P::voting, P::queued, P::preparing, P::distributing, P::committed, P::repick, P::seating, P::resuming};
		bool ok = true;
		for (P p : phases)
		{
			room r(3);
			r.to(p);
			const P hp = r.h.now(), gp = r.G(1).now();
			const std::uint32_t op = r.h.op(), gen = r.h.generation();
			auto e = header(T::end, op, CODE, SESSION + 1); e.because = R::reason::host_cancel; auto b = enc(e);
			ok = ok && r.G(1).receive(K(1), b.data(), b.size(), r.t) == F::foreign;
			if (op > 1) { e = header(T::end, op - 1); e.because = R::reason::host_cancel; b = enc(e); ok = ok && r.G(1).receive(K(1), b.data(), b.size(), r.t) == F::stale; }
			auto c = header(T::cancel, op + 5); c.because = R::reason::host_cancel; b = enc(c);
			ok = ok && r.G(1).receive(K(1), b.data(), b.size(), r.t) == F::stale;
			auto l = header(T::loaded, op, CODE, SESSION + 1); l.gen = gen ? gen : 1; l.check = 77; b = enc(l);
			ok = ok && r.h.receive(K(2), b.data(), b.size(), r.t) == F::foreign;
			l = header(T::fail, op + 1); l.at = P::voting; l.because = R::reason::guest_abort; b = enc(l);
			ok = ok && r.h.receive(K(2), b.data(), b.size(), r.t) == F::stale;
			auto s2 = header(T::seated, op); s2.gen = gen + 7; b = enc(s2);
			const auto sr = r.h.receive(K(2), b.data(), b.size(), r.t);
			ok = ok && (sr == F::stale || sr == F::out_of_phase);
			r.pump();
			const bool same = r.h.now() == hp && r.G(1).now() == gp;
			ok = ok && same;
			if (!same) std::printf("  generation probe moved %s: host %s guest %s\n", S(p).c_str(), S(r.h.now()).c_str(), S(r.G(1).now()).c_str());
		}
		check(ok, "generations: another session, an older/newer op and another restore generation are refused at every phase, both ways, and change nothing");
	}

	void host_loss_every_phase()
	{
		const P phases[] = {P::voting, P::queued, P::preparing, P::distributing, P::committed, P::repick, P::seating, P::resuming, P::playing};
		for (P p : phases)
		{
			room r(3);
			r.to(p);
			const P guest_phase = r.G(1).now();
			r.G(1).host_lost(r.t);
			const bool replaced = !(R::precommit(guest_phase) || guest_phase == P::idle || guest_phase == P::cancelled);
			check(r.G(1).now() == P::ended && r.G(1).outcome() == R::reason::host_lost && r.G(1).world_replaced() == replaced,
			      "host loss in " + S(p) + " (guest " + S(guest_phase) + "): ended, world " + (replaced ? "replaced" : "unchanged"));
			auto c = header(T::commit, r.h.op()); c.gen = 9; auto b = enc(c);
			check(r.G(1).receive(K(1), b.data(), b.size(), r.t) == F::closed && !r.G(1).may_resume_input(), "host loss in " + S(p) + ": nothing after it resumes the guest");
		}
	}


	struct lossy_result { bool reached = false, consistent = true; std::string host, guests; };
	lossy_result lossy(unsigned seed, int mode)
	{
		room r(mode == 0 ? 1 + int(seed % 4) : 2 + int(seed % 3));
		std::mt19937 rng(seed * 7919u + unsigned(mode));
		r.drop = [&rng](int, int, const std::vector<std::uint8_t>&) { return rng() % 3 == 0; };
		r.twice = seed % 5 == 0;
		// Story rosters every other run: saved owner present (a guest), departed (K(9)) or unknown.
		R::roster_info roster = sample_roster();
		if (mode == 0 && seed % 2 == 0)
		{
			const unsigned v = (seed / 2) % 3;
			roster = sample_roster(12, v == 0 ? K(std::uint8_t(r.n)) : v == 1 ? K(9) : R::key{});
		}
		const R::digest cap = mode == 2 ? R::digest{} : D(0x77);
		const auto p = sample_proposal();
		const unsigned cancel_at = 5 + rng() % 30;
		bool failed_once = false, cancelled = false;
		std::vector<bool> picked(std::size_t(r.n), false);
		r.propose();
		for (int i = 0; i < 1200; ++i)
		{
			bool all_resumed = true;
			for (int s = 1; s < r.n; ++s)
			{
				auto& g = r.G(s);
				switch (g.now())
				{
				case P::voting: g.vote(true, r.t); break;
				case P::preparing: if (mode != 1) g.staged(true, p.image_len, p.image, cap, r.t); break;
				case P::committed:
					if (mode >= 2 && s == 1 && !failed_once) { failed_once = true; g.loaded(g.generation(), false, 0, roster, r.t); }
					else g.loaded(g.generation(), true, 77, roster, r.t);
					break;
				case P::rolling_back: g.loaded(g.generation(), true, 55, roster, r.t); break;
				case P::repick: if (!picked[std::size_t(s)]) { picked[std::size_t(s)] = true; g.pick(roster.story != R::story_none ? roster.story : std::uint16_t(10 + s), r.t); } break;
				case P::seating: g.seated(r.t); break;
				case P::resuming: g.first_frame(r.t); break;
				default: break;
				}
				all_resumed = all_resumed && (g.now() == P::resuming || g.now() == P::playing);
			}
			switch (r.h.now())
			{
			case P::distributing: if (mode == 1 && !cancelled && unsigned(i) >= cancel_at) { cancelled = true; r.h.cancel(r.t); } break;
			case P::preparing: r.h.host_ready(true, cap, r.t); break;
			case P::committed: r.h.host_loaded(r.h.generation(), true, 77, roster, r.t); break;
			case P::rolling_back: r.h.host_loaded(r.h.generation(), true, 55, roster, r.t); break;
			case P::repick: if (!picked[0]) { picked[0] = true; r.h.host_pick(roster.story != R::story_none ? roster.story : 10, r.t); } break;
			case P::seating: r.h.host_seated(r.t); break;
			case P::resuming: if (all_resumed) r.h.first_frame(r.t); break; // the native barrier needs everyone's input
			default: break;
			}
			r.step(100);
			if (mode == 1 && r.h.now() == P::voting && r.t > 70000) break;
		}
		const P want = mode == 1 ? P::cancelled : mode == 2 ? P::ended : P::playing;
		lossy_result x;
		const P hp = r.h.now();
		x.host = S(hp);
		x.reached = hp == want;
		for (int s = 1; s < r.n; ++s)
		{
			const P gp = r.G(s).now();
			x.guests += S(gp) + " ";
			if (gp != want) x.reached = false;
			if (hp == P::cancelled && !(R::precommit(gp) || gp == P::cancelled)) x.consistent = false;
			if ((gp == P::resuming || gp == P::playing) && !(hp == P::resuming || hp == P::playing)) x.consistent = false;
		}
		if (mode == 3 && !r.h.rolled_back()) x.reached = false;
		return x;
	}

	void delays_and_duplicates()
	{
		{
			room r(3);
			r.to(P::distributing);
			r.hold = [](int from, int to, const std::vector<std::uint8_t>& b) { return from == 0 && to == 2 && b[2] == std::uint8_t(T::commit); };
			r.stage_all();
			check(r.h.now() == P::committed && r.G(1).now() == P::committed && r.G(2).now() == P::staged, "delay: a held COMMIT leaves that guest staged");
			r.hold = nullptr;
			r.held.clear();
			r.step(R::resend_ms + 100);
			check(r.G(2).now() == P::committed, "delay: the host's resend delivers COMMIT");
			r.load_all();
			check(r.h.now() == P::repick, "delay: then everyone loads");
		}
		{
			room r(3);
			r.to(P::committed);
			r.hold = [](int from, int, const std::vector<std::uint8_t>& b) { return from == 2 && b[2] == std::uint8_t(T::loaded); };
			r.load_all();
			check(r.h.now() == P::committed && !r.G(1).may_resume_input(), "delay: a held LOADED keeps everyone frozen (local load completion is not enough)");
			r.release();
			check(r.h.now() == P::repick && r.guests_are(P::repick), "delay: the late LOADED completes the barrier");
		}
		{
			room r(4);
			r.twice = true;
			r.to(P::playing);
			check(r.h.now() == P::playing && r.guests_are(P::playing) && r.host_refusals == 0, "duplicates: every message delivered twice - same result, no refusal beyond 'repeat'");
		}
		{
			room r(3);
			r.to(P::committed);
			auto old = header(T::loaded, r.h.op()); old.gen = 99; old.check = 77; auto b = enc(old);
			check(r.h.receive(K(2), b.data(), b.size(), r.t) == F::stale && r.h.now() == P::committed, "stale: a LOADED for another generation is refused while committed");
			r.load_all();
			auto l = header(T::loaded, r.h.op()); l.gen = r.h.generation(); l.check = 77; b = enc(l);
			check(r.h.receive(K(2), b.data(), b.size(), r.t) == F::repeat, "duplicate: a resent LOADED is a repeat");
			l.check = 78; b = enc(l);
			check(r.h.receive(K(2), b.data(), b.size(), r.t) == F::out_of_phase && r.h.now() == P::repick, "stale: a different LOADED after re-pick began changes nothing");
		}
		{
			// Seeded loss/duplication across full runs: the room completes or fails consistently, never mixed.
			// mode 0 complete (normal / story rosters), 1 host Cancel precommit, 2 load failure without rollback (END),
			// 3 load failure with a verified rollback. Every run must reach its outcome on EVERY PC.
			const char* names[] = {"complete", "cancel", "end", "rollback"};
			for (int mode = 0; mode < 4; ++mode)
			{
				int reached = 0, consistent = 0, runs = 0;
				for (unsigned seed = 1; seed <= 200; ++seed)
				{
					++runs;
					const lossy_result x = lossy(seed, mode);
					reached += x.reached;
					consistent += x.consistent;
					if (!x.reached || !x.consistent) std::printf("  lossy %s seed %u: host %s guests %s\n", names[mode], seed, x.host.c_str(), x.guests.c_str());
				}
				std::printf("  lossy %s: %d of %d reached, %d consistent\n", names[mode], reached, runs, consistent);
				check(reached == runs && consistent == runs, std::string("loss (fix 10): 200 seeded runs, a third of all messages lost (CANCEL/END/FREEZE too), some duplicated - every run reaches '") + names[mode] + "' on every PC, none mixed");
			}
		}
		{
			// The host answers a message about a finished op with its outcome (the guest missed the CANCEL).
			room r(3);
			r.propose();
			r.drop = [](int from, int to, const std::vector<std::uint8_t>& b) { return from == 0 && to == 1 && b[2] == std::uint8_t(T::cancel); };
			r.G(1).vote(true, r.t);
			r.pump();
			r.h.cancel(r.t);
			r.pump();
			check(r.G(1).now() == P::voting && r.G(2).now() == P::cancelled, "replay: guest 1 missed the CANCEL");
			r.drop = nullptr;
			auto v = header(T::vote, 1); v.answer = 1;
			r.deliver(1, 0, enc(v)); // its earlier answer, resent
			r.pump();
			check(r.G(1).now() == P::cancelled && r.G(1).outcome() == R::reason::host_cancel, "replay: the host answers the finished op with its CANCEL; the guest settles");
			r.propose();
			check(r.G(1).now() == P::voting && r.G(1).op() == 2, "replay: the next proposal opens normally");
		}
		{
			// A newer PROPOSE supersedes a precommit op whose CANCEL was lost.
			room r(2);
			r.to(P::distributing);
			r.drop = [](int from, int, const std::vector<std::uint8_t>& b) { return from == 0 && b[2] == std::uint8_t(T::cancel); };
			r.h.cancel(r.t);
			r.pump();
			check(r.G(1).now() == P::preparing, "supersede: the guest missed the CANCEL (still frozen in preparation)");
			r.drop = nullptr;
			r.propose();
			check(r.G(1).now() == P::voting && r.G(1).op() == 2 && !r.G(1).world_replaced(), "supersede: the next PROPOSE opens op 2 (the old one ended precommit)");
		}
		{
			// A newer PROPOSE supersedes a precommit op whose CANCEL was lost; a committed guest refuses one.
			room r(2);
			r.to(P::distributing);
			r.drop = [](int from, int, const std::vector<std::uint8_t>& b) { return from == 0 && b[2] == std::uint8_t(T::cancel); };
			r.stage_all();
			check(r.h.now() == P::committed && r.G(1).now() == P::committed, "one at a time: committed");
			auto p = header(T::propose, 9); p.members = {{K(1), 0}, {K(2), 1}}; p.image_len = 5; p.image = D(1); p.source_len = 5; p.source = D(2);
			auto b = enc(p);
			check(r.G(1).receive(K(1), b.data(), b.size(), r.t) == F::busy && r.G(1).now() == P::committed, "one at a time: a committed guest refuses a new PROPOSE");
		}
		{
			room r(3);
			r.propose();
			bool cut = false;
			for (int i = 0; i < 16; ++i)
			{
				std::vector<std::uint8_t> junk{16, 1, std::uint8_t(2 + i)}; // 16 different malformed messages
				r.h.receive(K(2), junk.data(), junk.size(), r.t + std::uint32_t(i) * 100);
				cut = cut || r.h.now() == P::cancelled;
			}
			r.pump();
			check(cut && r.h.outcome() == R::reason::protocol && r.h.outcome_slot() == 1 && r.guests_are(P::cancelled), "protocol: 16 different refusals from one member within 10 s cancel the open operation");
		}
	}

	void commit_failures()
	{
		{
			room r(3);
			r.to(P::committed);
			r.h.host_loaded(r.h.generation(), true, 77, sample_roster(), r.t);
			r.G(1).loaded(r.G(1).generation(), true, 77, sample_roster(), r.t);
			r.G(2).loaded(r.G(2).generation(), false, 0, sample_roster(), r.t);
			r.pump();
			check(r.h.now() == P::rolling_back && r.h.outcome() == R::reason::load_failed && r.h.outcome_slot() == 2 && r.h.op() == 2 && r.h.generation() == 2,
			      "partial commit: one load fails - freeze, then a rollback operation (op 2, generation 2), no vote");
			check(r.G(1).now() == P::rolling_back && r.G(2).now() == P::rolling_back && r.G(1).op() == 2, "partial commit: every guest (also the failed one) restores its own verified capture");
			check(!r.G(1).may_resume_input(), "partial commit: the loaded guest did not resume alone");
			r.load_all(sample_roster(), -1, 55);
			check(r.h.now() == P::resuming && r.guests_are(P::seated) == false && r.G(1).now() == P::resuming, "rollback: every PC loaded its capture: RESUME (no re-pick: original seats)");
			r.h.first_frame(r.t);
			for (int s = 1; s < 3; ++s) r.G(s).first_frame(r.t);
			check(r.h.now() == P::playing && r.h.rolled_back() && r.h.outcome() == R::reason::load_failed && r.G(1).now() == P::playing, "rollback: the party plays the original campaign again; the outcome names the failure");
		}
		{
			room r(3);
			r.to(P::distributing);
			r.ready();
			const auto p = sample_proposal();
			r.G(1).staged(true, p.image_len, p.image, D(0x77), r.t);
			r.G(2).staged(true, p.image_len, p.image, D(0x76), r.t);
			r.pump();
			check(r.h.now() == P::committed && !r.h.rollback_available(), "rollback: differing captures are not a verified rollback");
			r.G(2).loaded(r.G(2).generation(), false, 0, sample_roster(), r.t);
			r.pump();
			check(r.h.now() == P::ended && r.h.outcome() == R::reason::load_failed && r.guests_are(P::ended) && r.G(1).world_replaced(), "partial commit without a verified rollback: the session ends for everyone");
		}
		{
			room r(3);
			r.to(P::preparing);
			r.h.host_ready(true, R::digest{}, r.t);
			r.pump();
			r.stage_all(R::digest{});
			check(r.h.now() == P::committed && !r.h.rollback_available(), "rollback: no capture anywhere -> not available");
			r.leave(1);
			check(r.h.now() == P::ended && r.h.outcome() == R::reason::member_left && r.G(2).now() == P::ended, "partial commit: a departure without rollback ends the session");
		}
		{
			room r(3);
			r.to(P::committed);
			r.h.host_loaded(r.h.generation(), true, 77, sample_roster(), r.t);
			r.G(1).loaded(r.G(1).generation(), true, 77, sample_roster(), r.t);
			r.G(2).loaded(r.G(2).generation(), true, 78, sample_roster(), r.t);
			r.pump();
			check(r.h.now() == P::rolling_back && r.h.outcome() == R::reason::check_mismatch && r.h.outcome_slot() == 2, "check: a different loaded-state check freezes (then rollback)");
			r.h.host_loaded(r.h.generation(), true, 55, sample_roster(), r.t);
			r.G(1).loaded(r.G(1).generation(), true, 55, sample_roster(), r.t);
			r.G(2).loaded(r.G(2).generation(), false, 0, sample_roster(), r.t);
			r.pump();
			check(r.h.now() == P::ended && r.h.outcome() == R::reason::rollback_failed && r.guests_are(P::ended), "rollback: a failure during the rollback ends the session");
		}
		{
			// A guest that staged and missed COMMIT and FREEZE still follows the rollback (its world never changed).
			room r(3);
			r.to(P::distributing);
			r.drop = [](int from, int to, const std::vector<std::uint8_t>& b) { return from == 0 && to == 2 && (b[2] == std::uint8_t(T::commit) || b[2] == std::uint8_t(T::freeze)); };
			r.stage_all();
			check(r.G(2).now() == P::staged && r.h.now() == P::committed, "missed: guest 2 never saw COMMIT");
			r.G(1).loaded(r.G(1).generation(), false, 0, sample_roster(), r.t);
			r.pump();
			check(r.h.now() == P::rolling_back && r.G(2).now() == P::rolling_back && !r.G(2).world_replaced(), "missed: guest 2 takes the ROLLBACK straight from staged");
		}
		{
			// A guest whose capture differs from the ROLLBACK's refuses it; the host ends when it hears.
			room r(2);
			r.to(P::committed);
			auto rb = header(T::rollback, 2); rb.previous_op = 1; rb.gen = 2; rb.rollback = D(0x99);
			auto fz = header(T::freeze, 1); fz.gen = 1; fz.because = R::reason::load_failed; fz.slot = 0;
			auto b = enc(fz);
			r.G(1).receive(K(1), b.data(), b.size(), r.t);
			b = enc(rb);
			check(r.G(1).receive(K(1), b.data(), b.size(), r.t) == F::digest && r.G(1).now() == P::frozen, "rollback: a guest refuses a rollback digest it does not hold");
			auto out = r.G(1).take();
			R::message m;
			check(!out.empty() && R::parse(out.back().bytes.data(), out.back().bytes.size(), m) == F::none && m.t == T::fail && m.because == R::reason::rollback_failed, "rollback: and says so (FAIL rollback_failed)");
		}
		{
			room r(2);
			r.to(P::committed);
			r.G(1).loaded(r.G(1).generation(), true, 77, sample_roster(), r.t);
			r.pump();
			auto c = header(T::commit, 1); c.gen = 1; auto b = enc(c);
			check(r.G(1).receive(K(1), b.data(), b.size(), r.t) == F::repeat, "commit: the host's resent COMMIT is a repeat after loading");
			c.gen = 5; b = enc(c);
			check(r.G(1).receive(K(1), b.data(), b.size(), r.t) == F::out_of_phase && r.G(1).generation() == 1, "commit: a conflicting generation changes nothing");
			auto res = header(T::resume, 1); res.gen = 1; b = enc(res);
			check(r.G(1).receive(K(1), b.data(), b.size(), r.t) == F::out_of_phase && !r.G(1).may_resume_input(), "barrier: a RESUME before re-pick and seating is refused");
		}
	}

	void repick_tests()
	{
		{
			room r(3);
			r.to(P::repick);
			check(r.G(1).pick(10, r.t) == F::none, "re-pick: guest 1 picks hero 10");
			r.pump();
			check(r.G(2).pick(10, r.t) == F::none, "re-pick: guest 2 asks for the same hero");
			r.pump();
			check(r.h.picks().assign[1] == 10 && r.h.picks().assign[2] == R::hero_open && r.G(2).picks().assign[1] == 10, "re-pick: the first claim wins; the second stays open");
			check(r.h.host_pick(10, r.t) == F::taken, "re-pick: the host cannot take a held hero either");
			check(r.G(2).pick(99, r.t) == F::hero, "re-pick: a hero outside the save's roster is refused locally");
			r.G(1).pick(11, r.t);
			r.pump();
			check(r.h.picks().assign[1] == 11, "re-pick: a member can change to a free hero before the final assignment");
			r.G(2).pick(10, r.t);
			r.h.host_pick(R::hero_watch, r.t);
			r.pump();
			check(r.h.now() == P::seating && r.h.picks().assign[0] == R::hero_watch && r.h.picks().assign[2] == 10, "re-pick: the released hero can be taken; the host may watch");
		}
		{
			// Roster smaller than the party: the extra member watches.
			room r(4);
			R::roster_info two; two.heroes = {20, 21};
			r.to(P::committed);
			r.load_all(two);
			r.h.host_pick(20, r.t);
			r.G(1).pick(21, r.t);
			r.pump();
			check(r.h.now() == P::seating && r.h.picks().assign[2] == R::hero_watch && r.h.picks().assign[3] == R::hero_watch, "re-pick: with two heroes for four players, the rest watch");
		}
		{
			// Story segment, the saved owner present: it takes the story hero at once, everyone else watches.
			room r(3);
			r.to(P::committed);
			r.load_all(sample_roster(12, K(2)));
			check(r.h.now() == P::seating && r.h.picks().assign[1] == 12 && r.h.picks().assign[0] == R::hero_watch && r.G(2).picks().assign[2] == R::hero_watch,
			      "story: the saved owner (present) plays the story hero; the others watch");
		}
		{
			// Story owner departed (not a member): the host or a remaining player may take control; first claim wins.
			room r(3);
			r.to(P::committed);
			r.load_all(sample_roster(12, K(7)));
			check(r.h.now() == P::repick && r.G(1).picks().owner == R::slot_none, "story: the saved owner departed - the story hero is open");
			check(r.G(1).pick(11, r.t) == F::hero, "story: only the story hero (or watch) can be picked");
			check(r.G(2).pick(12, r.t) == F::none, "story: a remaining player claims it");
			r.pump();
			check(r.h.now() == P::seating && r.h.picks().assign[2] == 12 && r.h.picks().assign[0] == R::hero_watch && r.h.picks().assign[1] == R::hero_watch, "story: first claim wins; everyone else watches");
		}
		{
			room r(3);
			r.to(P::committed);
			r.load_all(sample_roster(12));
			r.step(R::repick_ms + 100);
			check(r.h.now() == P::seating && r.h.picks().assign[0] == 12, "story: nobody claimed it in 90 s - the host takes the story hero");
		}
		{
			// Owner present, then departs during the re-pick: the host may take it.
			room r(3);
			r.to(P::committed);
			r.load_all(sample_roster(12, K(3)));
			check(r.h.now() == P::seating, "story: owner present -> immediate final assignment");
			room q(3);
			q.to(P::committed);
			q.G(2).pick(R::hero_watch, q.t);
			q.load_all(sample_roster(12, R::key{}));
			check(q.h.now() == P::repick, "story: owner unknown -> open claims");
			q.leave(2);
			check(q.h.host_pick(12, q.t) == F::none, "story: after a departure the host takes control of the story hero");
			q.pump();
			check(q.h.now() == P::seating && q.h.picks().assign[0] == 12 && q.G(1).picks().assign[0] == 12, "story: the story continues with the host");
		}
		{
			// A saved owner who passes (watch) frees the story hero for a claim.
			R::repick_state s;
			std::array<bool, 4> here{true, true, true, false};
			s.start(3, here, {1, 2, 3}, 2, 1);
			check(!s.complete() || s.assign[1] == 2, "story state: owner holds the story hero at start");
			check(s.pick(1, R::hero_watch) == F::none && !s.complete() && s.pick(2, 2) == F::none && s.complete(), "story state: the owner may pass it on; another member claims it");
			check(s.pick(3, 1) == F::unauthorized, "story state: a slot outside the party cannot pick");
			R::repick_state z;
			z.start(2, std::array<bool, 4>{true, true, false, false}, {5}, R::story_none, R::slot_none);
			z.pick(1, 5);
			check(z.complete(), "state: complete when no free hero remains for the open slot");
			z.finish();
			check(z.assign[0] == R::hero_watch && z.assign[1] == 5, "state: the hero-less member watches");
		}
		{
			// A guest that loaded a different roster: the worlds differ - FAIL, the host ends.
			room r(3);
			r.to(P::committed);
			R::roster_info other = sample_roster(); other.heroes.pop_back();
			r.h.host_loaded(r.h.generation(), true, 77, sample_roster(), r.t);
			r.G(1).loaded(r.G(1).generation(), true, 77, sample_roster(), r.t);
			r.G(2).loaded(r.G(2).generation(), true, 77, other, r.t);
			r.pump();
			check(r.h.now() == P::repick && r.h.departed(2) && r.G(2).now() == P::ended && r.G(2).outcome() == R::reason::check_mismatch && r.G(1).now() == P::repick,
			      "re-pick (fix 6): a guest with a different loaded roster leaves (ended on that PC); the others continue the same world");
		}
		{
			room r(2);
			r.to(P::committed);
			R::roster_info bad; bad.heroes = {1, 1};
			r.h.host_loaded(r.h.generation(), true, 77, bad, r.t);
			r.pump();
			check(r.h.now() == P::rolling_back && r.h.outcome() == R::reason::load_failed, "re-pick: an invalid roster from the host's own load is a load failure");
		}
	}


	void review_probes()
	{
		{
			// P1 (fix 3): story owner present -> immediate final ASSIGN; the REPICK is lost to guest 1.
			room r(3);
			r.to(P::committed);
			r.drop = [](int from, int to, const std::vector<std::uint8_t>& b) { return from == 0 && to == 1 && b[2] == std::uint8_t(T::repick); };
			r.load_all(sample_roster(12, K(3)));
			r.drop = nullptr;
			for (int i = 0; i < 100; ++i)
			{
				for (int s = 1; s < 3; ++s) if (r.G(s).now() == P::seating) r.G(s).seated(r.t);
				if (r.h.now() == P::seating) r.h.host_seated(r.t);
				r.step(100);
			}
			check(r.h.now() == P::resuming && r.guests_are(P::resuming), "P1 (fix 3): a lost REPICK is resent with the final ASSIGN; everyone seats and resumes");
		}
		{
			// P2 (fix 4): a withdrawal sent while queued arrives after the first safe tick.
			room r(3);
			r.to(P::queued);
			std::vector<std::uint8_t> no;
			r.G(1).vote(false, r.t);
			for (auto& o : r.G(1).take()) no = o.bytes;
			r.h.safety(true, R::unsafe::none, 900);
			r.h.tick(r.t);
			r.pump();
			check(r.h.now() == P::preparing, "P2: the host prepared before the No arrived");
			r.h.receive(K(2), no.data(), no.size(), r.t);
			r.pump();
			check(r.h.now() == P::cancelled && r.h.outcome() == R::reason::declined && r.h.outcome_slot() == 1 && r.G(2).now() == P::cancelled,
			      "P2 (fix 4): a late No in preparing cancels at once (declined), nobody waits for a stall timeout");
			room q(3);
			q.to(P::distributing);
			auto v = header(T::vote, 1); v.answer = 2; auto b = enc(v);
			q.h.receive(K(3), b.data(), b.size(), q.t);
			check(q.h.now() == P::cancelled && q.h.outcome() == R::reason::declined && q.h.outcome_slot() == 2, "P2 (fix 4): a No while distributing cancels too");
		}
		{
			// P3 (fix 2): a staged guest loses the one CANCEL.
			room r(3);
			r.to(P::preparing); r.ready();
			const auto p = sample_proposal();
			r.G(1).staged(true, p.image_len, p.image, D(0x77), r.t); r.pump();
			r.drop = [](int from, int to, const std::vector<std::uint8_t>& b) { return from == 0 && to == 1 && b[2] == std::uint8_t(T::cancel); };
			r.h.cancel(r.t); r.pump();
			r.drop = nullptr;
			r.step(2500);
			check(r.G(1).now() == P::cancelled && r.G(1).outcome() == R::reason::host_cancel && !r.G(1).world_replaced(), "P3 (fix 2): a staged guest resends once a second and learns the lost CANCEL within 2.5 s");
		}
		{
			// P3b (fix 2): a loaded guest loses the one END.
			room r(3);
			r.to(P::preparing);
			r.h.host_ready(true, R::digest{}, r.t); r.pump();
			r.stage_all(R::digest{});
			r.G(1).loaded(r.G(1).generation(), true, 77, sample_roster(), r.t); r.pump();
			r.drop = [](int from, int to, const std::vector<std::uint8_t>& b) { return from == 0 && to == 1 && (b[2] == std::uint8_t(T::end) || b[2] == std::uint8_t(T::freeze)); };
			r.G(2).loaded(r.G(2).generation(), false, 0, sample_roster(), r.t); r.pump();
			check(r.h.now() == P::ended && r.G(1).now() == P::loaded, "P3b: the host ended; guest 1 missed FREEZE and END");
			r.drop = nullptr;
			r.step(2500);
			check(r.G(1).now() == P::ended && r.G(1).outcome() == R::reason::load_failed && !r.G(1).may_resume_input(), "P3b (fix 2): it learns the END from the host's reply to its resend");
		}
		{
			// P3c (fix 2): the host goes completely silent after a guest staged - the guest ends itself, never resumes.
			room r(3);
			r.to(P::distributing);
			r.stage_all(D(0x77), 2);
			r.drop = [](int from, int, const std::vector<std::uint8_t>&) { return from == 0; };
			r.step(R::bound_silence_ms - 1000);
			check(r.G(1).now() == P::staged, "P3c: still staged just before the long-silence fallback");
			r.step(2000);
			check(r.G(1).now() == P::ended && r.G(1).outcome() == R::reason::host_silent && !r.G(1).world_replaced() && !r.G(1).may_resume_input(),
			      "P3c (fix 2): after the long-silence fallback a bound guest ends (world unchanged, never resumes)");
		}
		{
			// P4 (fix 7): every member picks watch outside a story segment.
			room r(2);
			r.to(P::committed);
			r.load_all();
			r.h.host_pick(R::hero_watch, r.t); r.G(1).pick(R::hero_watch, r.t); r.pump();
			check(r.h.now() == P::seating && r.h.picks().assign[0] == 10 && r.G(1).picks().assign[0] == 10, "P4 (fix 7): nobody may leave the party without a hero - the host gets the first free hero");
		}
		{
			// P5 (fix 8): a delayed older PICK must not override the newer choice.
			room r(3);
			r.to(P::committed);
			r.load_all();
			std::vector<std::vector<std::uint8_t>> late;
			r.G(1).pick(11, r.t); for (auto& o : r.G(1).take()) late.push_back(o.bytes);
			r.G(1).pick(12, r.t); r.pump();
			if (late.empty()) late.emplace_back(); // the guest was not re-picking (a broken core): the check below fails
			const auto res = r.h.receive(K(2), late[0].data(), late[0].size(), r.t); r.pump();
			check(res == F::stale && r.h.picks().assign[1] == 12, "P5 (fix 8): a reordered older PICK is stale; the newer choice stands");
		}
		{
			// P6 (fix 6): one guest's FAIL after everyone loaded the same world is that guest's departure.
			room r(3);
			r.to(P::repick);
			R::message m = header(T::fail, r.h.op()); m.at = P::repick; m.because = R::reason::seat_timeout;
			auto b = R::encode(m);
			r.h.receive(K(3), b.data(), b.size(), r.t); r.pump();
			check(r.h.now() == P::repick && r.h.departed(2) && r.G(1).now() == P::repick, "P6 (fix 6): a FAIL during re-pick removes only that member");
			r.h.host_pick(10, r.t); r.G(1).pick(11, r.t); r.pump();
			r.seat_all(2);
			check(r.h.now() == P::resuming && r.G(1).now() == P::resuming, "P6: the rest seat and resume without it");
			room q(3);
			q.to(P::resuming);
			m = header(T::fail, q.h.op()); m.at = P::resuming; m.because = R::reason::guest_abort; b = R::encode(m);
			q.h.receive(K(2), b.data(), b.size(), q.t);
			check(q.h.now() == P::resuming && q.h.departed(1), "P6: a FAIL while resuming is a departure too");
		}
		{
			// P7 (fix 5): the guest's refusal of a rollback reaches the host under the rollback's op.
			room r(3);
			r.to(P::preparing); r.ready();
			const auto p = sample_proposal();
			r.G(1).staged(true, p.image_len, p.image, D(0x77), r.t);
			r.G(2).staged(true, p.image_len, p.image, D(0x77), r.t);
			r.pump();
			r.drop = [](int from, int to, const std::vector<std::uint8_t>& x) { return from == 0 && to == 1 && x[2] == std::uint8_t(T::rollback); };
			r.h.host_loaded(r.h.generation(), false, 0, sample_roster(), r.t);
			r.pump();
			check(r.h.now() == P::rolling_back, "P7: the host's own load failed -> rolling back");
			R::message m = header(T::rollback, r.h.op()); m.previous_op = r.h.op() - 1; m.gen = r.h.generation(); m.rollback = D(0x55);
			auto b = R::encode(m);
			r.G(1).receive(K(1), b.data(), b.size(), r.t);
			r.drop = nullptr;
			r.pump();
			check(r.h.now() == P::ended && r.h.outcome() == R::reason::rollback_failed && r.h.outcome_slot() == 1 && r.guests_are(P::ended),
			      "P7 (fix 5): a guest refusing the rollback digest is heard at once - the host ends for everyone (no 120 s wait)");
		}
		{
			// Fix 9: a load completion carries the generation it started under; a late commit-load completion during
			// the rollback is not the rollback load.
			room r(3);
			r.to(P::committed);
			r.G(1).loaded(r.G(1).generation(), false, 0, sample_roster(), r.t);
			r.pump();
			check(r.h.now() == P::rolling_back && r.h.generation() == 2 && r.G(2).generation() == 2, "fix 9: rolling back under generation 2");
			r.h.host_loaded(1, true, 77, sample_roster(), r.t);
			r.G(2).loaded(1, true, 77, sample_roster(), r.t);
			r.G(1).loaded(2, true, 55, sample_roster(), r.t);
			r.pump();
			check(r.h.now() == P::rolling_back && r.G(2).now() == P::rolling_back, "fix 9: the generation-1 completions on the host and guest 2 are ignored");
			r.h.host_loaded(2, true, 55, sample_roster(), r.t);
			r.G(2).loaded(2, true, 55, sample_roster(), r.t);
			r.pump();
			check(r.h.now() == P::resuming && r.guests_are(P::resuming), "fix 9: the generation-2 loads complete the rollback");
		}
	}


	// Re-review of f9d2436 (probe2.cpp Q1-Q4).
	std::vector<R::message> sent_to(std::vector<R::outgoing> out, int slot)
	{
		std::vector<R::message> v;
		for (auto& o : out)
			if (o.slot == R::slot_none || o.slot == slot) { R::message m; if (R::parse(o.bytes.data(), o.bytes.size(), m) == F::none) v.push_back(m); }
		return v;
	}
	void rereview_probes()
	{
		{
			// Q1: a guest stages before the host's own host_ready; its resends must not trip the refusal gate.
			room r(2);
			r.propose(); r.vote_all();
			const auto p = sample_proposal();
			r.G(1).staged(true, p.image_len, p.image, D(0x77), r.t); r.pump();
			r.step(20000);
			check(r.h.now() == P::preparing && r.G(1).now() == P::staged, "Q1: an early STAGED (host still preparing) is not a protocol fault - 20 s later still preparing");
			r.h.host_ready(true, D(0x77), r.t); r.pump();
			check(r.h.now() == P::committed && r.G(1).now() == P::committed && r.h.rollback_available(), "Q1: the early STAGED was recorded - host_ready commits at once");
			room q(3);
			q.propose();
			std::vector<std::uint8_t> junk{16, 1, 2};
			for (int i = 0; i < 40; ++i) q.h.receive(K(2), junk.data(), junk.size(), q.t + std::uint32_t(i) * 100);
			check(q.h.now() == P::voting, "Q1: one identical malformed message resent 40 times in 4 s does not cancel (resends are not new refusals)");
		}
		{
			// Q2: a guest the host departs at the seat deadline is told (END seat_timeout), never resumes, holds no hero.
			room r(3);
			r.to(P::committed);
			r.load_all();
			r.h.host_pick(10, r.t); r.G(1).pick(11, r.t); r.G(2).pick(12, r.t); r.pump();
			r.h.host_seated(r.t); r.G(1).seated(r.t); r.pump();
			std::vector<R::outgoing> seen;
			auto keep = [&](int from, int to, const std::vector<std::uint8_t>& b) { if (from == 0 && to == 2 && r.h.departed(2)) seen.push_back({std::uint8_t(to), b}); return false; };
			r.drop = keep;
			r.step(R::seat_ms + 200);
			check(r.h.departed(2) && r.h.now() == P::resuming && r.G(1).now() == P::resuming, "Q2: guest 2 departed at the seat deadline; the rest resume");
			bool only_end = !seen.empty();
			bool had_end = false;
			for (auto& m : sent_to(seen, 2)) { only_end = only_end && m.t == T::end; had_end = had_end || (m.t == T::end && m.because == R::reason::seat_timeout && m.slot == 2); }
			check(had_end && only_end, "Q2: the departed guest gets a targeted END (seat_timeout) and no RESUME or other broadcast decision");
			check(r.G(2).now() == P::ended && r.G(2).outcome() == R::reason::seat_timeout && !r.G(2).may_resume_input(), "Q2: it ends and never produces input");
			check(r.h.picks().assign[2] == R::hero_watch, "Q2: the departed slot holds no hero in the host's final row");
			r.drop = nullptr;
			r.G(2).seated(r.t); r.pump(); r.step(2000);
			check(!r.G(2).may_resume_input(), "Q2: a late SEATED changes nothing");
		}
		{
			// Q2 (immediate): with everything guest 2 sends lost, the host's own targeted END still reaches it at the
			// departure - it is told, not merely answered when it speaks.
			room r(3);
			r.to(P::committed);
			r.load_all();
			r.h.host_pick(10, r.t); r.G(1).pick(11, r.t); r.G(2).pick(12, r.t); r.pump();
			r.h.host_seated(r.t); r.G(1).seated(r.t); r.pump();
			r.drop = [](int from, int, const std::vector<std::uint8_t>&) { return from == 2; };
			r.step(R::seat_ms + 200);
			check(r.h.departed(2) && r.G(2).now() == P::ended && r.G(2).outcome() == R::reason::seat_timeout, "Q2: the departure itself sends the END (guest 2 mute)");
		}
		{
			// Q2b: the END is lost; the departed guest seats late and its resend is answered with the END.
			room r(3);
			r.to(P::committed);
			r.load_all();
			r.h.host_pick(10, r.t); r.G(1).pick(11, r.t); r.G(2).pick(12, r.t); r.pump();
			r.h.host_seated(r.t); r.G(1).seated(r.t); r.pump();
			r.drop = [](int from, int to, const std::vector<std::uint8_t>& b) { return from == 0 && to == 2 && b[2] == std::uint8_t(T::end); };
			r.step(R::seat_ms + 200);
			r.drop = nullptr;
			check(r.G(2).now() == P::seating, "Q2b: guest 2 missed its END");
			r.G(2).seated(r.t); r.pump(); r.step(2500);
			check(r.G(2).now() == P::ended && !r.G(2).may_resume_input(), "Q2b: its resent SEATED is answered with the END; it never resumes");
		}
		{
			// Q3: a guest departed for its FAIL during re-pick is told (END member_left) and never resumes as a watcher.
			room r(3);
			r.to(P::repick);
			R::message m = header(T::fail, r.h.op()); m.at = P::repick; m.because = R::reason::load_failed;
			auto b = R::encode(m);
			r.deliver(2, 0, b); r.pump();
			r.h.host_pick(10, r.t); r.G(1).pick(11, r.t); r.pump();
			r.h.host_seated(r.t); r.G(1).seated(r.t); r.G(2).seated(r.t); r.pump(); r.step(2000);
			check(r.h.departed(2) && r.G(2).now() == P::ended && r.G(2).outcome() == R::reason::member_left && !r.G(2).may_resume_input(),
			      "Q3: a guest departed for its FAIL gets END (member_left) and never resumes");
			check(r.h.now() == P::resuming && r.G(1).now() == P::resuming && r.h.picks().assign[2] == R::hero_watch && r.G(1).picks().assign[2] == R::hero_watch,
			      "Q3: the rest resume; the departed slot is watch in the final row");
		}
		{
			// Q4 (contract): the integration drains received messages BEFORE tick(); after a 151 s blocked load the
			// host's queued resend arrives first and the guest does not end itself.
			room r(2);
			r.to(P::committed);
			auto c = header(T::commit, r.h.op()); c.gen = r.h.generation(); auto b = enc(c);
			r.G(1).receive(K(1), b.data(), b.size(), r.t + 151000);
			r.G(1).tick(r.t + 151000);
			check(r.G(1).now() == P::committed, "Q4: received first, then tick: a long blocked load does not end the guest");
		}
		{
			// Nit: the pick sequence is capped, never wraps to the refused 0.
			room r(2);
			r.to(P::committed);
			r.load_all();
			F last = F::none;
			int accepted = 0;
			for (int i = 0; i < 70000; ++i) { last = r.G(1).pick(std::uint16_t(10 + (i & 1)), r.t); if (last == F::none) ++accepted; r.G(1).take(); }
			check(accepted == 0xffff && last == F::closed, "nit: after 65535 picks a guest's pick is refused locally (no wrap to sequence 0)");
		}
	}

	void rule_and_framing()
	{
		check(R::rule_id == 63 && R::rule_revision == 1 && R::rule_effective == std::array<std::uint32_t, 4>{1u, 1u, 16u, 0u}, "rule 63 OnlineRestore revision 1 {1, 1, 16, 0}");
		const std::uint8_t kind16[] = {16, 1, 1};
		const auto p = gns_transport_core::parse(kind16, sizeof kind16);
		check(!p.valid, "framing 4: a build without rule 63 parses link kind 16 as invalid (drops it); rule 25 unchanged");
		check(R::link_kind != online_party_core::kind_report && R::link_kind != online_party_core::kind_table, "framing: kind 16 does not collide with the party kinds 7/8");
	}
}

int main(int argc, char** argv)
{
	const unsigned rounds = argc > 1 ? unsigned(std::strtoul(argv[1], nullptr, 10)) : 200000u;
	parser_tests();
	fuzz(rounds);
	happy_paths();
	vote_permutations();
	vote_details();
	queue_tests();
	membership_every_phase();
	staging_failures();
	timeouts();
	generations_every_phase();
	host_loss_every_phase();
	delays_and_duplicates();
	commit_failures();
	repick_tests();
	review_probes();
	rereview_probes();
	rule_and_framing();
	std::printf("%d PASS / %d FAIL\n", passed, failed);
	return failed ? 1 : 0;
}
