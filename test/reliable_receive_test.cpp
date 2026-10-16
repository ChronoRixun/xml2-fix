// Execute the native datagram parser and reliable receiver against synthetic message pools.
// The local executable supplies the instructions; no executable bytes or game content are embedded.
#include "entity_transfer.hpp"
#include "patch_transaction.hpp"
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

extern "C" std::uint8_t fixture_space[0x700000];
namespace
{
int failures = 0, acknowledgements = 0, header_calls = 0, body_calls = 0, queued = 0;
std::vector<std::uint8_t> binary, manager(0x13800), peer(0x30c0), cache(0x149ac);
std::array<std::uint8_t, 20> node{}, header{};
std::array<std::uint8_t, 325> body{};
std::uint16_t queued_sequence = 0;
void check(bool ok, const char* text)
{
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", text);
	failures += !ok;
}
void* at(std::uint32_t va)
{
	return reinterpret_cast<void*>(va);
}
template <class T> T& field(std::vector<std::uint8_t>& v, std::size_t o)
{
	return *reinterpret_cast<T*>(v.data() + o);
}
void jump(std::uint32_t va, const void* target)
{
	auto* p = static_cast<std::uint8_t*>(at(va));
	p[0] = 0xe9;
	const auto rel = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)) - (va + 5);
	std::memcpy(p + 1, &rel, 4);
}
std::size_t offset(std::uint32_t va)
{
	auto u16 = [](std::size_t o) {
		if (o > binary.size() || binary.size() - o < 2)
			std::exit(2);
		std::uint16_t n;
		std::memcpy(&n, binary.data() + o, 2);
		return n;
	};
	auto u32 = [](std::size_t o) {
		if (o > binary.size() || binary.size() - o < 4)
			std::exit(2);
		std::uint32_t n;
		std::memcpy(&n, binary.data() + o, 4);
		return n;
	};
	const auto pe = u32(0x3c);
	const auto sections = std::size_t(pe) + 24 + u16(std::size_t(pe) + 20);
	if (u16(0) != 0x5a4d || u32(pe) != 0x4550 || sections + std::size_t(u16(std::size_t(pe) + 6)) * 40 > binary.size())
		std::exit(2);
	for (unsigned i = 0; i < u16(std::size_t(pe) + 6); ++i)
	{
		const auto row = sections + i * 40, rva = std::size_t(u32(row + 12));
		if (va - 0x400000 >= rva && va - 0x400000 < rva + u32(row + 16))
			return u32(row + 20) + va - 0x400000 - rva;
	}
	return binary.size();
}
void copy(std::uint32_t va, std::size_t n)
{
	const auto o = offset(va);
	if (o > binary.size() || n > binary.size() - o)
		std::exit(2);
	std::memcpy(at(va), binary.data() + o, n);
}
void* __cdecl cache_get()
{
	return cache.data();
}
void* __fastcall header_alloc(void*, void*)
{
	++header_calls;
	auto& used = field<std::uint32_t>(cache, 0x48d0);
	if (used >= 768)
		return nullptr;
	++used;
	header.fill(0);
	return header.data();
}
void* __fastcall body_alloc(void*, void*, unsigned n)
{
	++body_calls;
	if (n > 325)
		return nullptr;
	if (n > 60)
	{
		auto& used = field<std::uint32_t>(cache, 0x149a8);
		if (used >= 50)
			return nullptr;
		++used;
	}
	body.fill(0);
	return body.data();
}
bool __fastcall enqueue(void*, void*, void*, unsigned sequence)
{
	++queued;
	queued_sequence = static_cast<std::uint16_t>(sequence);
	return true;
}
bool __fastcall connected(void*, void*)
{
	return true;
}
void __fastcall cookie(unsigned)
{
}
int __fastcall dispatch(void*, void*, std::uint8_t* event)
{
	if (event[0] == 0)
		return reinterpret_cast<int(__cdecl*)(void*)>(at(0x6100b0))(event);
	if (event[0] == 0x0a)
		++acknowledgements;
	return 0;
}
void packet(unsigned expected, unsigned sequence, unsigned large, unsigned headers, unsigned bytes = 200,
			unsigned messages = 1)
{
	std::fill(manager.begin(), manager.end(), 0);
	std::fill(peer.begin(), peer.end(), 0);
	std::fill(cache.begin(), cache.end(), 0);
	node.fill(0);
	acknowledgements = header_calls = body_calls = queued = 0;
	field<std::uint32_t>(cache, 0x149a8) = large;
	field<std::uint32_t>(cache, 0x48d0) = headers;
	field<std::uint16_t>(peer, 0x308c) = static_cast<std::uint16_t>(expected);
	field<void*>(manager, 0x124b8) = node.data();
	field<unsigned>(manager, 0x124c4) = 1;
	*reinterpret_cast<void**>(node.data() + 0x10) = peer.data();
	auto* p = peer.data() + 0x52e;
	for (unsigned message = 0; message < messages; ++message)
	{
		*p++ = 0xc8; // reliable H, variable length, native sequence word, synthetic body
		*reinterpret_cast<std::uint16_t*>(p) = static_cast<std::uint16_t>(bytes + 2);
		p += 2;
		*reinterpret_cast<std::uint16_t*>(p) = static_cast<std::uint16_t>(sequence + message);
		p += 2;
		for (unsigned i = 0; i < bytes; ++i)
			*p++ = static_cast<std::uint8_t>(i * 13 + 7 + message);
	}
	*p++ = 0x0a;
	*reinterpret_cast<std::uint16_t*>(p) = 17;
	p += 2;
	field<void*>(peer, 0x1e98) = p;
	reinterpret_cast<void(__thiscall*)(void*)>(at(0x60dca0))(manager.data());
}
} // namespace

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		std::puts("Supply the locally owned retail executable.");
		return 2;
	}
	std::ifstream f(argv[1], std::ios::binary);
	binary.assign(std::istreambuf_iterator<char>(f), {});
	if (binary.size() < 4096)
		return 2;
	const auto start = reinterpret_cast<std::uintptr_t>(fixture_space);
	if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(0x400000) || start > 0x60d000 ||
		start + sizeof fixture_space < 0xa3e600)
		return 2;
	DWORD old = 0;
	if (!VirtualProtect(fixture_space, sizeof fixture_space, PAGE_EXECUTE_READWRITE, &old))
		return 2;
	copy(0x6100b0, 0x6101af - 0x6100b0);
	copy(0x60dca0, 0x60de27 - 0x60dca0);
	copy(0x6157a0, 0x70);
	// The ACK request has the native two-byte fixed body; H carries its variable body length on the wire.
	*reinterpret_cast<std::uint16_t*>(at(0xa3e490 + 0x0a * 2)) = 2;
	for (const auto& r : entity_capacity_rules::extra_regions)
		if (r.lo == 0x6100b0 || r.lo == 0x60dca0)
			check(patch_transaction::sha256(static_cast<const std::uint8_t*>(at(r.lo)), r.hi_exclusive - r.lo) ==
					  patch_transaction::sha256_of(r.sha256),
				  "native receiver/parser matches the production region guard");
	if (failures)
		return 2;
	jump(0x60f240, &cache_get);
	jump(0x60e740, &header_alloc);
	jump(0x60e910, &body_alloc);
	jump(0x60eac0, &enqueue);
	jump(0x60dbe0, &dispatch);
	jump(0x6106e0, &connected);
	jump(0x672161, &cookie);
	packet(100, 100, 50, 50);
	check(queued == 0 && acknowledgements == 1 && header_calls == 1 && body_calls == 1 &&
			  field<unsigned>(cache, 0x48d0) == 51,
		  "negative control: native full body pool drops a reliable message, leaks its header, then ACKs the datagram");
	packet(100, 101, 49, 49);
	check(queued == 1 && acknowledgements == 1 && field<unsigned>(cache, 0x149a8) == 50,
		  "negative control: native out-of-order message consumes the last body needed by the missing sequence");

	namespace pt = patch_transaction;
	pt::contribution patch;
	patch.owner = "synthetic reliable receive";
	patch.guards.push_back({0x6100e8, pt::bytes_of("53668b5e08"), "native reliable allocation entry"});
	patch.writes.push_back(pt::relocation(
		0x6100e8, "e9", true,
		static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&entity_transfer::reliable_receive_room)), 0,
		"receive room"));
	for (const auto& r : entity_capacity_rules::extra_regions)
		if (r.lo == 0x6100b0)
			patch.regions.push_back({r.name, r.lo, r.hi_exclusive, pt::sha256_of(r.sha256)});
	{
		static_cast<std::uint8_t*>(at(0x6100e8))[0] ^= 1;
		pt::transaction refused;
		refused.add(patch);
		check(refused.commit() == pt::outcome::refused_guard && *static_cast<std::uint8_t*>(at(0x6100e8)) == 0x52,
			  "changed entry refuses without writing");
		static_cast<std::uint8_t*>(at(0x6100e8))[0] ^= 1;
		static_cast<std::uint8_t*>(at(0x6100ed))[0] ^= 1;
		pt::transaction changed;
		changed.add(patch);
		check(changed.commit() == pt::outcome::refused_region,
			  "changed receiver body refuses even when the entry signature matches");
		static_cast<std::uint8_t*>(at(0x6100ed))[0] ^= 1;
	}
	{
		pt::hooks fault;
		fault.after_apply = []() { static_cast<std::uint8_t*>(at(0x6100e9))[0] ^= 0xff; };
		pt::transaction t;
		t.add(patch);
		check(t.commit(&fault) == pt::outcome::rolled_back,
			  "forced readback failure restores the native reliable entry");
	}
	pt::transaction t;
	t.add(patch);
	check(t.commit() == pt::outcome::committed, "production receive bridge installs with verified readback");
	packet(100, 101, 49, 49);
	check(queued == 0 && acknowledgements == 0 && header_calls == 0 && body_calls == 0 &&
			  field<unsigned>(cache, 0x149a8) == 49 && field<void*>(peer, 0x1e98) == peer.data() + 0x52e,
		  "out-of-order pressure: no allocation, no ACK, pending datagram batch cleared for retransmission");
	packet(100, 101, 48, 48, 200, 2);
	check(queued == 1 && queued_sequence == 101 && acknowledgements == 0 && header_calls == 1 && body_calls == 1 &&
			  field<unsigned>(cache, 0x149a8) == 49 && body[0] == 7 &&
			  body[199] == static_cast<std::uint8_t>(199 * 13 + 7),
		  "pressure midway through a datagram preserves the already queued payload and withholds the trailing ACK");
	packet(100, 100, 49, 49);
	bool exact = true;
	for (unsigned i = 0; i < 200; ++i)
		exact = exact && body[i] == static_cast<std::uint8_t>(i * 13 + 7);
	check(queued == 1 && queued_sequence == 100 && acknowledgements == 1 && exact &&
			  field<unsigned>(cache, 0x149a8) == 50,
		  "the missing in-order message uses the reserved body and preserves its exact bytes");
	packet(101, 101, 0, 0);
	check(queued == 1 && queued_sequence == 101 && acknowledgements == 1,
		  "after draining the gap, a retransmitted next message is admitted normally");
	packet(100, 101, 0, 767, 8);
	check(queued == 0 && acknowledgements == 0 && header_calls == 0,
		  "small out-of-order messages leave one header for the missing sequence");
	packet(100, 100, 0, 767, 8);
	check(queued == 1 && acknowledgements == 1 && field<unsigned>(cache, 0x48d0) == 768,
		  "an in-order small message may use the reserved header");
	packet(100, 100, 50, 50);
	check(queued == 0 && acknowledgements == 0 && header_calls == 0 && body_calls == 0,
		  "a completely full body pool defers before leaking a header or acknowledging data loss");
	packet(100, 100, 0, 768, 8);
	check(queued == 0 && acknowledgements == 0 && header_calls == 0,
		  "a completely full header pool defers before acknowledging data loss");
	packet(100, 101, 50, 10, 60);
	check(queued == 1 && acknowledgements == 1 && field<unsigned>(cache, 0x149a8) == 50,
		  "a 60-byte message uses the small-body pool even when large bodies are full");
	packet(100, 101, 49, 10, 61);
	check(queued == 0 && acknowledgements == 0 && header_calls == 0,
		  "a 61-byte future message preserves the last large-body slot");
	packet(100, 99, 50, 768);
	check(queued == 0 && acknowledgements == 1 && header_calls == 0,
		  "already delivered duplicates retain native ACK behavior without allocation");
	packet(0xffff, 0xffff, 49, 767);
	check(queued == 1 && acknowledgements == 1, "sequence wrap boundary still admits the expected message");
	packet(0, 1, 49, 49);
	check(queued == 0 && acknowledgements == 0, "out-of-order reserve also applies after sequence wrap");
	std::printf("Reliable receive: %d failure(s)\n", failures);
	return failures ? 1 : 0;
}
