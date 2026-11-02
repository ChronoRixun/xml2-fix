// The dopples' shared fastball tick, executed: X-Men Legends 1's own code (default.xbe 0x110470..0x110538) runs against
// the same scenarios as dopple_rules::tick (src/dopple_rules.hpp). The bytes are read from the owner's default.xbe at
// run time and checked by SHA-256 before anything executes; no game code is embedded. Its five globals and two
// constants are relocated into the test; its six calls and its tail jump go to instrumented stand-ins (handle resolve,
// alive, distance, movement-target clear, navigation cancel). Does not launch any game.
//
// usage: xml2_dopple_xbe_test [<XMen2.exe>] <default.xbe>
#include "dopple_rules.hpp"
#include "sha256.hpp"

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <vector>

namespace
{
	using u8 = std::uint8_t;
	using u32 = std::uint32_t;
	int failures = 0, checks = 0;
	void check(bool ok, const char* what)
	{
		++checks;
		if (!ok) { ++failures; std::printf("FAIL %s\n", what); }
	}
	constexpr u32 begin = 0x110470, size = 0xc9;
	// SHA-256 of default.xbe 0x110470..0x110538 (the retail tick).
	constexpr std::array<u8, 32> identity{0xb5, 0xcb, 0x3a, 0x20, 0x05, 0xda, 0x15, 0x23, 0x8f, 0xac, 0x3f, 0x8e, 0x7a, 0xfc, 0xae, 0x0e,
	                                      0x81, 0xe6, 0x03, 0xf2, 0xaa, 0xc1, 0x98, 0xf6, 0xe1, 0x67, 0x36, 0x16, 0x59, 0x33, 0x24, 0xc6};
	u32 word(const std::vector<u8>& d, std::size_t o) { u32 v = 0; std::memcpy(&v, d.data() + o, 4); return v; }
	std::optional<std::vector<u8>> xbe_bytes(const std::vector<u8>& raw, u32 va, u32 n)
	{
		if (raw.size() < 0x124 || std::memcmp(raw.data(), "XBEH", 4)) return std::nullopt;
		const u32 image = word(raw, 0x104), count = word(raw, 0x11c), table = word(raw, 0x120);
		if (table < image || count > 64) return std::nullopt;
		for (u32 i = 0; i < count; ++i)
		{
			const std::size_t s = std::size_t(table - image) + i * 0x38;
			const u32 vaddr = word(raw, s + 4), offset = word(raw, s + 12), bytes = word(raw, s + 16);
			if (va >= vaddr && va - vaddr + n <= bytes && std::size_t(offset) + va - vaddr + n <= raw.size())
				return std::vector<u8>(raw.begin() + offset + (va - vaddr), raw.begin() + offset + (va - vaddr) + n);
		}
		return std::nullopt;
	}
	// The original's globals (0x4fedc4 re-arm, b4 Colossus flag, b0 Wolverine flag, a0 / a4 the two handles) and
	// constants (0x3ca9ec 400, 0x3c6de0 5).
	struct globals { float rearm; u32 colossus, wolverine, wolverine_handle, colossus_handle; float range, delay; } g;
	struct fake_actor { u8 body[0x40]; bool alive; int cleared, navigation; };
	std::array<fake_actor, 2> actors;
	float pair_distance = 0;
	void* __cdecl resolve(u32 handle) { return handle == 1 ? &actors[0] : handle == 2 ? &actors[1] : nullptr; }
	bool __fastcall alive(void* self, void*) { return static_cast<fake_actor*>(self)->alive; }
	float __fastcall distance(void*, void*, void*) { return pair_distance; }
	void __fastcall clear_target(void* self, void*) { ++static_cast<fake_actor*>(self)->cleared; }
	void __fastcall cancel_navigation(void* self, void*) { ++static_cast<fake_actor*>(self)->navigation; }
	using tick_t = void(__fastcall*)(void* brain, void*);
	tick_t prepare(std::vector<u8> code)
	{
		struct operand { u32 value; void* to; int uses; };
		const std::array<operand, 7> ops{{{0x4fedc4, &g.rearm, 2}, {0x4fedb4, &g.colossus, 2}, {0x4fedb0, &g.wolverine, 2},
		                                  {0x4feda0, &g.wolverine_handle, 1}, {0x4feda4, &g.colossus_handle, 1},
		                                  {0x3ca9ec, &g.range, 1}, {0x3c6de0, &g.delay, 1}}};
		for (const auto& op : ops)
		{
			int found = 0;
			for (u32 i = 0; i + 4 <= code.size(); ++i)
				if (word(code, i) == op.value)
				{
					const u32 to = static_cast<u32>(reinterpret_cast<std::uintptr_t>(op.to));
					std::memcpy(code.data() + i, &to, 4); ++found; i += 3;
				}
			if (found != op.uses) return nullptr;
		}
		u8* run = static_cast<u8*>(VirtualAlloc(nullptr, code.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		if (!run) return nullptr;
		struct branch { u32 at; u8 op; u32 target; const void* to; };
		const std::array<branch, 7> branches{{{0x1104b1, 0xe8, 0x293f0, reinterpret_cast<const void*>(&resolve)},
		                                      {0x1104c2, 0xe8, 0x293f0, reinterpret_cast<const void*>(&resolve)},
		                                      {0x1104d6, 0xe8, 0x29080, reinterpret_cast<const void*>(&alive)},
		                                      {0x1104e1, 0xe8, 0x29080, reinterpret_cast<const void*>(&alive)},
		                                      {0x1104f1, 0xe8, 0x12060, reinterpret_cast<const void*>(&distance)},
		                                      {0x110526, 0xe8, 0x34770, reinterpret_cast<const void*>(&clear_target)},
		                                      {0x110530, 0xe9, 0x345a0, reinterpret_cast<const void*>(&cancel_navigation)}}};
		for (const auto& b : branches)
		{
			const u32 i = b.at - begin;
			if (code[i] != b.op || b.at + 5 + word(code, i + 1) != b.target) return nullptr;
			const u32 rel = static_cast<u32>(reinterpret_cast<std::uintptr_t>(b.to)) - static_cast<u32>(reinterpret_cast<std::uintptr_t>(run + i + 5));
			std::memcpy(code.data() + i + 1, &rel, 4);
		}
		std::memcpy(run, code.data(), code.size());
		DWORD old = 0;
		if (!VirtualProtect(run, code.size(), PAGE_EXECUTE_READ, &old) || !FlushInstructionCache(GetCurrentProcess(), run, code.size())) return nullptr;
		return reinterpret_cast<tick_t>(run);
	}
}

int main(int argc, char** argv)
{
	// The XBE is whichever argument starts with "XBEH" (run_native passes <XMen2.exe> <default.xbe>).
	std::vector<u8> raw;
	for (int i = 1; i < argc && raw.empty(); ++i)
	{
		std::ifstream f(argv[i], std::ios::binary);
		std::vector<u8> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		if (data.size() >= 4 && !std::memcmp(data.data(), "XBEH", 4)) raw = std::move(data);
	}
	if (raw.empty()) { std::printf("  skip  supply the owned default.xbe\n"); return 0; }
	const auto code = xbe_bytes(raw, begin, size);
	const bool same = code && sha256_fips::digest(code->data(), code->size()) == identity;
	check(same, "default.xbe tick identity");
	if (!same) return 1;
	const auto tick = prepare(*code);
	check(tick != nullptr, "tick relocation (5 globals, 2 constants, 6 calls, 1 tail jump)");
	if (!tick) return 1;
	alignas(16) std::array<u8, 0x600> brain{};
	std::mt19937 random(0x0d0991e5u);
	int scenarios = 0, armed = 0;
	for (int n = 0; n < 20000; ++n)
	{
		// Times and the re-arm on 1/64 steps (exact in float and x87), flags 0..2, handles present or not, distance
		// around the 400 boundary.
		const float now = static_cast<float>(random() % 1024) / 64.f, rearm = static_cast<float>(random() % 1024) / 64.f;
		const u32 w = random() % 3 == 0 ? random() % 3 : 0, c = random() % 3 == 0 ? random() % 3 : 0;
		const bool w_present = random() % 8 != 0, c_present = random() % 8 != 0;
		const bool w_alive = random() % 6 != 0, c_alive = random() % 6 != 0;
		const float d = random() % 4 == 0 ? 400.f : static_cast<float>(random() % 51200) / 64.f;
		g = globals{rearm, c, w, w_present ? 1u : 0u, c_present ? 2u : 0u, 400.f, 5.f};
		actors = {};
		actors[0].alive = w_alive; actors[1].alive = c_alive; pair_distance = d;
		std::memcpy(brain.data() + 0x51c, &now, 4);
		tick(brain.data(), nullptr);
		dopple_rules::shared s;
		s.rearm_at = rearm; s.wolverine = static_cast<int>(w); s.colossus = static_cast<int>(c);
		const bool fired = dopple_rules::tick(s, now, w_present && c_present && w_alive && c_alive, d);
		const bool xbe_fired = actors[0].cleared == 1;
		armed += fired;
		++scenarios;
		const bool agree = fired == xbe_fired && static_cast<u32>(s.wolverine) == g.wolverine && static_cast<u32>(s.colossus) == g.colossus &&
		                   s.rearm_at == g.rearm && actors[0].navigation == actors[0].cleared && actors[1].cleared == 0 && actors[1].navigation == 0;
		if (!agree)
		{
			std::printf("FAIL scenario %d: now %.4f rearm %.4f flags %u/%u present %d/%d alive %d/%d d %.4f -> rules %d (%d/%d %.4f) xbe %d (%u/%u %.4f)\n",
			            n, now, rearm, w, c, w_present, c_present, w_alive, c_alive, d, fired, s.wolverine, s.colossus, s.rearm_at,
			            xbe_fired, g.wolverine, g.colossus, g.rearm);
			++failures;
			if (failures > 10) break;
		}
		++checks;
	}
	std::printf("  %d scenarios, %d armed fastballs, identical to default.xbe 0x110470\n", scenarios, armed);
	std::printf("Dopple tick (XBE): %d checks, %d failure(s)\n", checks, failures);
	return failures ? 1 : 0;
}
