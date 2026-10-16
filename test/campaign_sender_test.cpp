// [Online] FastTransfer: execute the retail campaign sender (0x607fb0) with the fast record bridge installed by a real
// protected transaction, against synthetic peers, shared pools and images, and feed every queued record through the
// production receiver (entity_transfer::receive). The executable is supplied locally; no executable bytes or game
// content are embedded. Optional further arguments name captured 2 MiB images to replay the same way.
#include "entity_capacity_rules.hpp"
#include "entity_transfer.hpp"
#include "patch_transaction.hpp"
#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

using namespace entity_capacity_rules;
extern "C" std::uint8_t fixture_space[0x700000];
struct Peer;
struct Connection
{
	void* unused{};
	Peer* endpoint{};
};
struct Record
{
	std::vector<std::uint8_t> body;
};
struct Peer
{
	Connection connection;           // +0
	std::uint32_t pad[4]{};          // +8
	std::uint32_t index{};           // +0x18: the native peer index (0x614210)
	unsigned queued{}, maximum{}, acks{}, records{};
	bool local{};
	std::deque<Record> in_flight;
	std::vector<std::uint8_t> wrapper;
	entity_transfer::state state;
};
struct Link
{
	std::uint32_t unused[2]{};
	Link* next{};
	std::uint32_t unused2{};
	Peer* peer{};
};
static Peer peers[4], host;
static Link links[4];
static int failures = 0, bad_records = 0, refusals = 0;
static std::vector<std::uint8_t> cache(0x149ac), node_cache(0x3dac);
template <class T> T& field(std::vector<std::uint8_t>& v, std::size_t o) { return *reinterpret_cast<T*>(v.data() + o); }
void check(bool ok, const std::string& message)
{
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", message.c_str());
	failures += !ok;
}
void* __cdecl session() { return peers; }
Link* __fastcall participants(void*, void*) { return links; }
bool __fastcall local(Peer* p, void*) { return p->local; }
Connection* __fastcall connection(Peer* p, void*) { return &p->connection; }
unsigned __fastcall queued(Connection* c, void*, unsigned op)
{
	if (op != 0x48) ++bad_records;
	return c->endpoint->queued;
}
Peer* __fastcall endpoint(Connection* c, void*) { return c->endpoint; }
bool __fastcall complete(Peer*, void*) { return false; }
void* __cdecl cache_get() { return cache.data(); }
void* __cdecl nodes_get() { return node_cache.data(); }
bool large_body(unsigned n) { return n > 60; }
bool __fastcall send_message(void*, void*, Peer* p, const std::uint8_t* h, unsigned flags)
{
	const auto n = *reinterpret_cast<const std::uint16_t*>(h + 8);
	const auto* data = *reinterpret_cast<const std::uint8_t* const*>(h);
	if (h[0x10] == 0x4a) { ++p->acks; return true; }
	if (h[0x10] != 0x48 || h[0xe] != 1 || flags != 0 || n < 2 || n > 300 || !data) ++bad_records;
	p->in_flight.push_back({std::vector<std::uint8_t>(data, data + n)});
	p->maximum = std::max(p->maximum, ++p->queued);
	++field<unsigned>(cache, large_body(n) ? 0x149a8 : 0x10948);
	++field<unsigned>(cache, 0x48d0);
	++field<unsigned>(node_cache, 0x3da8);
	return true;
}
void* at(unsigned va) { return reinterpret_cast<void*>(va); }
void jump(unsigned va, const void* fn)
{
	auto* p = static_cast<std::uint8_t*>(at(va));
	p[0] = 0xe9;
	const auto rel = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(fn)) - (va + 5);
	std::memcpy(p + 1, &rel, 4);
}
std::vector<std::uint8_t> binary;
unsigned ofs(unsigned va)
{
	auto u16 = [](unsigned o) {
		if (o > binary.size() || binary.size() - o < 2) std::exit(2);
		std::uint16_t n;
		std::memcpy(&n, binary.data() + o, 2);
		return n;
	};
	auto u32 = [](unsigned o) {
		if (o > binary.size() || binary.size() - o < 4) std::exit(2);
		unsigned n;
		std::memcpy(&n, binary.data() + o, 4);
		return n;
	};
	const auto pe = u32(0x3c), sections = pe + 24 + u16(pe + 20);
	if (u16(0) != 0x5a4d || u32(pe) != 0x4550 || sections > binary.size() ||
	    std::uint64_t(sections) + std::uint64_t(u16(pe + 6)) * 40 > binary.size())
		std::exit(2);
	for (unsigned i = 0; i < u16(pe + 6); ++i)
	{
		const auto r = sections + i * 40, rva = u32(r + 12);
		if (va - 0x400000 >= rva && va - 0x400000 < rva + u32(r + 16)) return u32(r + 20) + va - 0x400000 - rva;
	}
	return static_cast<unsigned>(binary.size());
}
void copy(unsigned va, unsigned n)
{
	const auto o = ofs(va);
	if (o > binary.size() || n > binary.size() - o) std::exit(2);
	std::memcpy(at(va), binary.data() + o, n);
}
void fast_refused(fast_verdict, std::uint32_t, std::uint32_t, std::uint32_t) { ++refusals; }

// The receiving side of one record: the production receiver, then the transport retires the message's pools.
void deliver(Peer& p, const transfer_profile& profile, std::vector<std::uint8_t>& receiver_context)
{
	auto record = std::move(p.in_flight.front());
	p.in_flight.pop_front();
	*reinterpret_cast<std::uint8_t**>(receiver_context.data() + 0x2250) = p.wrapper.data();
	std::uint8_t event[0x14]{};
	event[0] = 'H';
	*reinterpret_cast<std::uint8_t**>(event + 4) = record.body.data();
	*reinterpret_cast<std::uint16_t*>(event + 8) = static_cast<std::uint16_t>(record.body.size());
	*reinterpret_cast<std::uint8_t**>(event + 0xc) = receiver_context.data();
	static Connection back{nullptr, &host};
	*reinterpret_cast<Connection**>(event + 0x10) = &back;
	static const entity_transfer::observer observer{nullptr, nullptr, &fast_refused};
	const auto acks_before = host.acks;
	entity_transfer::receive(event, profile, p.state, observer);
	p.acks += host.acks - acks_before;
	--p.queued;
	--field<unsigned>(cache, large_body(static_cast<unsigned>(record.body.size())) ? 0x149a8 : 0x10948);
	--field<unsigned>(cache, 0x48d0);
	--field<unsigned>(node_cache, 0x3da8);
	++p.records;
}

std::vector<std::uint8_t> context;
void prepare(std::vector<std::uint8_t>& image, unsigned payload)
{
	entity_transfer::set_fast_payload(payload);
	context.assign(0x2300, 0);
	*reinterpret_cast<std::uint8_t**>(context.data() + 0x2250) = image.data();
	std::fill(cache.begin(), cache.end(), 0);
	std::fill(node_cache.begin(), node_cache.end(), 0);
	for (unsigned i = 0; i < 4; ++i)
	{
		peers[i] = {};
		peers[i].index = i;
		peers[i].local = i == 0;
		peers[i].connection.endpoint = &peers[i];
		peers[i].wrapper.assign(payload, 0xcd); // the game's pool does not zero the wrapper
		peers[i].state.fast = true;
		links[i].next = i < 3 ? &links[i + 1] : nullptr;
		links[i].peer = &peers[i];
		*reinterpret_cast<std::uint8_t**>(context.data() + 0x2240 + 4 * i) = image.data();
	}
}
using Sender = void(__cdecl*)(void*);
const auto sender = reinterpret_cast<Sender>(0x607fb0);

// Interleaved, partial acknowledgement until every remote received the image; returns the sender passes.
unsigned run(std::vector<std::uint8_t>& image, const transfer_profile& profile, std::mt19937& random)
{
	std::vector<std::uint8_t> receiver_context(0x2300, 0);
	unsigned passes = 0;
	while (peers[1].state.received < profile.payload || peers[2].state.received < profile.payload ||
	       peers[3].state.received < profile.payload || peers[1].queued || peers[2].queued || peers[3].queued)
	{
		sender(context.data());
		// Uneven progress: each peer's transport retires a random prefix of its in-flight records (loss = withheld ACK).
		for (unsigned i = 1; i < 4; ++i)
			for (auto n = random() % (peers[i].in_flight.size() + 1); n; --n) deliver(peers[i], profile, receiver_context);
		if (++passes > 200000) std::exit(3);
	}
	(void)image;
	return passes;
}
std::size_t expected_records(const std::vector<std::uint8_t>& image, unsigned payload)
{
	std::size_t n = 0;
	for (std::uint32_t at = 0; at < payload; ++n) at += fast_next(image.data(), at, payload).covered;
	return n;
}
void trial(const std::string& name, std::vector<std::uint8_t> image, const transfer_profile& profile, unsigned seed)
{
	prepare(image, profile.payload);
	std::mt19937 random(seed);
	bad_records = refusals = 0;
	sender(context.data());
	const auto first = std::min<std::size_t>(fast_window, expected_records(image, profile.payload));
	bool window = peers[0].in_flight.empty() && peers[1].queued == first && field<unsigned>(cache, 0x149a8) <= 50 - fast_large_margin;
	for (unsigned i = 1; i < 4; ++i) window = window && peers[i].queued <= fast_window;
	check(window, name + ": the first pass fills the first remote's window (" + std::to_string(first) + " records), the shared large pool keeps its margin (" +
	      std::to_string(peers[2].queued) + "/" + std::to_string(peers[3].queued) + " for the others), never the local player");
	const unsigned before[] = {peers[1].queued, peers[2].queued, peers[3].queued};
	sender(context.data());
	sender(context.data());
	// A peer the pool held back may top up later (a sparse image mixes small bodies), never beyond its window.
	check(peers[1].queued == before[0] && peers[2].queued >= before[1] && peers[3].queued >= before[2] && peers[2].queued <= fast_window &&
	      peers[3].queued <= fast_window && field<unsigned>(cache, 0x149a8) <= 50 - fast_large_margin,
	      name + ": withheld acknowledgements never overfill the window or the pool margin");
	const auto passes = run(image, profile, random);
	bool exact = !bad_records && !refusals;
	unsigned maximum = 0;
	for (unsigned i = 1; i < 4; ++i)
	{
		exact = exact && peers[i].wrapper == image && peers[i].acks == 1 && peers[i].records == expected_records(image, profile.payload);
		maximum = std::max(maximum, peers[i].maximum);
	}
	check(exact && maximum <= fast_window, name + ": three peers rebuild the exact image, one 'J' each, " + std::to_string(peers[1].records) +
	      " records (" + std::to_string(passes) + " sender passes, max in flight " + std::to_string(maximum) + ")");
	check(!field<unsigned>(cache, 0x149a8) && !field<unsigned>(cache, 0x48d0) && !field<unsigned>(cache, 0x10948) && !field<unsigned>(node_cache, 0x3da8),
	      name + ": every pool returns to zero");
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::puts("Supply the locally owned retail executable to exercise the sender.");
		return 2;
	}
	std::ifstream f(argv[1], std::ios::binary);
	binary.assign(std::istreambuf_iterator<char>(f), {});
	if (binary.size() < 4096) return 2;
	const auto fixture = reinterpret_cast<std::uintptr_t>(fixture_space);
	if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(0x400000) || fixture > 0x4692d0 || fixture + sizeof fixture_space < 0x615840)
		return 2;
	DWORD old = 0;
	if (!VirtualProtect(fixture_space, sizeof fixture_space, PAGE_EXECUTE_READWRITE, &old)) return 2;
	copy(0x607fb0, 0x60816b - 0x607fb0);
	copy(0x610950, 0x6109b3 - 0x610950);
	copy(0x614210, 4);
	copy(0x615810, 0x1f);

	// ---- the protected install: guards, region hashes, readback, rollback -------------------------------------
	namespace pt = patch_transaction;
	pt::contribution patch;
	patch.owner = "entity_capacity (FastTransfer)";
	for (const auto& r : fast_regions) patch.regions.push_back({r.name, r.lo, r.hi_exclusive, pt::sha256_of(r.sha256)});
	for (const auto& l : fast_literals)
	{
		patch.guards.push_back({l.va, pt::bytes_of(l.old_hex), l.role});
		patch.writes.push_back({l.va, pt::bytes_of(l.new_hex), l.role});
	}
	for (const auto& r : fast_relocations)
	{
		patch.guards.push_back({r.va, pt::bytes_of(r.old_hex), r.role});
		patch.writes.push_back(pt::relocation(r.va, r.prefix_hex, true, reinterpret_cast<std::uint32_t>(&entity_transfer::fast_sender), r.pad, r.role));
	}
	const auto stock = [] {
		return std::memcmp(at(0x608004), "\x8b\x7c\x24\x38\x8b\xb7\x50\x22\x00\x00", 10) == 0 &&
		       std::memcmp(at(0x61097b), "\xc7\x86\x2c\x21\x00\x00\xa0\x86\x01\x00", 10) == 0;
	};
	check(stock(), "retail sender chunk loop and peer budget signatures");
	{
		static_cast<std::uint8_t*>(at(0x608030))[0] ^= 1; // inside the hashed sender, outside the written bytes
		pt::transaction refused;
		refused.add(patch);
		check(refused.commit() == pt::outcome::refused_region && stock(), "a changed sender refuses before any write");
		static_cast<std::uint8_t*>(at(0x608030))[0] ^= 1;
	}
	{
		pt::hooks fault;
		fault.after_apply = [] { static_cast<std::uint8_t*>(at(0x61097f))[0] = 0x55; };
		pt::transaction rollback;
		rollback.add(patch);
		check(rollback.commit(&fault) == pt::outcome::rolled_back && stock(), "a failed readback rolls the bridge and the budget back together");
	}
	pt::transaction install;
	install.add(patch);
	check(install.commit() == pt::outcome::committed &&
	      *reinterpret_cast<std::uint32_t*>(at(0x610981)) == fast_peer_budget && static_cast<std::uint8_t*>(at(0x608004))[0] == 0xe9,
	      "FastTransfer bridge and 400,000-byte peer budget commit with verified readback");

	jump(0x612be0, &session);
	jump(0x611160, &participants);
	jump(0x6142a0, &local);
	jump(0x4692d0, &connection);
	jump(0x610790, &queued);
	jump(0x55d7e0, &endpoint);
	jump(0x610f30, &send_message);
	jump(0x614450, &complete);
	jump(0x60f240, &cache_get);
	jump(0x6067d0, &nodes_get);

	// ---- the record format on its own ---------------------------------------------------------------------------
	{
		std::uint32_t covered = 0;
		const std::uint8_t zero[] = {'Z', 0, 0, 0x20, 0}, empty[] = {'Z', 0, 0, 0, 0}, bad[] = {'X', 1};
		std::uint8_t data[301]{'D'};
		check(fast_chunk(0, zero, 5, 0x200000, true, covered) == fast_verdict::complete && covered == 0x200000, "a single zero record can cover the whole raised image");
		check(fast_chunk(1, zero, 5, 0x200000, true, covered) == fast_verdict::overflow && !covered, "a zero run past the payload end is refused");
		check(fast_chunk(0, empty, 5, 0x200000, true, covered) == fast_verdict::malformed && fast_chunk(0, zero, 4, 0x200000, true, covered) == fast_verdict::malformed &&
		      fast_chunk(0, bad, 2, 0x200000, true, covered) == fast_verdict::malformed && fast_chunk(0, data, 1, 0x200000, true, covered) == fast_verdict::malformed,
		      "empty runs, short zero records, unknown tags and empty data are malformed");
		check(fast_chunk(0, data, 300, 0x200000, true, covered) == fast_verdict::accept && covered == 299 &&
		      fast_chunk(0, data, 301, 0x200000, true, covered) == fast_verdict::malformed, "data records carry at most 299 bytes (300 with the tag: one body, no fragments)");
		check(fast_chunk(0, data, 5, 0x200000, false, covered) == fast_verdict::no_buffer, "no transfer buffer: nothing accepted");
		check(fast_sends(0, {}) == fast_window && fast_sends(30, {}) == 2 && fast_sends(40, {}) == 0, "the window bounds records in flight per peer");
		check(fast_sends(0, {41, 0, 0, 0}) == 1 && fast_sends(0, {42, 0, 0, 0}) == 0 && fast_sends(0, {50, 0, 0, 0}) == 0 &&
		      fast_sends(0, {0, 700, 0, 0}) == 4 && fast_sends(0, {0, 0, 0, 586}) == 0 && fast_sends(0, {0, 0, 768, 0}) == 0,
		      "every shared pool keeps its margin (8 large bodies, 64 headers/small bodies/nodes)");
	}

	// ---- whole transfers through the retail sender and the production receiver ------------------------------------
	std::mt19937 random(24);
	std::vector<std::uint8_t> sparse(e10a2_transfer.payload, 0);
	for (unsigned island = 0; island < 900; ++island)
	{
		const auto start = random() % (e10a2_transfer.payload - 2000);
		const auto length = 1 + random() % 700;
		for (unsigned i = 0; i < length; ++i) sparse[start + i] = static_cast<std::uint8_t>(random() % 3 ? random() & 0xff : 0u);
	}
	// Boundaries: zero runs of 63/64/65 bytes, a run straddling the 299-byte data limit, data at the very end.
	for (unsigned i = 0; i < 200; ++i) sparse[1000 + i] = 1, sparse[1263 + i] = 2, sparse[1527 + i] = 3, sparse[1792 + i] = 4;
	std::fill(sparse.begin() + 3000, sparse.begin() + 3299, std::uint8_t(9));
	std::fill(sparse.begin() + 3290, sparse.begin() + 3299, std::uint8_t(0));
	sparse.back() = 7;
	trial("sparse raised image", sparse, e10a2_transfer, 1);
	auto tail = sparse;
	tail.back() = 0;
	trial("raised image ending in a zero run", tail, e10a2_transfer, 2);
	std::vector<std::uint8_t> dense(e10a2_transfer.payload);
	for (std::size_t i = 0; i < dense.size(); ++i) dense[i] = static_cast<std::uint8_t>(1 + (i * 17 + i / 311) % 255);
	trial("dense raised image (no zero runs: the worst case)", dense, e10a2_transfer, 3);
	trial("all-zero raised image", std::vector<std::uint8_t>(e10a2_transfer.payload, 0), e10a2_transfer, 4);
	std::vector<std::uint8_t> retail(sparse.begin(), sparse.begin() + retail_transfer.payload);
	trial("retail-size image", retail, retail_transfer, 5);

	// ---- pool pressure: a crowded shared large pool pauses the sender; it resumes with no loss ---------------------
	{
		prepare(dense, e10a2_transfer.payload);
		field<unsigned>(cache, 0x149a8) = 50 - fast_large_margin;
		sender(context.data());
		check(peers[1].queued == 0 && peers[2].queued == 0 && peers[3].queued == 0, "large pool at its margin: no record is queued");
		field<unsigned>(cache, 0x149a8) = 50 - fast_large_margin - 3;
		sender(context.data());
		check(peers[1].queued == 3 && peers[2].queued == 0 && peers[3].queued == 0 && field<unsigned>(cache, 0x149a8) == 50 - fast_large_margin,
		      "three free bodies above the margin: three records, shared across peers in order");
		field<unsigned>(cache, 0x149a8) -= 50 - fast_large_margin - 3;
		bad_records = refusals = 0;
		run(dense, e10a2_transfer, random);
		bool exact = !bad_records && !refusals;
		for (unsigned i = 1; i < 4; ++i) exact = exact && peers[i].wrapper == dense && peers[i].acks == 1;
		check(exact, "after the pressure clears every peer completes with the exact image");
	}

	// ---- captured images (optional, local files) ---------------------------------------------------------------
	for (int i = 2; i < argc; ++i)
	{
		std::ifstream in(argv[i], std::ios::binary);
		std::vector<std::uint8_t> image(std::istreambuf_iterator<char>(in), {});
		if (image.size() != e10a2_transfer.payload) { check(false, std::string("captured image size: ") + argv[i]); continue; }
		std::printf("captured %s: %zu records instead of %u chunks\n", argv[i], expected_records(image, e10a2_transfer.payload), (e10a2_transfer.payload + 199) / 200);
		trial(std::string("captured image ") + std::to_string(i - 1), image, e10a2_transfer, 10 + i);
	}
	std::printf("Campaign sender fixture: %d failure(s)\n", failures);
	return failures ? 1 : 0;
}
