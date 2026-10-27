#include "gns_transport_core.hpp"
#include "network_validation.hpp"
#include "gns_transport_rules.hpp"
#include "gameplay_capabilities.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// [Online] Transport=gns: the adapter's buffers and framing without GNS or the game (src/gns_transport_core.hpp), the
// rule-25 agreement entry, and - with a retail XMen2.exe as the optional argument, read only - every install guard.
namespace
{
	int failures = 0, passes = 0;
	void check(bool ok, const char* what)
	{
		std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
		ok ? ++passes : ++failures;
	}
	using namespace gns_transport_core;
	std::vector<std::uint8_t> body(std::uint8_t fill, std::size_t n) { return std::vector<std::uint8_t>(n, fill); }
	struct sink
	{
		std::vector<message> sent;
		std::size_t refuse_after = SIZE_MAX;
		bool operator()(const message& m, const std::vector<std::uint8_t>& bytes)
		{
			if (sent.size() >= refuse_after) return false;
			if (bytes.size() != m.body.size() + 2 || bytes[0] != 0 || bytes[1] != m.opcode) return false;
			sent.push_back(m);
			return true;
		}
	};
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
}

int main(int argc, char** argv)
{
	{
		// Retail 0x612449 emits a teardown notice for the local participant;
		// 0x6133c9 emits the same wire opcode for several other removal causes.
		const std::uint32_t teardown[]{0x61244e, 0, 0x10cc, 0x1106, 0xabcd0001};
		const auto a = read_removal_notice(teardown);
		check(a.caller == 0x61244e && a.index == 0 && a.lost == 0x10cc && a.others == 0x1106 && a.flag == 1,
		      "B1 diagnostic records native teardown caller and both reason codes (flag is a byte)");
		const std::uint32_t other[]{0x6133ce, 1, 0x10ce, 0x10cf, 0};
		const auto b = read_removal_notice(other);
		check(b.caller == 0x6133ce && b.index == 1 && b.lost == 0x10ce && b.others == 0x10cf && b.flag == 0,
		      "same 0x2b opcode preserves a different removal source and reasons");
	}

	check(hello_due(true, false, 1000, 0), "hello: connector starts the handshake");
	check(!hello_due(true, false, 1249, 1000), "hello: no busy retry before 250 ms");
	check(hello_due(true, false, 1250, 1000), "hello: retry when the listener was not yet admitted at the first hello");
	check(!hello_due(true, true, 2000, 1000) && !hello_due(false, false, 2000, 1000), "hello: a bound link or listener does not retry");
	check(hello_due(true, false, 20, 0xffffff10u), "hello: retry interval survives the tick counter wrapping");
	// Ordering: whole messages leave in enqueue order, reliable and unreliable alike, bodies copied at enqueue.
	{
		outbox o;
		auto a = body(1, 10), b = body(2, 700), c = body(3, 0);
		o.enqueue(0x05, true, false, a.data(), a.size());
		o.enqueue(0x66, false, false, b.data(), b.size());
		a[0] = 9; // the game's body buffer is reused after the enqueue returns
		o.enqueue(0x48, true, false, nullptr, 0);
		sink s;
		check(o.flush(s) == 3 && s.sent.size() == 3, "ordering: three messages flushed");
		check(s.sent[0].opcode == 0x05 && s.sent[1].opcode == 0x66 && s.sent[2].opcode == 0x48, "ordering: enqueue order kept across reliable and unreliable");
		check(s.sent[0].body == body(1, 10) && s.sent[1].body.size() == 700 && s.sent[2].body.empty(), "ordering: bodies copied at enqueue, a 700-byte body whole (no 300-byte fragments)");
		check(s.sent[0].reliable && !s.sent[1].reliable && s.sent[2].reliable, "ordering: the reliable flag (+0xe) chooses the lane per message");
		check(o.pending() == 0 && o.in_flight() == 2, "ordering: only reliable messages are tracked in flight");
		(void)c;
	}
	// Coalesce (+0xf): at most one queued reliable message per opcode, the newest at the back.
	{
		outbox o;
		auto x = body(1, 4), y = body(2, 4), z = body(3, 4);
		o.enqueue(0x5c, true, false, x.data(), x.size());
		o.enqueue(0x01, true, false, y.data(), y.size());
		o.enqueue(0x5c, true, true, z.data(), z.size());
		sink s;
		o.flush(s);
		check(s.sent.size() == 2 && s.sent[0].opcode == 0x01 && s.sent[1].opcode == 0x5c && s.sent[1].body == body(3, 4), "coalesce: the queued 0x5c is replaced and the newest goes last");
		o.enqueue(0x5d, false, false, x.data(), x.size());
		o.enqueue(0x5d, true, true, y.data(), y.size());
		sink t;
		o.flush(t);
		check(t.sent.size() == 2, "coalesce: an unreliable message of the opcode is not in the native reliable queue and stays");
		o.enqueue(0x07, true, true, x.data(), x.size());
		sink u;
		o.flush(u);
		check(u.sent.size() == 1 && u.sent[0].opcode == 0x07, "coalesce: nothing queued of the opcode - just queued");
		o.enqueue(0x07, true, true, y.data(), y.size());
		check(o.count(0x07) == 2, "coalesce: a message GNS already holds cannot be recalled (counted until acknowledged)");
	}
	// A full GNS send buffer stops the flush; the rest keeps its order for the next tick.
	{
		outbox o;
		for (std::uint8_t i = 1; i <= 5; ++i) { auto b = body(i, 3); o.enqueue(i, true, false, b.data(), b.size()); }
		sink s;
		s.refuse_after = 2;
		check(o.flush(s) == 2 && o.pending() == 3, "send buffer full: two sent, three kept");
		s.refuse_after = SIZE_MAX;
		check(o.flush(s) == 3 && s.sent.size() == 5, "send buffer full: the remainder goes next tick");
		bool ordered = true;
		for (std::uint8_t i = 0; i < 5; ++i) ordered = ordered && s.sent[i].opcode == i + 1;
		check(ordered, "send buffer full: order preserved across ticks");
	}
	// Backlog (0x610790 for the campaign sender's window): buffered + GNS-unacknowledged reliable, conservatively.
	{
		outbox o;
		auto chunk = body(0x48, 200);
		for (int i = 0; i < 3; ++i) o.enqueue(0x48, true, false, chunk.data(), chunk.size());
		check(o.count(0x48) == 3 && o.count(0x4a) == 0, "backlog: buffered messages count");
		sink s;
		o.flush(s);
		o.outstanding(3 * 202);
		check(o.count(0x48) == 3, "backlog: all bytes outstanding - three in flight");
		o.outstanding(202);
		check(o.count(0x48) == 1, "backlog: one message's bytes outstanding - one in flight");
		o.enqueue(0x48, true, false, chunk.data(), chunk.size());
		check(o.count(0x48) == 2, "backlog: buffered plus in flight");
		o.flush(s);
		o.outstanding(202 + 203);
		check(o.count(0x48) == 2, "backlog: GNS framing bytes keep a message counted (never under-reported)");
		o.outstanding(0);
		check(o.count(0x48) == 0 && o.in_flight() == 0, "backlog: everything acknowledged - zero");
		inbox in;
		message m; m.opcode = 0x48; m.reliable = true;
		in.push(m); in.push(m);
		m.reliable = false; in.push(m);
		check(in.count(0x48) == 2, "backlog: received-undelivered reliable messages count as the native receive queue does");
	}
	// Inbound: arrival order, the dispatcher's return 2 holds the head and everything after it.
	{
		inbox in;
		for (std::uint8_t i = 1; i <= 4; ++i) { message m; m.opcode = i; m.reliable = true; in.push(m); }
		std::vector<std::uint8_t> seen;
		int defer_at = 2;
		auto deliver = [&](const message& m) { if (m.opcode == defer_at) { defer_at = -1; return 2; } seen.push_back(m.opcode); return 1; };
		check(in.drain(deliver) == 1 && in.size() == 3, "defer: return 2 stops after the first message");
		check(seen.size() == 1 && seen[0] == 1, "defer: nothing after the deferred message was delivered");
		check(in.drain(deliver) == 3 && seen == std::vector<std::uint8_t>{1, 2, 3, 4}, "defer: the next tick resumes at the deferred message, in order");
		auto unhandled = [&](const message&) { return 0; };
		message m; m.opcode = 9; in.push(m);
		check(in.drain(unhandled) == 1 && in.size() == 0, "defer: an unhandled message (0) is consumed like the native drain");
		inbox small(2);
		check(small.push(m) && small.push(m) && !small.push(m), "defer: the receive queue is bounded (the link ends instead of growing)");
	}
	// Byte caps: count-only queues let a peer exhaust the 32-bit game's address space.
	{
		outbox o;
		auto chunk = body(0x5a, 700);
		for (int i = 0; i < 3000; ++i) o.enqueue(0x48, true, false, chunk.data(), chunk.size());
		check(o.pending() == 2987, "outbound byte cap: 2 MiB of framed messages is the hard limit, further enqueues are refused");
		sink s;
		o.flush(s);
		check(s.sent.size() == 2987 && o.pending() == 0, "outbound byte cap: flushing releases every pending message");
	}
	{
		outbox o;
		auto chunk = body(0x6b, 1200);
		for (int i = 0; i < 1744; ++i) o.enqueue(i == 0 ? 0x5c : 0x48, true, false, chunk.data(), chunk.size());
		o.enqueue(0x5c, true, true, chunk.data(), chunk.size());
		o.enqueue(0x49, true, false, chunk.data(), chunk.size());
		check(o.pending() == 1744, "outbound byte cap: coalesce replaces bytes instead of hiding overflow");
	}
	{
		inbox in;
		message m; m.opcode = 0x48; m.reliable = true; m.body = body(0x48, 700);
		for (int i = 0; i < 2000; ++i) in.push(m);
		check(in.size() == 1497, "inbound byte cap: 1 MiB of bodies is the hard limit");
		int delivered = 0;
		auto deliver = [&](const message&) { return ++delivered == 1 ? 1 : 2; };
		check(in.drain(deliver) == 1 && in.push(m) && in.size() == 1497,
		      "inbound byte cap: draining frees exactly one message's space for a later push");
		in.clear();
		check(in.size() == 0 && in.push(m), "inbound byte cap: clear resets the queue for a later push");
	}
	// Accepted wire sizes: the stock parser's true 0x505 body cap, not the smaller direct-send stack frame.
	{
		message game; game.opcode = 0x42; game.body = body(1, 0x505);
		const auto largest = frame(game);
		game.body = body(1, 0x506);
		const auto too_large = frame(game);
		check(parse(largest.data(), largest.size()).valid && !parse(too_large.data(), too_large.size()).valid,
		      "wire size: game bodies stop at 0x505 bytes");
		auto stock = body(2, 1500);
		const auto largest_datagram = datagram(5165, 5165, stock.data(), stock.size());
		stock = body(2, 1501);
		const auto large_datagram = datagram(5165, 5165, stock.data(), stock.size());
		check(parse(largest_datagram.data(), largest_datagram.size()).valid && !parse(large_datagram.data(), large_datagram.size()).valid,
		      "wire size: tunnelled datagrams stop at 1500 bytes");
	}
	// Room-tunnel admission and listener policy: unbound peers cannot stockpile game traffic.
	{
#if GNS_TRANSPORT_TUNNEL_POLICY
		message game_message; game_message.opcode = 0x42;
		const auto game_frame = frame(game_message);
		const auto game = parse(game_frame.data(), game_frame.size());
		const auto hello_frame = hello(kind::hello, 0x01020304, 5165);
		const auto hello_message = parse(hello_frame.data(), hello_frame.size());
		const auto ack_frame = hello(kind::hello_ack, 0x01020304, 5165);
		const auto ack = parse(ack_frame.data(), ack_frame.size());
		const std::uint8_t ping = std::uint8_t(kind::keepalive);
		const auto keepalive = parse(&ping, 1);
		const std::uint8_t garbage = 0xee;
		const auto invalid = parse(&garbage, 1);
		check(!tunnel_message_allowed(false, game) && !tunnel_message_allowed(false, keepalive) && !tunnel_message_allowed(false, invalid) &&
		      tunnel_message_allowed(false, hello_message) && tunnel_message_allowed(false, ack),
		      "unbound tunnel: only hello/hello_ack control messages are retained");
		check(tunnel_message_allowed(true, game) && tunnel_message_allowed(true, keepalive) && !tunnel_message_allowed(true, invalid),
		      "bound tunnel: game traffic is admitted and garbage stays refused");
		check(!create_ip_listener(true) && create_ip_listener(false), "room mode: no GNS IP listener; LAN mode keeps it");
		check(stray_allowed(0, false) && stray_allowed(7, false) && !stray_allowed(8, false) && !stray_allowed(0, true),
		      "stray connections: eight total and one per remote IP");
#else
		check(false, "unbound tunnel: only hello/hello_ack control messages are retained");
		check(false, "room mode: no GNS IP listener; LAN mode keeps it");
		check(false, "stray connections: eight total and one per remote IP");
#endif
	}
	// D3's table plugs in here later; the base build has no such receive-validation entry point.
	{
#if GNS_TRANSPORT_OPCODE_VALIDATOR
		set_opcode_length_validator([](std::uint8_t opcode, std::size_t length) { return opcode == 0x42 && length == 3; });
		inbox in;
		message m; m.opcode = 0x42; m.body = body(1, 2);
		if (validate_opcode_length(m.opcode, m.body.size())) in.push(m);
		check(in.size() == 0 && !validate_opcode_length(0x43, 3), "validation hook: wrong length and wrong opcode are refused before queueing");
		m.body = body(1, 3);
		if (validate_opcode_length(m.opcode, m.body.size())) in.push(m);
		check(in.size() == 1, "validation hook: the accepted opcode and exact length reach the queue");
		set_opcode_length_validator(nullptr);
#else
		check(false, "validation hook: wrong length and wrong opcode are refused before queueing");
		check(false, "validation hook: the accepted opcode and exact length reach the queue");
#endif
	}
	// D3's researched receive table and the body-aware exceptions at the pre-dispatch boundary.
	{
		using namespace network_validation;
		// One table for both lanes: the GNS boundary reads the stock-path table (0.2.3h captured-byte lengths, wrapper
		// convention and the dynamically registered opcodes), never a copy of its own.
		bool table_ok = true;
		for (unsigned op = 0; op < 256; ++op)
		{
			const auto* got = find_rule(static_cast<std::uint8_t>(op));
			if (got != receive_validation_rules::find_rule(static_cast<std::uint8_t>(op)))
			{
				std::printf("shared table mismatch at opcode 0x%02x\n", op);
				table_ok = false;
			}
			if (got && (got->min_len > got->max_len || got->max_len > 0x505)) table_ok = false;
		}
		for (const unsigned op : {0x12u, 0x13u, 0x27u, 0x30u, 0x52u, 0x73u, 0xffu}) if (find_rule(static_cast<std::uint8_t>(op))) table_ok = false;
		for (const unsigned op : {0x20u, 0x21u, 0x22u, 0x23u, 0x24u, 0x25u, 0x53u, 0x54u, 0x55u, 0x56u, 0x57u, 0x58u, 0x59u}) if (!find_rule(static_cast<std::uint8_t>(op))) table_ok = false;
		check(table_ok, "shared table: GNS lane and stock lane read one table; fragment and unknown opcodes reject; dynamic opcodes accepted");

		context stock;
		// 0.2.3i maxima: a fixed-layout handler ignores extra bytes, so its row accepts any body from its minimum up to
		// the 0x505 stock cap; short bodies and bodies past the cap still reject (same table as the stock lane).
		auto fixed = body(1, 0x506);
		check(valid_message(0x01, fixed.data(), 33, stock) && !valid_message(0x01, fixed.data(), 32, stock) && valid_message(0x01, fixed.data(), 34, stock) &&
		          valid_message(0x01, fixed.data(), 0x505, stock) && !valid_message(0x01, fixed.data(), 0x506, stock),
		      "D3 lengths: fixed-size lockstep bodies reject short input and bodies past the stock cap, accept extra bytes");
		auto admission = body(0, 6);
		std::uint32_t index = 3;
		std::memcpy(admission.data(), &index, 4);
		check(valid_message(0x1b, admission.data(), admission.size(), stock), "D3 admission: 0x1b keeps a valid in-range player index");
		index = 4; std::memcpy(admission.data(), &index, 4);
		check(!valid_message(0x1b, admission.data(), admission.size(), stock), "D3 admission: 0x1b rejects the first out-of-range player index");
		admission.resize(77); index = 3; std::memcpy(admission.data() + 0x30, &index, 4);
		check(valid_message(0x1f, admission.data(), admission.size(), stock), "D3 admission: 0x1f remains in the handshake with a valid index");
		index = 4; std::memcpy(admission.data() + 0x30, &index, 4);
		check(!valid_message(0x1f, admission.data(), admission.size(), stock) && !valid_message(0x1f, admission.data(), 76, stock),
		      "D3 admission: 0x1f rejects an out-of-range index or short body");

		std::vector<std::uint8_t> direct(9, 0); direct[2] = 0x1b; index = 3; std::memcpy(direct.data() + 3, &index, 4);
		std::vector<std::uint8_t> wrapped(12, 0); wrapped[2] = 0; wrapped[3] = 0x1b; std::memcpy(wrapped.data() + 6, &index, 4);
		check(valid_tunneled_datagram(direct.data(), direct.size(), 4) && valid_tunneled_datagram(wrapped.data(), wrapped.size(), 4),
		      "D3 tunnel: direct and reliable-wrapped admission indexes pass when in range");
		index = 4; std::memcpy(direct.data() + 3, &index, 4); std::memcpy(wrapped.data() + 6, &index, 4);
		const std::uint8_t unrelated[] = {0, 0, 0x16};
		check(!valid_tunneled_datagram(direct.data(), direct.size(), 4) && !valid_tunneled_datagram(wrapped.data(), wrapped.size(), 4) && valid_tunneled_datagram(unrelated, sizeof(unrelated), 4),
		      "D3 tunnel: hostile admission indexes reject without changing unrelated stock datagrams");

		auto transfer = body(1, 301);
		context fast; fast.fast_transfer = true;
		check(valid_message(0x48, transfer.data(), 200, stock) && !valid_message(0x48, transfer.data(), 184, stock) && !valid_message(0x48, transfer.data(), 300, stock),
		      "D3 transfer: without rule 24 the native stock chunk is exactly 200 bytes");
		transfer[0] = 'D';
		check(valid_message(0x48, transfer.data(), 300, fast) && valid_message(0x48, transfer.data(), 2, fast) && !valid_message(0x48, transfer.data(), 1, fast) && !valid_message(0x48, transfer.data(), 301, fast),
		      "D3 transfer: rule 24 accepts one to 299 data bytes and rejects empty or oversized records");
		const std::uint8_t zero[] = {'Z', 0x40, 0, 0, 0}, unknown[] = {'X', 1};
		check(valid_message(0x48, zero, sizeof(zero), fast) && !valid_message(0x48, zero, 4, fast) && !valid_message(0x48, unknown, sizeof(unknown), fast),
		      "D3 transfer: rule 24 accepts five-byte zero runs and rejects malformed tags or lengths");

		auto join = body(1, 68);
		check(!valid_message(0x18, join.data(), join.size(), stock), "D3 strings: 0x18 requires a terminated game name");
		join[0x3e] = 0;
		check(valid_message(0x18, join.data(), join.size(), stock) && !valid_message(0x18, join.data(), 67, stock),
		      "D3 strings: a bounded 0x18 name and the minimum struct are accepted");
		auto roster = body(1, 238); roster[0xaa] = 60;
		check(!valid_message(0x44, roster.data(), roster.size(), stock), "D3 roster: the count-derived record loop cannot pass the body end");
		roster.resize(285);
		check(valid_message(0x44, roster.data(), roster.size(), stock), "D3 roster: a body covering every counted record is accepted");
		auto text = body('A', 4);
		check(!valid_message(0x72, text.data(), text.size(), stock), "D3 strings: 0x72 rejects an unterminated copy");
		text.back() = 0;
		check(valid_message(0x72, text.data(), text.size(), stock), "D3 strings: 0x72 accepts a short NUL-terminated string");

		auto indexed = body(0, 40); index = 5; std::memcpy(indexed.data(), &index, 4);
		check(valid_message(0x29, indexed.data(), 4, stock) && valid_message(0x46, indexed.data(), 4, stock) && valid_message(0x5d, indexed.data(), 40, stock),
		      "D3 indexes: peer indexes through five remain valid");
		index = 6; std::memcpy(indexed.data(), &index, 4);
		check(!valid_message(0x29, indexed.data(), 4, stock) && !valid_message(0x46, indexed.data(), 4, stock) && !valid_message(0x5d, indexed.data(), 40, stock),
		      "D3 indexes: out-of-range peer writes reject before dispatch");
		indexed[0] = 0xff;
		check(!valid_message(0x5d, indexed.data(), 40, stock), "D3 indexes: a negative signed lockstep slot rejects");

		// Reliable wrapper [opcode][seq u16][payload]; the dispatch length excludes the opcode byte (payload + 2).
		std::vector<std::uint8_t> wrapper(3 + 33, 0); wrapper[0] = 0x01;
		check(valid_message(0x00, wrapper.data(), wrapper.size() - 1, stock) && !valid_message(0x00, wrapper.data(), wrapper.size() - 2, stock),
		      "D3 reliable wrapper: the inner opcode gets the same fixed-length validation");
		{
			// Captured/padded shapes that must pass on this lane exactly as on the stock lane (0.2.3h regressions).
			std::vector<std::uint8_t> sync(14, 0), movie(3, 0), mesh(6, 0);
			sync[0] = 0x2e; movie[0] = 0x59; movie[1] = 0x06; mesh[0] = 0x23;
			check(valid_message(0x00, sync.data(), 14, stock) && valid_message(0x2e, sync.data(), 12, stock) && valid_message(0x2b, sync.data(), 12, stock),
			      "shared table: wrapped 0x00 records and padded twelve-byte sync/menu bodies pass");
			check(valid_message(0x59, nullptr, 0, stock) && valid_message(0x00, movie.data(), 2, stock) && valid_message(0x59, sync.data(), 1, stock) &&
			          !valid_message(0x59, fixed.data(), 0x506, stock),
			      "shared table: the empty 0x59 movie-completion notification passes, extra bytes pass (0.2.3i), past the cap rejects");
			check(valid_message(0x23, sync.data(), 4, stock) && valid_message(0x25, nullptr, 0, stock) && valid_message(0x00, mesh.data(), 6, stock),
			      "shared table: the NAT mesh topology/completion messages (0x23, 0x25) pass");
		}
		wrapper.resize(3); wrapper[0] = 0;
		check(!valid_message(0x00, wrapper.data(), wrapper.size() - 1, stock), "D3 reliable wrapper: recursive wrappers reject");
	}
	// Teardown: a closed link holds nothing and reports no backlog.
	{
		outbox o;
		auto b = body(1, 8);
		o.enqueue(0x48, true, false, b.data(), b.size());
		sink s; o.flush(s);
		o.enqueue(0x48, true, false, b.data(), b.size());
		o.clear();
		inbox in; message m; m.opcode = 0x48; m.reliable = true; in.push(m); in.clear();
		check(o.count(0x48) == 0 && o.pending() == 0 && o.in_flight() == 0 && in.size() == 0, "teardown: buffers and the in-flight record cleared");
	}
	// Framing and the hello.
	{
		message m; m.opcode = 0x2b; m.body = body(7, 5);
		const auto f = frame(m);
		const auto p = parse(f.data(), f.size());
		check(p.valid && p.k == kind::game && p.opcode == 0x2b && p.length == 5 && p.body[4] == 7, "framing: a game message round-trips");
		message empty; empty.opcode = 0x09;
		const auto e = frame(empty);
		check(parse(e.data(), e.size()).valid && parse(e.data(), e.size()).length == 0, "framing: an empty body");
		const std::uint8_t one = 0;
		check(!parse(&one, 1).valid && !parse(nullptr, 0).valid, "framing: truncated payloads refused");
		const auto h = hello(kind::hello, 0x0110a8c0, 6500);
		const auto q = parse(h.data(), h.size());
		check(q.valid && q.k == kind::hello && q.address == 0x0110a8c0 && q.port == 6500, "hello: names the stock address and port");
		auto bad = h; bad[2] ^= 0xff;
		check(!parse(bad.data(), bad.size()).valid && !parse(h.data(), h.size() - 1).valid, "hello: wrong magic or size refused");
		const std::uint8_t keep = std::uint8_t(kind::keepalive), keep_long[2]{keep, 0};
		check(parse(&keep, 1).valid && parse(&keep, 1).k == kind::keepalive && !parse(keep_long, 2).valid, "keepalive: one byte, nothing else");
		check(fragment_opcode(0x12) && fragment_opcode(0x13) && !fragment_opcode(0x48), "fragments: 0x12/0x13 are the refused legacy opcodes");
	}
	// Exactly one side connects.
	{
		const std::uint32_t a = 0xac1f1001, b = 0xac1f1002;
		check(connects(a, 6500, b, 6500) != connects(b, 6500, a, 6500), "pairing: two addresses - one connector");
		check(connects(a, 6500, a, 6501) != connects(a, 6501, a, 6500), "pairing: one address, two ports - one connector");
		// Behind a port-preserving NAT each side sees itself at its private address and the other at a public one.
		check(connects(0xc0a801ec, 5165, 0x646e663b, 62082) != connects(0xac1f1001, 62082, 0x646e663b, 5165), "pairing: NAT-mapped addresses - still one connector (ports decide)");
		check(gns_transport_rules::gns_port_offset == 1000 && gns_port(5165, 1000) == 6165 && gns_port(64535, 1000) == 65535 && gns_port(64536, 1000) == 63536,
		      "pairing: GNS on the stock port + 1000 (- 1000 above 64535)");
	}
	// Rule 25 in the shared record: OFF adds nothing, ON vs OFF refuses naming 25.
	{
		using namespace gameplay_capabilities;
		auto make = [](bool gns, std::uint32_t offset = 1000) { record r; if (gns) r.add(gns_transport_id, 1, {1, offset, 1, 0}, true); r.finalize(); return r; };
		const auto off = make(false), off_too = make(false), on = make(true), on_too = make(true), other = make(true, 2000);
		record plain; plain.finalize();
		check(std::memcmp(off.bytes().data(), plain.bytes().data(), record_size) == 0, "rule 25: OFF is byte-identical to a record without it");
		check(on.compare(on_too.bytes()) == result::match && off.compare(off_too.bytes()) == result::match, "rule 25: ON-ON and OFF-OFF agree");
		difference d;
		check(on.compare(off.bytes(), &d) == result::entries, "rule 25: ON vs OFF refuses");
		char text[160];
		on.describe(d.buckets, d.local_only, true, text, sizeof(text));
		check(std::string(text) == "25 GnsTransport (not on the other PC)", "rule 25: the ON side names it");
		off.describe(d.buckets, 0, true, text, sizeof(text));
		check(std::string(text) == "25 GnsTransport (disabled on this PC)", "rule 25: the OFF side names it");
		check(on.compare(other.bytes()) == result::value, "rule 25: another port offset refuses");
		check(std::string(rule_name(gns_transport_id)) == "GnsTransport" && gns_transport_id == 25, "rule 25: name and ID");
	}
	if (argc > 1)
	{
		std::vector<std::uint8_t> mapped;
		const bool loaded = image(argv[1], mapped);
		check(loaded, "retail image mapped");
		if (loaded)
		{
			// first_mismatch reads image + va - 0x400000: the mapped buffer starts at the image base.
			const auto* bad = gns_transport_rules::first_mismatch(mapped.data());
			check(bad == nullptr, bad ? bad->what : "every install guard matches the retail executable");
			const auto* original = mapped.data() + gns_transport_rules::enqueue_va - limits_rules::image_base;
			check(std::memcmp(original, gns_transport_rules::enqueue_original.data(), 7) == 0 &&
			      std::memcmp(mapped.data() + gns_transport_rules::direct_va - limits_rules::image_base, gns_transport_rules::direct_original.data(), 6) == 0 &&
			      std::memcmp(mapped.data() + gns_transport_rules::backlog_va - limits_rules::image_base, gns_transport_rules::backlog_original.data(), 6) == 0 &&
			      std::memcmp(mapped.data() + gns_transport_rules::pump_slot - limits_rules::image_base, gns_transport_rules::pump_original.data(), 4) == 0 &&
			      std::memcmp(mapped.data() + gns_transport_rules::parser_jump - limits_rules::image_base, gns_transport_rules::parser_original.data(), 5) == 0 &&
			      std::memcmp(mapped.data() + gns_transport_rules::flush_jump - limits_rules::image_base, gns_transport_rules::flush_original.data(), 5) == 0,
			      "the replaced bytes are whole retail instructions / the pump slot / the parser and flush jumps");
		}
	}
	{
		// A lost participant's cut frame (framing revision 3): the records and announcements survivors exchange.
		std::vector<std::uint8_t> recs(3 * input_record_size, 0x5a);
		for (int i = 0; i < 3; ++i) { const std::uint16_t f = std::uint16_t(0xfffe + i); std::memcpy(recs.data() + i * input_record_size, &f, 2); }
		const auto m = lost_inputs(2, 0xfffe, recs.data(), 3);
		const auto p = parse(m.data(), m.size());
		check(p.valid && p.k == kind::lost_inputs && p.index == 2 && p.frame == 0xfffe && p.count == 3 && p.length == recs.size() &&
		      std::memcmp(p.body, recs.data(), recs.size()) == 0, "lost inputs: index, first frame, records round-trip (frames wrap at 65536)");
		auto bad = m; bad[1] = 4;
		check(!parse(bad.data(), bad.size()).valid, "lost inputs: a session index outside the four slots is refused");
		bad = m; bad[lost_inputs_header + input_record_size] ^= 1;
		check(!parse(bad.data(), bad.size()).valid, "lost inputs: a record whose own frame is not consecutive is refused");
		bad = m; bad.pop_back();
		check(!parse(bad.data(), bad.size()).valid, "lost inputs: a truncated record is refused");
		bad = m; bad[4] = 2;
		check(!parse(bad.data(), bad.size()).valid, "lost inputs: a miscounted message is refused");
		const auto none = lost_inputs(1, 7, nullptr, 0);
		check(parse(none.data(), none.size()).valid && parse(none.data(), none.size()).count == 0, "lost inputs: a survivor without records still reports (count 0)");
		const auto r = lost_ready(1, 327);
		const auto q = parse(r.data(), r.size());
		check(q.valid && q.k == kind::lost_ready && q.index == 1 && q.frame == 327, "lost ready: index and announced frame round-trip");
		auto badr = r; badr.push_back(0);
		check(!parse(badr.data(), badr.size()).valid, "lost ready: a wrong size is refused");
		check(ring_slot(5, 100, 99) == 4 && ring_slot(0, 100, 99) == 299 && ring_slot(5, 100, 100) == -1 && ring_slot(5, 100, std::uint16_t(100 - 300)) == -1 &&
		      ring_slot(2, 1, 0xffff) == 0, "ring slot: earlier frames behind the write index, none for the next frame or 300 back");
		check(!may_announce(false, true, true, 0) && !may_announce(true, false, true, 0) && !may_announce(true, true, false, 100) &&
		      may_announce(true, true, true, 0) && may_announce(true, true, false, lost_inputs_wait_ms), "announce: only waiting with the lost one's ring run out, after the others' records");
		survivor_view two[2]{{true, 327}, {true, 327}};
		check(lost_decision(327, two, 2, 0) == lost_step::remove_agreed, "decision: every survivor announced the same frame - remove");
		two[1].ready = 326;
		check(lost_decision(327, two, 2, 100) == lost_step::wait && lost_decision(327, two, 2, lost_agree_timeout_ms) == lost_step::remove_timeout,
		      "decision: a different frame waits (a later announcement replaces it), then times out");
		check(lost_decision(-1, two, 0, 0) == lost_step::wait && lost_decision(40, two, 0, 0) == lost_step::remove_agreed, "decision: alone it removes once announced");
		check(gns_transport_rules::agreement_revision == 4 && gns_transport_rules::framing_revision == 3, "rule 25 revision 4 / framing 3: every loss takes the agreed cut frame");
		auto hex6 = [](const std::array<unsigned char, 6>& b) { char out[13]{}; for (int i = 0; i < 6; ++i) std::snprintf(out + i * 2, 3, "%02x", b[i]); return std::string(out); };
		bool notice_guarded = false, lost_guarded = false;
		for (const auto& g : gns_transport_rules::guards)
		{
			if (g.va == gns_transport_rules::native_drop_notice) notice_guarded = std::string(g.hex).rfind(hex6(gns_transport_rules::notice_original), 0) == 0;
			if (g.va == gns_transport_rules::native_lost) lost_guarded = std::string(g.hex).rfind(hex6(gns_transport_rules::lost_original), 0) == 0;
		}
		check(notice_guarded && lost_guarded && gns_transport_rules::notice_resume == gns_transport_rules::native_drop_notice + 6 && gns_transport_rules::lost_resume == gns_transport_rules::native_lost + 6,
		      "the removal filters' six entry bytes are the guarded retail bytes; each bridge resumes right after them");
	}
	std::printf("%d PASS / %d FAIL\n", passes, failures);
	return failures ? 1 : 0;
}
