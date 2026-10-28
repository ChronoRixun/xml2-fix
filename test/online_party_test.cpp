// Online Party presentation data (docs/online-party.md): the kind 7 member report and kind 8 host table byte formats,
// their validation under malformed/fuzzed input, the model's sender permissions, sequence/room replay checks, rate and
// member caps, index reuse, RTT provenance and the facts-only local phase. Pure: online_party_core.hpp only.
#include "online_party_core.hpp"
#include "gns_transport_core.hpp"

#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace
{
	int passed = 0, failed = 0;
	void check(bool ok, const std::string& what)
	{
		std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
		(ok ? passed : failed)++;
	}
	namespace P = online_party_core;
	P::key K(std::uint8_t n) { P::key k{}; k.fill(n); return k; }
	const char* const CODE = "K7QN2X9M";
	const char* const OTHER = "ABCD2345";
	P::report sample_report(std::uint32_t seq, P::phase ph = P::phase::in_lobby)
	{
		P::report r;
		std::memcpy(r.code, CODE, 8);
		r.seq = seq;
		r.now = ph;
		r.index = 1;
		r.token = "LC33";
		r.name = "Kitty";
		return r;
	}
	P::table sample_table(std::uint32_t seq, const P::key& host, const std::vector<P::table_row>& extra = std::vector<P::table_row>())
	{
		P::table t;
		std::memcpy(t.code, CODE, 8);
		t.seq = seq;
		t.generation = 3;
		P::table_row h;
		h.who = host;
		h.index = 0;
		h.now = P::phase::in_lobby;
		h.stage = P::build::agreed;
		t.rows.push_back(h);
		for (const auto& e : extra) t.rows.push_back(e);
		return t;
	}
	P::refusal report_to(P::model& m, const P::key& from, const P::report& r, std::uint32_t now)
	{
		const auto b = P::encode(r);
		return m.on_report(from, b.data(), b.size(), now);
	}
	P::refusal table_to(P::model& m, const P::key& from, int index, const P::table& t, std::uint32_t now)
	{
		const auto b = P::encode(t);
		return m.on_table(from, index, b.data(), b.size(), now);
	}
	const P::card* card_of(const std::vector<P::card>& cards, const P::key& k)
	{
		for (const auto& c : cards) if (c.who == k) return &c;
		return nullptr;
	}
	std::vector<P::card> cards(const P::model& m, std::uint32_t now, int self_index = 0)
	{
		return m.cards(P::phase::in_lobby, self_index, "Logan", false, false, "LC33", now);
	}
}

int main()
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	// ---- framing table ----
	check(std::uint8_t(gns_transport_core::kind::lost_ready) == 6 && P::kind_report == 7 && P::kind_table == 8, "framing: kinds 0-6 are the transport's, party takes 7 and 8");
	{
		const std::uint8_t seven[] = {7, 1, 2, 3}, eight[] = {8, 1};
		check(!gns_transport_core::parse(seven, sizeof(seven)).valid && !gns_transport_core::parse(eight, sizeof(eight)).valid,
		      "framing: the transport parser (and so every revision-4 peer) treats kinds 7/8 as invalid - dropped, never game input");
	}

	// ---- identity keys ----
	{
		P::key k{};
		check(P::key_from_identity("legends:0123456789abcdef", k) && k[0] == 0x01 && k[7] == 0xef, "key: legends:<16 hex> -> 8 bytes");
		check(!P::key_from_identity("legends:0123456789ABCDEF", k) && !P::key_from_identity("legends:0123", k) && !P::key_from_identity("other:0123456789abcdef", k),
		      "key: upper-case, short and foreign identities refused");
	}

	// ---- report round trip and limits ----
	{
		auto r = sample_report(9);
		r.token = std::string(32, 'T');
		r.name = std::string(31, 'N');
		r.rtt = {{K(2), 38, P::route::direct}, {K(3), 112, P::route::relay}, {K(4), P::rtt_none, P::route::unknown}};
		const auto b = P::encode(r);
		check(b.size() == 119 && b.size() <= P::report_max, "report: the largest one is 119 bytes (cap 128)");
		P::report back;
		check(P::parse(b.data(), b.size(), back) == P::refusal::none && back.seq == 9 && back.token == r.token && back.name == r.name && back.rtt.size() == 3 &&
		          back.rtt[1].ms == 112 && back.rtt[1].how == P::route::relay && back.index == 1,
		      "report: round trip keeps every field");
		check(P::encode(back) == b, "report: canonical (parse -> encode gives the same bytes)");
		const auto small = P::encode(P::report{[] { P::report x; std::memcpy(x.code, CODE, 8); x.seq = 1; return x; }()});
		check(small.size() == P::report_fixed, "report: the smallest one is 23 bytes");
		bool truncations = true;
		for (std::size_t n = 0; n < b.size(); ++n) truncations = truncations && P::parse(b.data(), n, back) != P::refusal::none;
		check(truncations, "report: every truncation is refused");
		auto longer = b;
		longer.push_back(0);
		check(P::parse(longer.data(), longer.size(), back) == P::refusal::size, "report: a trailing byte is refused");
		std::vector<std::uint8_t> huge(P::report_max + 1, 0);
		huge[0] = 7;
		check(P::parse(huge.data(), huge.size(), back) == P::refusal::size, "report: over 128 bytes is refused before any field is read");
	}
	{
		auto base = P::encode(sample_report(5));
		P::report out;
		auto mutate = [&](std::size_t at, std::uint8_t v) { auto c = base; c[at] = v; return P::parse(c.data(), c.size(), out); };
		check(mutate(0, 8) == P::refusal::kind, "report enum: wrong kind");
		check(mutate(1, 2) == P::refusal::wire && mutate(1, 0) == P::refusal::wire, "report enum: wire versions other than 1");
		check(mutate(2, '0') == P::refusal::code && mutate(9, 'o') == P::refusal::code, "report: room code outside the alphabet");
		{ auto c = base; std::memset(c.data() + 10, 0, 4); check(P::parse(c.data(), c.size(), out) == P::refusal::sequence, "report: sequence 0 refused"); }
		check(mutate(14, 9) == P::refusal::none && mutate(14, 10) == P::refusal::phase && mutate(14, 255) == P::refusal::phase, "report enum: phase 0..9 only");
		check(mutate(15, 50) == P::refusal::percent, "report: a percent outside receiving_campaign is refused");
		{ auto c = base; c[14] = 5; c[15] = 100; check(P::parse(c.data(), c.size(), out) == P::refusal::none, "report: receiving_campaign 100% accepted"); }
		{ auto c = base; c[14] = 5; c[15] = 101; check(P::parse(c.data(), c.size(), out) == P::refusal::percent, "report: 101% refused"); }
		check(mutate(16, 3) == P::refusal::none && mutate(16, 4) == P::refusal::index && mutate(16, 0xff) == P::refusal::none, "report enum: index 0..3 or unknown");
		check(mutate(17, 1) == P::refusal::none && mutate(17, 2) == P::refusal::flags && mutate(17, 0x80) == P::refusal::flags, "report: only flag bit 0 is defined");
		check(mutate(18, 33) == P::refusal::token, "report length: token over 32");
		check(mutate(19, ' ') == P::refusal::token && mutate(19, 0x7f) == P::refusal::token, "report encoding: token bytes 0x21..0x7e");
		check(mutate(23, 32) == P::refusal::name, "report length: name over 31");
		check(mutate(24, 0) == P::refusal::name && mutate(24, 0x1f) == P::refusal::name && mutate(24, 0xe9) == P::refusal::none, "report encoding: no control bytes in the name, high bytes kept");
		check(mutate(29, 4) == P::refusal::rtt, "report length: more than three RTT rows");
		check(mutate(base.size() - 1, 0xfe) == P::refusal::hero, "report: the reserved hero field must say unknown");
		auto r = sample_report(5);
		r.rtt = {{K(2), 61000, P::route::direct}};
		auto b = P::encode(r);
		check(P::parse(b.data(), b.size(), out) == P::refusal::rtt, "report: RTT over 60000 ms refused");
		r.rtt = {{K(2), 10, P::route::direct}};
		b = P::encode(r);
		b[30 + 10] = 3;
		check(P::parse(b.data(), b.size(), out) == P::refusal::rtt, "report enum: route 0..2 only");
		r.rtt = {{K(2), 10, P::route::direct}, {K(2), 11, P::route::relay}};
		b = P::encode(r);
		check(P::parse(b.data(), b.size(), out) == P::refusal::duplicate, "report: duplicated RTT member refused");
	}

	// ---- table round trip and limits ----
	{
		P::table_row a; a.who = K(2); a.index = 1; a.now = P::phase::ready; a.ms = 40; a.how = P::route::direct; a.stage = P::build::agreed;
		P::table_row b; b.who = K(3); b.index = 2;
		P::table_row c; c.who = K(4); c.index = P::index_unknown; c.now = P::phase::receiving_campaign; c.percent = 40;
		const auto t = sample_table(4, K(1), {a, b, c});
		const auto bytes = P::encode(t);
		check(bytes.size() == 77 && bytes.size() <= P::table_max, "table: four rows are 77 bytes (cap 80)");
		P::table back;
		check(P::parse(bytes.data(), bytes.size(), back) == P::refusal::none && back.rows.size() == 4 && back.rows[3].percent == 40 && back.generation == 3, "table: round trip");
		check(P::encode(back) == bytes, "table: canonical");
		bool truncations = true;
		for (std::size_t n = 0; n < bytes.size(); ++n) truncations = truncations && P::parse(bytes.data(), n, back) != P::refusal::none;
		check(truncations, "table: every truncation is refused");
		auto five = bytes;
		five[16] = 5;
		five.insert(five.end(), bytes.begin() + 17, bytes.begin() + 32);
		check(five.size() == 92 && P::parse(five.data(), five.size(), back) == P::refusal::size, "table cap: five rows do not fit (over 80 bytes)");
		auto zero = bytes;
		zero[16] = 0;
		check(P::parse(zero.data(), P::table_fixed + 15, back) == P::refusal::rows, "table: zero rows refused");
		auto dup = sample_table(4, K(1), {a, a});
		auto d = P::encode(dup);
		check(P::parse(d.data(), d.size(), back) == P::refusal::duplicate, "table: duplicated member refused");
		auto same_index = b; same_index.who = K(5); same_index.index = 1;
		d = P::encode(sample_table(4, K(1), {a, same_index}));
		check(P::parse(d.data(), d.size(), back) == P::refusal::duplicate, "table: two members claiming one native index refused");
		d = P::encode(sample_table(4, K(1), {a}));
		auto m = d; m[17 + 15 + 14] = 4;
		check(P::parse(m.data(), m.size(), back) == P::refusal::rows, "table enum: build 0..3 only");
		m = d; m[17 + 15 + 9] = 10;
		check(P::parse(m.data(), m.size(), back) == P::refusal::phase, "table enum: phase");
		m = d; m[17 + 15 + 8] = 7;
		check(P::parse(m.data(), m.size(), back) == P::refusal::index, "table enum: index");
	}

	// ---- fuzz: random and mutated buffers never crash, and anything accepted is canonical and in range ----
	{
		std::mt19937 rng(20261027);
		const auto report_seed = P::encode([] { auto r = sample_report(7); r.rtt = {{K(2), 5, P::route::relay}}; return r; }());
		P::table_row row; row.who = K(2); row.index = 1;
		const auto table_seed = P::encode(sample_table(7, K(1), {row}));
		unsigned accepted = 0, canonical = 0, in_range = 0;
		for (int i = 0; i < 200000; ++i)
		{
			const bool report = i & 1;
			std::vector<std::uint8_t> b = report ? report_seed : table_seed;
			const int edits = 1 + int(rng() % 4);
			for (int e = 0; e < edits; ++e)
			{
				switch (rng() % 4)
				{
				case 0: if (!b.empty()) b[rng() % b.size()] = std::uint8_t(rng()); break;
				case 1: b.push_back(std::uint8_t(rng())); break;
				case 2: if (!b.empty()) b.erase(b.begin() + long(rng() % b.size())); break;
				default: if (rng() % 8 == 0) { b.resize(rng() % 140); for (auto& x : b) x = std::uint8_t(rng()); } break;
				}
			}
			if (report)
			{
				P::report r;
				if (P::parse(b.data(), b.size(), r) != P::refusal::none) continue;
				++accepted;
				canonical += P::encode(r) == b;
				in_range += r.now < P::phase::count && r.token.size() <= 32 && r.name.size() <= 31 && r.rtt.size() <= 3 && r.seq != 0;
			}
			else
			{
				P::table t;
				if (P::parse(b.data(), b.size(), t) != P::refusal::none) continue;
				++accepted;
				canonical += P::encode(t) == b;
				in_range += !t.rows.empty() && t.rows.size() <= 4 && t.seq != 0;
			}
		}
		check(accepted > 0 && canonical == accepted && in_range == accepted, "fuzz: 200000 mutated buffers - every accepted one is canonical and in range (" + std::to_string(accepted) + " accepted)");
	}

	// ---- model: sender permissions, room, sequence ----
	const auto host = K(1), self = K(2), c3 = K(3), c4 = K(4), outsider = K(9);
	{
		P::model m;
		m.open(CODE, self, host);
		m.roster({host, self, c3});
		check(report_to(m, outsider, sample_report(1), 0) == P::refusal::unknown_member, "model: a report from a key outside the roster is refused");
		check(report_to(m, c3, sample_report(1), 0) == P::refusal::none, "model: a member's report is accepted");
		check(report_to(m, c3, sample_report(1), 10) == P::refusal::sequence, "model: replayed (same sequence) report refused");
		check(report_to(m, c3, sample_report(3), 2000) == P::refusal::none && report_to(m, c3, sample_report(2), 4000) == P::refusal::sequence,
		      "model: an out-of-order (older) report is refused after a newer one");
		auto r = sample_report(9);
		std::memcpy(r.code, OTHER, 8);
		check(report_to(m, c3, r, 6000) == P::refusal::room, "model: a report for another room (old generation) is refused");
		r = sample_report(10);
		r.rtt = {{outsider, 10, P::route::direct}};
		check(report_to(m, c3, r, 8000) == P::refusal::unknown_member, "model: RTT about a non-member refused");
		r.rtt = {{c3, 10, P::route::direct}};
		check(report_to(m, c3, r, 10000) == P::refusal::unknown_member, "model: RTT about the sender itself refused");
		r.rtt = {{self, 10, P::route::direct}, {host, 30, P::route::relay}};
		check(report_to(m, c3, r, 12000) == P::refusal::none, "model: RTT to this PC and to the host accepted");

		// Table permission.
		check(table_to(m, c3, 2, sample_table(1, c3), 0) == P::refusal::permission, "spoofed host: a guest's table is refused");
		check(table_to(m, host, 2, sample_table(1, host), 0) == P::refusal::permission, "spoofed host: the broker host's tunnel mapped to native index 2 is refused");
		P::table_row g; g.who = c3; g.index = 0;
		auto t = sample_table(1, host, {g});
		t.rows[0].index = P::index_unknown;
		check(table_to(m, host, 0, t, 0) == P::refusal::host_row, "spoofed host: a guest row claiming index 0 refused");
		t = sample_table(1, host);
		t.rows[0].index = 2;
		check(table_to(m, host, 0, t, 0) == P::refusal::host_row, "spoofed host: the host's own row claiming index 2 refused");
		t = sample_table(1, host);
		t.rows[0].who = c3;
		check(table_to(m, host, 0, t, 0) == P::refusal::host_row, "spoofed host: a table without the sender's own row refused");
		P::table_row o; o.who = outsider; o.index = 3;
		check(table_to(m, host, 0, sample_table(1, host, {o}), 0) == P::refusal::unknown_member, "table: a row naming a non-member refused");
		g.index = 1;
		check(table_to(m, host, -1, sample_table(2, host, {g}), 0) == P::refusal::none, "table: the host's table before any native mapping is accepted");
		check(table_to(m, host, 0, sample_table(2, host, {g}), 600) == P::refusal::sequence, "table: replayed sequence refused");
		check(m.refused(P::refusal::permission) == 2 && m.refused(P::refusal::host_row) == 3, "model: refusals are counted by reason");
		m.reconnected(c3);
		check(report_to(m, c3, sample_report(1), 30000) == P::refusal::none, "reconnect: a new connection (restarted peer) starts its sequence again");
		check(report_to(m, c3, sample_report(1), 31000) == P::refusal::sequence, "reconnect: and replay is refused again on it");

		// A new room forgets every sequence.
		m.open(OTHER, self, host);
		m.roster({host, self, c3});
		r = sample_report(1);
		std::memcpy(r.code, OTHER, 8);
		check(report_to(m, c3, r, 20000) == P::refusal::none && report_to(m, c3, sample_report(50), 20000) == P::refusal::room,
		      "generation: a new room starts with no sequence memory and refuses the old room's reports");
	}

	// ---- rate, byte and member caps; muting ----
	{
		P::model m;
		m.open(CODE, self, host);
		m.roster({host, self, c3});
		unsigned ok = 0, rate = 0;
		for (std::uint32_t i = 1; i <= 6; ++i) { const auto x = report_to(m, c3, sample_report(i), 1000); ok += x == P::refusal::none; rate += x == P::refusal::rate; }
		check(ok == 4 && rate == 2, "rate: a burst of 6 reports in one ms - 4 accepted, 2 refused");
		check(report_to(m, c3, sample_report(7), 1200) == P::refusal::rate && report_to(m, c3, sample_report(8), 1600) == P::refusal::none, "rate: refill at 2 reports per second");
		for (std::uint32_t i = 0; i < 20; ++i) report_to(m, c3, sample_report(100), 1600);
		check(report_to(m, c3, sample_report(200), 100000) == P::refusal::muted, "mute: after 16 refused messages the tunnel's party messages are ignored");
		check(report_to(m, host, sample_report(1), 100000) == P::refusal::none, "mute: only that tunnel");
		// Byte budget: tables of 73 bytes; 6 burst tables = 438 B pass the byte bucket, the table bucket stops them.
		P::model b;
		b.open(CODE, self, host);
		b.roster({host, self, c3, c4});
		P::table_row r3; r3.who = c3; r3.index = 1;
		P::table_row r4; r4.who = c4; r4.index = 2;
		P::table_row rs; rs.who = self; rs.index = 3;
		unsigned tables = 0;
		for (std::uint32_t i = 1; i <= 8; ++i) tables += table_to(b, host, 0, sample_table(i, host, {r3, r4, rs}), 5000) == P::refusal::none;
		check(tables == 6, "rate: a burst of 8 host tables - 6 accepted");
		P::receive_gate gate;
		unsigned bytes_ok = 0;
		for (int i = 0; i < 30; ++i) bytes_ok += gate.admit(P::kind_report, 128, 0) == P::refusal::none;
		check(bytes_ok == 4, "bytes: the report bucket stops first at full-size reports");
		P::receive_gate g2;
		g2.reports = online_room_core::token_bucket(1000, 1000);
		unsigned by = 0;
		for (int i = 0; i < 30; ++i) by += g2.admit(P::kind_report, 128, 0) == P::refusal::none;
		check(by == 16, "bytes: 2 KiB burst of 128-byte reports = 16");
		P::model five;
		five.open(CODE, self, host);
		five.roster({host, self, c3, c4, K(5), K(6)});
		check(five.cards(P::phase::in_lobby, -1, "", false, false, "LC33", 0).size() == 4, "member cap: a five/six-key roster still gives four cards");
		five.roster({host, self, host, c3});
		check(five.cards(P::phase::in_lobby, -1, "", false, false, "LC33", 0).size() == 3, "duplicated ids: a repeated roster key is one member");
	}

	// ---- cards: readiness, provenance, build, index reuse ----
	{
		P::model m;
		m.open(CODE, self, host); // this PC is a guest
		m.roster({host, self, c3});
		P::member_facts fh; fh.tunnel = fh.bound = true; fh.index = 0; fh.rtt = 38; fh.how = P::route::direct; fh.native_name = "Logan"; fh.ready_known = true;
		P::member_facts f3; f3.tunnel = true; f3.bound = true; f3.index = 2; f3.ready_known = true; f3.ready = false; f3.native_name = "Alex";
		m.observe(host, fh);
		m.observe(c3, f3);
		m.settle_indices();
		auto r = sample_report(1, P::phase::ready);
		check(report_to(m, c3, r, 100) == P::refusal::none, "spoofed readiness: the claim parses");
		auto cs = m.cards(P::phase::in_lobby, 1, "Kitty", false, true, "LC33", 200);
		const auto* x = card_of(cs, c3);
		check(x && x->now == P::phase::in_lobby && !x->ready, "spoofed readiness: a claimed ready without the native flag shows In lobby, not ready");
		f3.ready = true;
		m.observe(c3, f3);
		auto r2 = sample_report(2, P::phase::in_lobby);
		report_to(m, c3, r2, 1200);
		cs = m.cards(P::phase::in_lobby, 1, "Kitty", false, true, "LC33", 1300);
		x = card_of(cs, c3);
		check(x && x->now == P::phase::ready && x->ready, "readiness: the native flag wins over a stale in_lobby claim");
		check(cs.size() == 3 && cs[0].who == host && cs[1].self && cs[2].who == c3, "cards: ordered by native index (host 0, self 1, member 2)");
		x = card_of(cs, host);
		check(x && x->host && x->from == P::rtt_source::local && x->rtt == 38 && x->how == P::route::direct, "provenance: the host's card shows this PC's own RTT and route");
		x = card_of(cs, c3);
		check(x && x->from == P::rtt_source::none && x->rtt < 0, "provenance: nothing measured stays unknown (neutral), never 0");
		// The host's table gives c3's RTT to the host.
		P::table_row t3; t3.who = c3; t3.index = 2; t3.ms = 112; t3.how = P::route::relay;
		P::table_row ts; ts.who = self; ts.index = 1; ts.ms = 40;
		check(table_to(m, host, 0, sample_table(1, host, {t3, ts}), 1400) == P::refusal::none, "table: accepted from the host at index 0");
		cs = m.cards(P::phase::in_lobby, 1, "Kitty", false, true, "LC33", 1500);
		x = card_of(cs, c3);
		check(x && x->from == P::rtt_source::to_host && x->rtt == 112 && x->how == P::route::relay, "provenance: no local measurement -> the host's value, marked to host");
		f3.rtt = 55; f3.how = P::route::direct;
		m.observe(c3, f3);
		cs = m.cards(P::phase::in_lobby, 1, "Kitty", false, true, "LC33", 1500);
		x = card_of(cs, c3);
		check(x && x->from == P::rtt_source::local && x->rtt == 55, "provenance: a local measurement replaces the host's");
		// Build badges.
		check(x && x->stage == P::build::agreed, "build: an admitted bound member is agreement-observed");
		P::model pre;
		pre.open(CODE, host, host); // hosting
		pre.roster({host, c3, c4});
		P::member_facts p3; p3.tunnel = true;
		pre.observe(c3, p3);
		auto tok = sample_report(1, P::phase::checking_build);
		report_to(pre, c3, tok, 0);
		auto other = sample_report(1, P::phase::checking_build);
		other.token = "LC32";
		other.name = "Bad\x01Name";
		auto ob = P::encode(other);
		check(pre.on_report(c4, ob.data(), ob.size(), 0) == P::refusal::name, "pending: a control byte in the name drops the report");
		other.name = "Remy";
		report_to(pre, c4, other, 0);
		auto pc = pre.cards(P::phase::in_lobby, 0, "Logan", false, false, "LC33", 10);
		const auto* a3 = card_of(pc, c3);
		const auto* a4 = card_of(pc, c4);
		check(a3 && a3->stage == P::build::same && !a3->admitted && a3->now == P::phase::checking_build && a3->name == "Kitty", "build: a pending member's matching token -> SAME (preview, not admission)");
		check(a4 && a4->stage == P::build::other && a4->now == P::phase::checking_build && a4->name == "Remy", "build: another token -> OTHER (its claimed phase shown)");
		check(pc[0].self && pc[0].host && pc[0].index == 0, "cards: the host's own card first");
		check(pre.hosting() && !pc[1].ready_known, "pending: no native ready shown for a broker-only member");
		// Stale claims fall back to local facts after 15 s.
		pc = pre.cards(P::phase::in_lobby, 0, "Logan", false, false, "LC33", 20000);
		a3 = card_of(pc, c3);
		check(a3 && a3->stage == P::build::checking && a3->name.empty() && a3->now == P::phase::checking_build, "stale: a report older than 15 s no longer shows");
	}
	{
		// Index reuse: C (index 2) leaves, E joins at index 2.
		const auto e = K(5);
		P::model m;
		m.open(CODE, self, host);
		m.roster({host, self, c3});
		P::member_facts f3; f3.tunnel = f3.bound = true; f3.index = 2; f3.native_name = "Alex";
		m.observe(c3, f3);
		report_to(m, c3, sample_report(4, P::phase::playing), 0);
		P::table_row t3; t3.who = c3; t3.index = 2; t3.ms = 70;
		table_to(m, host, 0, sample_table(1, host, {t3}), 0);
		m.roster({host, self, e});
		P::member_facts fe; fe.tunnel = fe.bound = true; fe.index = 2; fe.native_name = "Remy";
		m.observe(e, fe);
		auto cs = cards(m, 100, 1);
		const auto* x = card_of(cs, e);
		check(!card_of(cs, c3) && x && x->name == "Remy" && x->now == P::phase::in_lobby && x->rtt < 0, "index reuse: the new member at index 2 shows none of the old one's name, phase or RTT");
		// A late host-table row for the old member is gone with the roster change; one naming e at index 3 is not used
		// because this PC maps e itself.
		P::table_row te; te.who = e; te.index = 3;
		table_to(m, host, 0, sample_table(2, host, {te}), 600);
		cs = cards(m, 700, 1);
		x = card_of(cs, e);
		check(x && x->index == 2, "index reuse: a host-reported index never overrides this PC's own mapping");
		// Two members mapped to one index locally (a stale read): neither keeps it.
		m.roster({host, self, e, c3});
		m.observe(c3, f3);
		m.observe(e, fe);
		m.settle_indices();
		check(m.facts(e)->index == -1 && m.facts(c3)->index == -1, "duplicated ids: two members on one native index lose it both");
		// Host-reported index for an unmapped member only when free.
		P::member_facts none{};
		m.roster({host, self, c3});
		m.observe(c3, none);
		P::table_row t3b; t3b.who = c3; t3b.index = 1; // 1 is this PC's own index
		table_to(m, host, 0, sample_table(3, host, {t3b}), 1200);
		cs = cards(m, 1300, 1);
		x = card_of(cs, c3);
		check(x && x->index == -1, "index: a host-reported index this PC holds itself is not used");
		P::table_row t3c; t3c.who = c3; t3c.index = 3;
		table_to(m, host, 0, sample_table(4, host, {t3c}), 2400);
		cs = cards(m, 2500, 1);
		x = card_of(cs, c3);
		check(x && x->index == 3 && !x->admitted, "index: a free host-reported index is shown, never as an admission");
	}

	// ---- local phase from facts ----
	{
		P::phase_inputs f;
		check(P::local_phase(f) == P::phase::connecting, "phase: no host tunnel -> connecting");
		f.host_connected = true;
		check(P::local_phase(f) == P::phase::checking_build, "phase: tunnel up, not admitted -> checking_build");
		f.participant = true;
		check(P::local_phase(f) == P::phase::in_lobby, "phase: native participant -> in_lobby");
		f.native_ready = true;
		check(P::local_phase(f) == P::phase::ready, "phase: native ready flag -> ready");
		f.hosting = true;
		check(P::local_phase(f) == P::phase::in_lobby, "phase: a host is never ready (Start is the authority)");
		f.hosting = false;
		f.jip_pending = true;
		check(P::local_phase(f) == P::phase::waiting_next_area, "phase: JIP pending -> waiting_next_area");
		f.loading = true;
		check(P::local_phase(f) == P::phase::loading, "phase: JIP pending during the zone load -> loading");
		f.jip_pending = false;
		f.inputs_flowing = true;
		check(P::local_phase(f) == P::phase::loading, "phase: loading wins over input samples");
		f.loading = false;
		check(P::local_phase(f) == P::phase::playing, "phase: playing only with inputs flowing");
		f.left = true;
		check(P::local_phase(f) == P::phase::left, "phase: left");
		P::phase_inputs h; h.hosting = true;
		check(P::local_phase(h) == P::phase::connecting, "phase: a host before its native session -> connecting");
	}
	{
		P::input_flow flow;
		const int both[] = {0, 1};
		flow.sample(0, true, 10, 0);
		flow.sample(1, true, 10, 0);
		check(!flow.flowing(both, 2, 0), "input flow: first samples alone are not play");
		flow.sample(0, true, 11, 100);
		check(!flow.flowing(both, 2, 100), "input flow: one participant's ring moving is not shared input");
		flow.sample(1, true, 11, 150);
		check(flow.flowing(both, 2, 200), "input flow: every participant's ring moved -> flowing");
		flow.sample(0, true, 12, 900);
		check(!flow.flowing(both, 2, 1200), "input flow: a participant stalled over 1000 ms -> not flowing");
		flow.sample(1, false, 0, 1300);
		flow.sample(1, true, 30, 1300);
		check(!flow.flowing(both, 2, 1300), "input flow: an unreadable ring forgets the history");
		check(!flow.flowing(nullptr, 0, 1300), "input flow: no participants -> not flowing");
	}
	{
		P::pacer p(1000, 5000);
		const std::vector<std::uint8_t> a{1, 2}, b{1, 3};
		check(p.due(a, 0), "pacer: first message due");
		p.sent(a, 0);
		check(!p.due(b, 500), "pacer: a change within 1 s waits");
		check(p.due(b, 1000) && !p.due(a, 1000), "pacer: a change after 1 s is due, no change is not");
		check(p.due(a, 5000), "pacer: refresh after 5 s");
		auto m1 = P::encode(sample_report(1)), m2 = P::encode(sample_report(2));
		check(P::without_sequence(m1) == P::without_sequence(m2) && m1 != m2, "pacer: content compares without the sequence");
	}
	{
		// Generation: RTT-only changes do not move it; a phase change does.
		P::model m;
		m.open(CODE, host, host);
		m.roster({host, c3});
		P::member_facts f; f.tunnel = f.bound = true; f.index = 1; f.rtt = 30;
		m.observe(c3, f);
		const auto g0 = m.generation();
		check(m.note_cards(cards(m, 0)) && m.generation() == g0 + 1, "generation: first roster moves it");
		f.rtt = 90;
		m.observe(c3, f);
		check(!m.note_cards(cards(m, 0)), "generation: an RTT change alone does not (no status write)");
		report_to(m, c3, sample_report(1, P::phase::playing), 0);
		check(m.note_cards(cards(m, 0)), "generation: a phase change does");
	}
	{
		// End to end: a host's encoded table and a guest's encoded report are accepted by the other side's model.
		P::model h, g;
		h.open(CODE, host, host);
		h.roster({host, self});
		g.open(CODE, self, host);
		g.roster({host, self});
		P::member_facts gh; gh.tunnel = gh.bound = true; gh.index = 0; gh.rtt = 20; gh.how = P::route::relay;
		g.observe(host, gh);
		P::member_facts hg; hg.tunnel = hg.bound = true; hg.index = 1; hg.rtt = 21; hg.how = P::route::relay;
		h.observe(self, hg);
		auto rep = g.own_report(P::phase::ready, 1, true, "LC33", "Kitty");
		rep.seq = 1;
		check(rep.rtt.size() == 1 && rep.rtt[0].who == host && rep.rtt[0].ms == 20, "own report: carries this PC's own measurements");
		check(report_to(h, self, rep, 0) == P::refusal::none, "end to end: the host accepts the guest's report");
		const auto hc = h.cards(P::phase::in_lobby, 0, "Logan", false, false, "LC33", 0);
		auto tab = h.own_table(hc, 1);
		tab.seq = 1;
		check(tab.rows.size() == 2 && tab.rows[0].who == host && tab.rows[0].index == 0 && tab.rows[0].ms == P::rtt_none && tab.rows[1].ms == 21,
		      "own table: host row index 0 without RTT, member row with the host's measurement");
		check(table_to(g, host, 0, tab, 0) == P::refusal::none, "end to end: the guest accepts the host's table");
		auto bad = h.own_report(P::phase::in_lobby, 0, false, std::string("L C\x01") + std::string(40, 'x'), std::string(40, 'n'));
		bad.seq = 2;
		P::report back;
		const auto bb = P::encode(bad);
		check(P::parse(bb.data(), bb.size(), back) == P::refusal::none && back.token.size() == 32 && back.name.size() == 31, "own report: clipped and sanitized so peers never refuse it");
	}

	std::printf("%d PASS / %d FAIL\n", passed, failed);
	return failed ? 1 : 0;
}
