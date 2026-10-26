// The save-core capture bridges EXECUTED as emitted (save_core_rules::capture_bridges, the exact block save_format
// writes), under the retail and the E10A2 native profiles. Each bridge jumps back into the capture function at fixed
// retail addresses; the test owns the image range 0x401000..0x801000 and puts a landing pad at every such address, so
// each run reports which native continuation the bridge chose, whether it called the overflow hook (and with which
// record), and what it wrote into a zone record laid out by the profile. Every bound immediate and every record
// displacement the bridges replay is checked this way (plan section 10, deviation 13). No game, no game content.
#include "save_core_rules.hpp"

#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" std::uint8_t fixture_space[0x400000];

// The register frame a bridge is entered with, and where it left.
std::uint32_t g_entry, g_eax, g_ecx, g_esi, g_ebp, g_landed, g_saved_esp, g_hook_calls, g_hook_arg;

namespace
{
	using save_format_rules::native_profile;
	int failures = 0, checks = 0;
	void check(bool ok, const std::string& what) { ++checks; std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str()); failures += !ok; }

	__declspec(naked) void landing()
	{
		__asm
		{
			mov dword ptr [g_landed], eax
			mov esp, dword ptr [g_saved_esp]
			pop edi
			pop esi
			pop ebx
			pop ebp
			ret
		}
	}
	__declspec(naked) void enter_bridge()
	{
		__asm
		{
			push ebp
			push ebx
			push esi
			push edi
			mov dword ptr [g_saved_esp], esp
			mov eax, dword ptr [g_eax]
			mov ecx, dword ptr [g_ecx]
			mov esi, dword ptr [g_esi]
			mov ebp, dword ptr [g_ebp]
			jmp dword ptr [g_entry]
		}
	}
	void __cdecl overflow_hook(void* record) { ++g_hook_calls; g_hook_arg = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(record)); }

	void* at(std::uint32_t va) { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(va)); }
	// mov eax, va; jmp landing
	void pad(std::uint32_t va)
	{
		auto* p = static_cast<std::uint8_t*>(at(va));
		p[0] = 0xb8; std::memcpy(p + 1, &va, 4);
		p[5] = 0xe9; const auto rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&landing) - (va + 10)); std::memcpy(p + 6, &rel, 4);
	}

	struct outcome { std::uint32_t landed, hooks, hook_arg; };
	outcome run(std::uint32_t entry, std::uint8_t* record, std::uint32_t eax, std::uint32_t ecx, std::uint32_t esi)
	{
		g_entry = entry; g_eax = eax; g_ecx = ecx; g_esi = esi; g_ebp = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(record));
		g_landed = 0; g_hook_calls = 0; g_hook_arg = 0;
		reinterpret_cast<void (*)()>(&enter_bridge)();
		return {g_landed, g_hook_calls, g_hook_arg};
	}
	std::uint32_t addr(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }
	std::uint16_t word_at(const std::uint8_t* r, std::uint32_t off) { std::uint16_t v; std::memcpy(&v, r + off, 2); return v; }

	void profile_run(const native_profile& p, const char* tag)
	{
		auto* code = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
		const auto base = addr(code);
		const auto block = save_core_rules::capture_bridges(base, addr(reinterpret_cast<const void*>(&overflow_hook)), p);
		std::memcpy(code, block.bytes.data(), block.bytes.size());
		FlushInstructionCache(GetCurrentProcess(), code, block.bytes.size());
		auto entry = [&](std::uint32_t va) -> std::uint32_t { for (const auto& j : block.jumps) if (j.va == va) return j.target; return 0u; };
		std::vector<std::uint8_t> storage(p.record_size + 0x100, 0);
		auto* record = storage.data();
		const auto rec = addr(record);
		const std::string t = std::string(tag) + ": ";
		auto pass = [&](const outcome& o, std::uint32_t where) { return o.landed == where && o.hooks == 0; };
		auto full = [&](const outcome& o, std::uint32_t where) { return o.landed == where && o.hooks == 1 && o.hook_arg == rec; };

		check(block.jumps.size() == 6, t + "six capture jumps (five bounds and the ordinal writer)");
		// 0x41a427: the zone string arena follows the installed profile
		check(pass(run(entry(0x41a427), record, 0, p.zone_arena - 1, 0), 0x41a42f) && full(run(entry(0x41a427), record, 0, p.zone_arena, 0), 0x41a44c), t + "0x41a427 string arena: arena-1 continues, arena overflows to 0x41a44c");
		// 0x466659: ECX = state count against the profile's state bound
		check(pass(run(entry(0x466659), record, 0, p.states - 1, 0), 0x466661) && full(run(entry(0x466659), record, 0, p.states, 0), 0x4666bc),
		      t + "0x466659 state count: states-1 continues at 0x466661, states (" + std::to_string(p.states) + ") overflows to 0x4666bc");
		// 0x46668a: EAX = dynamic count
		check(pass(run(entry(0x46668a), record, p.dynamic - 1, 0, 0), 0x466691) && full(run(entry(0x46668a), record, p.dynamic, 0, 0), 0x4666a6),
		      t + "0x46668a dynamic count: dynamic-1 continues, dynamic (" + std::to_string(p.dynamic) + ") overflows to 0x4666a6");
		// 0x4666e7: ESI = the state count again (bound = states, not the smart bound)
		check(pass(run(entry(0x4666e7), record, 0, 0, p.states - 1), 0x4666f3) && full(run(entry(0x4666e7), record, 0, 0, p.states), 0x4667a3),
		      t + "0x4666e7 state count: states-1 continues at 0x4666f3, states overflows to 0x4667a3");
		// 0x4666f3: the record's smart count at the profile's offset against the smart bound; the retail offset 0x5274
		// holds a decoy value that would give the opposite answer
		const auto sc = save_format_rules::smart_count(p);
		std::uint32_t v = p.smart - 1; std::memcpy(record + sc, &v, 4);
		if (sc != 0x5274) { const std::uint32_t decoy = p.smart; std::memcpy(record + 0x5274, &decoy, 4); }
		const auto below = run(entry(0x4666f3), record, 0, 0, 0);
		v = p.smart; std::memcpy(record + sc, &v, 4);
		if (sc != 0x5274) { const std::uint32_t decoy = 0; std::memcpy(record + 0x5274, &decoy, 4); }
		const auto at_bound = run(entry(0x4666f3), record, 0, 0, 0);
		check(pass(below, 0x466703) && full(at_bound, 0x4667a3), t + "0x4666f3 smart count read at +0x" + [&] { char b[16]; std::snprintf(b, sizeof b, "%x", sc); return std::string(b); }() +
		      ": smart-1 continues at 0x466703, smart (" + std::to_string(p.smart) + ") overflows to 0x4667a3");
		// 0x46667a: the ordinal writer stores CX at map + (ordinal-1)*2 for 1..ordinals, refuses ordinals+1
		std::fill(storage.begin(), storage.end(), std::uint8_t(0));
		std::vector<std::uint8_t> expected(storage.size(), 0);
		bool writes = true;
		for (const std::uint32_t ordinal : {1u, 2u, 255u, 256u, p.ordinals})
		{
			const std::uint16_t value = static_cast<std::uint16_t>(0x4000u + ordinal);
			const auto o = run(entry(0x46667a), record, ordinal, value, 0);
			std::memcpy(expected.data() + p.ordinal_map + (ordinal - 1) * 2, &value, 2);
			writes = writes && pass(o, 0x466682) && word_at(record, p.ordinal_map + (ordinal - 1) * 2) == value;
		}
		const bool only_map = storage == expected;
		const auto over = run(entry(0x46667a), record, p.ordinals + 1, 0x7777, 0);
		check(writes && only_map && full(over, 0x4666a6) && storage == expected,
		      t + "0x46667a ordinal writer: ordinals 1/2/255/256/" + std::to_string(p.ordinals) + " land in the ordinal map at +0x" + [&] { char b[16]; std::snprintf(b, sizeof b, "%x", p.ordinal_map); return std::string(b); }() +
		      " and nowhere else, ordinal " + std::to_string(p.ordinals + 1) + " overflows to 0x4666a6 without a write");
		VirtualFree(code, 0, MEM_RELEASE);
	}
}

int main()
{
	std::setbuf(stdout, nullptr);
	const auto b = reinterpret_cast<std::uintptr_t>(fixture_space);
	if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(0x400000) || b > 0x41a42f || b + sizeof fixture_space < 0x466800) { std::puts("fixture must cover 0x41a000..0x466800 at image base 0x400000"); return 2; }
	DWORD old = 0;
	VirtualProtect(fixture_space, sizeof fixture_space, PAGE_EXECUTE_READWRITE, &old);
	for (const std::uint32_t va : {0x41a42fu, 0x41a44cu, 0x466661u, 0x4666bcu, 0x466691u, 0x4666a6u, 0x4666f3u, 0x4667a3u, 0x466703u, 0x466682u}) pad(va);
	profile_run(save_format_rules::retail_profile, "retail");
	profile_run(save_format_rules::e10a2_profile, "E10A2");
	profile_run(save_format_rules::w2_profile, "W2");
	// the bytes the bridges replay are the retail instructions under the retail profile (the guard stays retail)
	bool retail_bytes = true;
	for (const auto& site : save_core_rules::capture_bounds)
	{
		const auto c = save_core_rules::capture_compare(site, save_format_rules::retail_profile);
		for (std::size_t i = 0; i < c.size(); ++i) retail_bytes = retail_bytes && c[i] == limits_rules::hex_byte(site.compare, i);
	}
	check(retail_bytes, "retail profile: every replayed compare is byte-identical to the retail instruction");
	std::printf("Save capture bridges: %d checks, %d failure(s)\n", checks, failures);
	return failures ? 1 : 0;
}
