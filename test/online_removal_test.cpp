// Online removal notice (rule 64 OnlineRemovalNotice, online_removal_core.hpp): the link kind 9 parser under bounds and
// seeded fuzzing, and the in-session Party Kick between simulated machines (the host, the removed member B, a third member
// C) over a lossy room wire: the host's sender with the real Party report sequence it heard (online_party_core::model), the
// members' receivers with their own report sequences and connection floors, the room layer's host-loss tracker, and the
// GNS framing's view of kind 9 (gns_transport_core::parse). Pure: no Windows, GNS or game calls. Optional argument: fuzz rounds.
#include "online_removal_core.hpp"
#include "online_party_core.hpp"
#include "online_room_core.hpp"
#include "gns_transport_core.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <random>
#include <string>
#include <vector>

namespace
{
	int passed = 0, failed = 0;
	unsigned start_failures = 0, kicks = 0; // every simulated Party Kick starts exactly one notice
	void check(bool ok, const std::string& what)
	{
		std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
		(ok ? passed : failed)++;
	}
	namespace X = online_removal_core;
	namespace P = online_party_core;
	using F = X::refusal;
	const char* const CODE = "K7QN2X9M";
	const char* const OTHER = "ABCD2345";
	P::key K(std::uint8_t n) { P::key k{}; k.fill(n); return k; }
	const P::key HOST = K(0x11), B = K(0x22), C = K(0x33);

	X::notice sample(std::uint32_t heard = 7, std::uint32_t serial = 1, std::uint8_t index = 1)
	{
		X::notice n;
		std::memcpy(n.code, CODE, 8);
		n.host = HOST;
		n.target = B;
		n.index = index;
		n.heard = heard;
		n.serial = serial;
		return n;
	}

	// One member PC: its report sequence (per process), its connection floor to the host, its native session, the receiver,
	// the room layer's tracker, and what reached its session (native 0x2b bodies) and its Hub.
	struct member
	{
		P::key self{};
		int index = 1;
		std::uint32_t reports = 0, floor = 0;
		bool session = true, room = true, host_connected = true;
		X::receiver rx;
		online_room_core::host_loss_tracker loss;
		std::vector<std::array<std::uint8_t, X::native_body_size>> delivered;
		unsigned refused_logged = 0;
		void connect() { floor = reports; loss.connected(); host_connected = true; }
		std::uint32_t report() { return ++reports; }
		X::guest_view view(bool from_host) const
		{
			X::guest_view v;
			v.room = room;
			v.code = CODE;
			v.self = self;
			v.host = HOST;
			v.from_host_tunnel = from_host;
			v.floor = floor;
			v.sent = reports;
			v.session = session;
			v.own_index = session ? index : -1;
			return v;
		}
		// The room layer's kind-9 path (online_room.cpp removal_receive + removal_deliver_tick).
		F receive(const std::vector<std::uint8_t>& bytes, bool from_host)
		{
			// The GNS framing never sees kind 9 as one of its own (a build without rule 64 drops it as invalid).
			const auto framed = gns_transport_core::parse(bytes.data(), bytes.size());
			if (framed.valid) return F::count;
			const auto why = rx.on_message(view(from_host), bytes.data(), bytes.size());
			if (why == F::none)
			{
				loss.removed_by_host();
				delivered.push_back(X::native_body(std::uint8_t(index)));
				session = false; // the native Kick's removal ends it (0x612f60 -> 0x612c40 -> the terminal clear)
			}
			else if (why != F::repeat) ++refused_logged;
			return why;
		}
		// The removed PC's departure: it left the room after its session ended, so its host connection closes.
		bool leaving() const { return !session && rx.removed(); }
		// The host connection ends (the host dropped it at the resend bound, or the host quit).
		void host_ended() { loss.ended(true); host_connected = false; session = false; }
	};

	// The host: the real Party model hears the members' reports (kind 7), the sender repeats the notice.
	struct host_pc
	{
		P::model party;
		X::sender out;
		std::uint32_t now = 100000;
		host_pc()
		{
			party.open(CODE, HOST, HOST);
			party.roster({HOST, B, C});
		}
		bool hear(member& m)
		{
			P::report r;
			std::memcpy(r.code, CODE, 8);
			r.seq = m.report();
			r.now = P::phase::playing;
			r.index = std::uint8_t(m.index);
			r.token = "LC33";
			r.name = "Kitty";
			const auto b = P::encode(r);
			return party.on_report(m.self, b.data(), b.size(), now) == P::refusal::none;
		}
	};

	struct wire
	{
		std::mt19937 rng{1};
		unsigned drop_first = 0, sent = 0;
		double loss = 0;
		bool deliver()
		{
			++sent;
			if (sent <= drop_first) return false;
			return std::uniform_real_distribution<double>(0, 1)(rng) >= loss;
		}
	};

	// One Party Kick of B, run on the host's tick (every 50 ms) until B leaves or the window closes. Returns the host's sends.
	struct outcome { bool accepted = false, host_dropped = false, b_panel = false, c_touched = false; unsigned sends = 0, deliveries = 0; std::uint32_t ms = 0; };
	outcome kick(wire w, bool b_has_reports = true, unsigned host_ticks_before_report = 0)
	{
		host_pc h;
		member b, c;
		b.self = B; b.index = 1; c.self = C; c.index = 2;
		b.connect(); c.connect();
		if (b_has_reports) for (int i = 0; i < 3; ++i) { h.hear(b); h.now += 1000; }
		for (int i = 0; i < 3; ++i) h.hear(c);
		outcome o;
		const auto start = h.now;
		++kicks;
		if (!h.out.start(B, 1, h.now)) ++start_failures;
		std::deque<std::vector<std::uint8_t>> to_b;
		bool b_left = false;
		for (unsigned tick = 0; tick < 400; ++tick, h.now += 50)
		{
			if (!b_has_reports && tick == host_ticks_before_report) h.hear(b); // a report arrives late (after a reconnect)
			const auto* t = h.out.find(B);
			if (!t) break;
			if (b_left) { h.out.finish(B); break; }
			const auto step = h.out.due(*t, h.now);
			if (step == X::sender::step::expire) { o.host_dropped = true; b.host_ended(); h.out.finish(B); break; }
			if (step == X::sender::step::send)
			{
				const auto heard = h.party.heard(B);
				if (!heard) continue;
				const auto bytes = X::encode(h.out.make(*t, CODE, HOST, heard));
				h.out.sent(B, h.now);
				++o.sends;
				if (w.deliver()) to_b.push_back(bytes);
			}
			while (!to_b.empty()) { b.receive(to_b.front(), true); to_b.pop_front(); }
			if (b.leaving()) b_left = true; // its room leave closes the tunnel; the host sees it on a later tick
		}
		o.accepted = b.rx.removed();
		o.deliveries = unsigned(b.delivered.size());
		o.b_panel = b.loss.lost();
		o.c_touched = c.rx.removed() || !c.session || !c.delivered.empty();
		o.ms = h.now - start;
		return o;
	}

	void parser()
	{
		const auto good = X::encode(sample());
		X::notice back;
		check(good.size() == 35 && X::message_size == 35, "wire: a notice is exactly 35 bytes");
		check(X::parse(good.data(), good.size(), back) == F::none && back.heard == 7 && back.serial == 1 && back.index == 1 && back.target == B && back.host == HOST &&
		      std::memcmp(back.code, CODE, 8) == 0, "wire: round trip");
		bool sizes = true;
		for (std::size_t n = 0; n <= 80; ++n)
		{
			std::vector<std::uint8_t> b(good.begin(), good.end());
			b.resize(n, 0);
			if (n != good.size() && X::parse(b.data(), b.size(), back) != F::size) sizes = false;
		}
		check(sizes && X::parse(nullptr, 35, back) == F::size, "wire: every other size (0..80) and a null buffer are refused (size)");
		auto bad = [&](std::size_t at, std::uint8_t value, F want, const char* what)
		{
			auto b = good;
			b[at] = value;
			check(X::parse(b.data(), b.size(), back) == want, std::string("wire: ") + what);
		};
		bad(0, 7, F::kind, "another kind (7) is refused");
		bad(0, 16, F::kind, "the restore kind (16) is refused");
		bad(1, 2, F::wire, "wire 2 is refused");
		bad(1, 0, F::wire, "wire 0 is refused");
		bad(2, 'I', F::code, "a room code character outside the alphabet is refused");
		bad(9, 0, F::code, "a NUL in the room code is refused");
		bad(26, 0, F::index, "index 0 (the host) is refused");
		bad(26, 4, F::index, "index 4 is refused");
		bad(26, 0xff, F::index, "index 0xff is refused");
		{
			auto n = sample(); n.host = P::key{};
			auto b = X::encode(n); check(X::parse(b.data(), b.size(), back) == F::keys, "wire: a zero host key is refused");
			n = sample(); n.target = P::key{};
			b = X::encode(n); check(X::parse(b.data(), b.size(), back) == F::keys, "wire: a zero target key is refused");
			n = sample(); n.target = HOST;
			b = X::encode(n); check(X::parse(b.data(), b.size(), back) == F::keys, "wire: the host naming itself is refused");
			n = sample(0); b = X::encode(n); check(X::parse(b.data(), b.size(), back) == F::heard, "wire: heard 0 is refused");
			n = sample(7, 0); b = X::encode(n); check(X::parse(b.data(), b.size(), back) == F::serial, "wire: serial 0 is refused");
			n = sample(0xffffffffu, 0xffffffffu, 3); b = X::encode(n);
			check(X::parse(b.data(), b.size(), back) == F::none && back.heard == 0xffffffffu && back.serial == 0xffffffffu && back.index == 3, "wire: the largest values round-trip");
		}
		// The GNS framing (rule 25, framing 4) does not know kind 9: a build without rule 64 drops it (never as a game message).
		const auto framed = gns_transport_core::parse(good.data(), good.size());
		check(!framed.valid && !gns_transport_core::tunnel_message_allowed(true, framed), "framing: kind 9 is no GNS framing kind (dropped, not delivered as game data)");
		check(X::rule_id == 64 && X::rule_revision == 1 && X::rule_effective[0] == 1 && X::rule_effective[1] == 1 && X::rule_effective[2] == 9 && X::rule_effective[3] == 0,
		      "agreement: rule 64 revision 1, effective {1, 1, 9, 0}");
		const auto body = X::native_body(2);
		check(body.size() == 12 && body[0] == 2 && body[1] == 0 && body[4] == 0xce && body[5] == 0x10 && body[8] == 1 && body[9] == 0 && body[11] == 0,
		      "native: the delivered 0x2b body = index u32, reason 0x10CE, flag 1 (the host's native Kick)");
	}

	void fuzz(unsigned rounds)
	{
		std::mt19937 rng(0x6464);
		const auto good = X::encode(sample());
		unsigned accepted = 0, mismatched = 0;
		for (unsigned i = 0; i < rounds; ++i)
		{
			std::vector<std::uint8_t> b;
			if (i % 3 == 0) { b.resize(rng() % 64); for (auto& x : b) x = std::uint8_t(rng()); if (!b.empty()) b[0] = X::link_kind; if (b.size() > 1) b[1] = 1; }
			else { b = good; const auto flips = 1 + rng() % 4; for (unsigned k = 0; k < flips; ++k) b[rng() % b.size()] = std::uint8_t(rng()); }
			X::notice n;
			if (X::parse(b.data(), b.size(), n) != F::none) continue;
			++accepted;
			// Anything accepted re-encodes to the same bytes and satisfies every structural bound.
			const auto again = X::encode(n);
			if (again != b || n.index < 1 || n.index > 3 || !n.heard || !n.serial || n.host == n.target || !P::valid_code(n.code)) ++mismatched;
		}
		check(mismatched == 0, "fuzz: " + std::to_string(rounds) + " random / mutated messages, " + std::to_string(accepted) + " parsed, every one canonical and in bounds");
	}

	void receiver_rules()
	{
		member b;
		b.self = B;
		b.index = 1;
		b.reports = 40;
		b.connect(); // floor 40
		b.reports = 45;
		auto send = [&](const X::notice& n, bool from_host) { return b.receive(X::encode(n), from_host); };
		check(send(sample(44), false) == F::not_host && !b.rx.removed(), "forged: the same notice on another member's tunnel is refused (not the host)");
		{
			auto n = sample(44); n.host = C;
			check(send(n, true) == F::not_host, "forged: a host key that is not the room's is refused even on the host tunnel");
		}
		{
			auto v = b.view(true); v.hosting = true;
			const auto bytes = X::encode(sample(44));
			X::receiver r;
			check(r.on_message(v, bytes.data(), bytes.size()) == F::not_host, "forged: a host never accepts a removal (it is the room's host)");
		}
		{
			auto n = sample(44); std::memcpy(n.code, OTHER, 8);
			check(send(n, true) == F::room, "stale: another room's notice is refused");
			auto v = b.view(true); v.room = false;
			const auto bytes = X::encode(sample(44));
			X::receiver r;
			check(r.on_message(v, bytes.data(), bytes.size()) == F::room, "stale: no room, no removal");
		}
		{
			auto n = sample(44); n.target = C;
			check(send(n, true) == F::target, "misdirected: a notice for another member is refused");
		}
		check(send(sample(40), true) == F::stale, "stale: heard at the connection floor (an earlier connection's report) is refused");
		check(send(sample(12), true) == F::stale, "stale: an older session's notice (heard 12 < floor 40) is refused");
		check(send(sample(46), true) == F::future, "forged: heard above any report this PC sent is refused");
		check(send(sample(44, 1, 2), true) == F::session, "stale: a notice naming another native index of this PC is refused");
		b.session = false;
		check(send(sample(44), true) == F::session && !b.rx.removed(), "stale: outside a native session the notice changes nothing");
		b.session = true;
		check(b.refused_logged == 9 && b.delivered.empty() && b.session, "refusals: each one logged, nothing delivered, the session keeps running");
		check(send(sample(41, 9), true) == F::none && b.rx.removed() && b.rx.serial() == 9 && b.delivered.size() == 1 && !b.loss.lost(),
		      "accepted: the first valid notice of this connection removes this PC once");
		check(send(sample(41, 9), true) == F::repeat && send(sample(45, 9), true) == F::repeat && b.delivered.size() == 1, "duplicates: resends change nothing (repeat)");
		check(b.rx.refused(F::stale) == 2 && b.rx.refused(F::not_host) == 2 && b.rx.refused(F::repeat) == 2, "counters: per-reason refusal counts");
		// A re-join of the same room: new connection, floor moves above every earlier report; the old resends are stale.
		b.rx.reset(); b.session = true; b.delivered.clear();
		b.connect(); // floor 45
		check(send(sample(45, 9), true) == F::stale && !b.rx.removed(), "re-join: the previous session's notice (heard 45 = floor) is ignored");
		b.report();
		check(send(sample(46, 10), true) == F::none, "re-join: a new removal on the new connection is accepted");
	}

	void sender_rules()
	{
		X::sender s;
		check(!s.start(P::key{}, 1, 0) && !s.start(B, 0, 0) && !s.start(B, 4, 0), "sender: a zero key, the host index and index 4 are refused");
		check(s.start(B, 1, 1000) && !s.start(B, 1, 1000), "sender: one pending notice per member (a second Kick of B does nothing)");
		check(s.start(C, 2, 1000) && s.start(K(0x44), 3, 1000) && !s.start(K(0x55), 3, 1000), "sender: at most three pending (the remote members)");
		const auto* t = s.find(B);
		check(t && s.due(*t, 1000) == X::sender::step::send, "sender: the first notice goes at once");
		s.sent(B, 1000);
		t = s.find(B);
		check(s.due(*t, 1249) == X::sender::step::wait && s.due(*t, 1250) == X::sender::step::send, "sender: repeated every 250 ms");
		check(s.due(*t, 10999) != X::sender::step::expire && s.due(*t, 11000) == X::sender::step::expire, "sender: the window ends after 10 s");
		const auto serial = t->serial;
		s.finish(B);
		check(!s.find(B) && s.start(B, 1, 20000) && s.find(B)->serial != serial, "sender: a later Kick of the same member is a new serial");
	}

	void machines()
	{
		{
			const auto o = kick(wire{});
			check(o.accepted && o.deliveries == 1 && !o.b_panel && !o.host_dropped && !o.c_touched && o.sends == 1,
			      "two machines: Party Kick -> B accepts the first notice, its session ends as a native Kick (one 0x2b), no panel; C untouched");
		}
		bool all = true;
		unsigned worst = 0;
		for (unsigned n = 1; n <= 39; ++n)
		{
			wire w; w.drop_first = n;
			const auto o = kick(w);
			all = all && o.accepted && o.deliveries == 1 && !o.b_panel && !o.host_dropped && o.sends == n + 1;
			worst = std::max(worst, o.ms);
		}
		check(all, "lost notices: with the first 1..39 notices dropped B still ends once with the notice (last after " + std::to_string(worst) + " ms)");
		{
			wire w; w.drop_first = 1000;
			const auto o = kick(w);
			check(!o.accepted && o.host_dropped && o.b_panel && o.sends == 40, "lost notices: all 40 lost -> the host closes B's room connection at 10 s; B gets the host-left panel, never limbo");
		}
		{
			unsigned ok = 0;
			for (unsigned seed = 1; seed <= 200; ++seed)
			{
				wire w; w.rng.seed(seed); w.loss = 0.5;
				const auto o = kick(w);
				ok += o.accepted && o.deliveries == 1 && !o.b_panel;
			}
			check(ok == 200, "lossy wire: 200 Kicks at 50% loss, every one ends B once with the notice (" + std::to_string(ok) + "/200)");
		}
		{
			const auto o = kick(wire{}, false, 20);
			check(o.accepted && o.deliveries == 1 && !o.b_panel, "no report heard yet on this connection: the notice waits for one, then B ends with it");
		}
		{
			// Host loss (the host quits / vanishes): no notice, the connection that carried the session ends -> the 07 panel.
			member b; b.self = B; b.connect(); b.report();
			b.host_ended();
			check(b.loss.lost() && !b.rx.removed() && b.delivered.empty(), "host loss: without a notice the host-left panel stays (the 07 panel)");
			member q; q.self = B; q.connect(); q.loss.local_departure(); q.host_ended();
			check(!q.loss.lost(), "guest Leave / Quit: a local departure is never a host loss (unchanged)");
		}
		check(start_failures == 0 && kicks == 242, "host: each of the " + std::to_string(kicks) + " simulated Party Kicks started exactly one notice");
	}
}

int main(int argc, char** argv)
{
	const unsigned rounds = argc > 1 ? unsigned(std::strtoul(argv[1], nullptr, 10)) : 200000u;
	parser();
	fuzz(rounds);
	receiver_rules();
	sender_rules();
	machines();
	std::printf("online removal notice: %d passed, %d failed\n", passed, failed);
	return failed ? 1 : 0;
}
