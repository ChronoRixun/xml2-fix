#include "x1_leader.hpp"
#include "x1_leader_rules.hpp"

#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// Link the production installer against isolated settings/logger providers.
namespace ini
{
	bool requested = false;
	bool flag(const wchar_t*, const wchar_t*, bool) { return requested; }
}
namespace game_version
{
	void refuse_startup() {}
}
namespace logger
{
	const std::filesystem::path& module_dir()
	{
		static const std::filesystem::path empty;
		return empty;
	}
	void write(const char* fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		std::vprintf(fmt, args);
		std::printf("\n");
		va_end(args);
	}
}

// A read/write/execute section named to sort before .text, spanning both hook sites (0x43597b,
// 0x46e4ed) wherever the linker places it in the image.
#pragma section(".afixture", read, write, execute)
__declspec(allocate(".afixture")) std::uint8_t fixture_space[0x70000]{};

namespace
{
	const std::uintptr_t pain_site = 0x43597b, pain_resume = 0x435982, pain_suppress = 0x4359a9,
	                     pain_continue = 0x435984, pain_rejoin = 0x4359b0;
	const std::uintptr_t drop_site = 0x46e4ed, drop_resume = 0x46e4f4, drop_rejoin = 0x46e503;
	int failures;
	// Bumped by the marker at each rejoin: a run must reach it exactly once. A site that entered its
	// bridge with a call left the return address on the stack, and the rejoin's ret then went back
	// into the patched site and ran the tail a second time (the game would do the same from the
	// real function's epilogue) - this count is what catches that.
	volatile std::uint32_t rejoins;

	LONG WINAPI report_crash(EXCEPTION_POINTERS* ex)
	{
		std::printf("CRASH code=%08lX eip=%08lX esp=%08lX ebp=%08lX esi=%08lX edi=%08lX eax=%08lX ecx=%08lX edx=%08lX\n",
		            ex->ExceptionRecord->ExceptionCode, ex->ContextRecord->Eip, ex->ContextRecord->Esp,
		            ex->ContextRecord->Ebp, ex->ContextRecord->Esi, ex->ContextRecord->Edi, ex->ContextRecord->Eax,
		            ex->ContextRecord->Ecx, ex->ContextRecord->Edx);
		std::printf("stack:");
		for (unsigned i = 0; i < 48; ++i)
		{
			std::printf(" %08x", static_cast<unsigned>(
			                 *reinterpret_cast<const std::uint32_t*>(ex->ContextRecord->Esp + i * 4)));
		}
		std::printf("\nebp-chain:");
		for (unsigned i = 0; i < 24; ++i)
		{
			std::printf(" %08x", static_cast<unsigned>(
			                 *reinterpret_cast<const std::uint32_t*>(ex->ContextRecord->Ebp + i * 4)));
		}
		std::printf("\n");
		ExitProcess(3);
	}

	void check(bool ok, const char* msg)
	{
		std::printf("%s %s\n", ok ? "PASS" : "FAIL", msg);
		failures += !ok;
	}

	// inc dword ptr [rejoins]; ret
	void place_rejoin(const std::uintptr_t at)
	{
		auto* r = reinterpret_cast<std::uint8_t*>(at);
		r[0] = 0xff;
		r[1] = 0x05;
		const auto counter = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&rejoins));
		std::memcpy(r + 2, &counter, 4);
		r[6] = 0xc3;
	}

	// Retail bytes around both sites (from the exe): the test plus its kept branch, with a ret
	// marker at each rejoin so the runner returns after the branch's first effect.
	void place_native()
	{
		auto* p = reinterpret_cast<std::uint8_t*>(pain_site);
		const std::array<std::uint8_t, 7> test_edi{0xf6, 0x87, 0xdb, 0x03, 0x00, 0x00, 0x08};
		std::memcpy(p, test_edi.data(), test_edi.size());
		p[7] = 0x75; p[8] = static_cast<std::uint8_t>(pain_suppress - (pain_resume + 2)); // jne suppress
		auto* c = reinterpret_cast<std::uint8_t*>(pain_continue);
		const std::array<std::uint8_t, 11> cont{0x8b, 0x06, 0xf6, 0x80, 0x65, 0x07, 0x00, 0x00, 0x02, 0x74,
		                                        static_cast<std::uint8_t>(pain_rejoin - (pain_continue + 11))};
		std::memcpy(c, cont.data(), cont.size()); // mov eax,[esi]; test [eax+0x765],2; je rejoin
		*reinterpret_cast<std::uint8_t*>(pain_suppress) = 0x80; // and byte ptr [esi+0xac], 0xf7
		*reinterpret_cast<std::uint8_t*>(pain_suppress + 1) = 0xa6;
		*reinterpret_cast<std::uint32_t*>(pain_suppress + 2) = 0xac;
		*reinterpret_cast<std::uint8_t*>(pain_suppress + 6) = 0xf7;
		place_rejoin(pain_rejoin);

		auto* d = reinterpret_cast<std::uint8_t*>(drop_site);
		const std::array<std::uint8_t, 7> test_esi{0xf6, 0x86, 0xdb, 0x03, 0x00, 0x00, 0x08};
		std::memcpy(d, test_esi.data(), test_esi.size());
		d[7] = 0x74; d[8] = static_cast<std::uint8_t>(drop_rejoin - (drop_resume + 2)); // je rejoin
		auto* f = reinterpret_cast<std::uint8_t*>(drop_resume + 2);                     // test ebp,ebp
		f[0] = 0x85; f[1] = 0xed;
		f[2] = 0x7e; f[3] = static_cast<std::uint8_t>(drop_rejoin - (drop_resume + 6)); // jle rejoin
		const std::array<std::uint8_t, 6> set_bl{0xc6, 0x44, 0x24, 0x13, 0x01,            // mov byte[esp+0x13],1
		                                          0x8a};
		std::memcpy(f + 4, set_bl.data(), set_bl.size());
		f[10] = 0x5c; f[11] = 0x24; f[12] = 0x13;                                       // mov bl,[esp+0x13]
		place_rejoin(drop_rejoin);
	}

	// Runs the pain site with esi = the victim ctx (bit 3 of +0xac set) and edi = the attacker.
	__declspec(naked) void __stdcall run_pain(void* ctx, void* attacker, void* site)
	{
		__asm
		{
			push esi
			push edi
			mov esi, dword ptr [esp + 0xc]
			mov edi, dword ptr [esp + 0x10]
			call dword ptr [esp + 0x14]
			pop edi
			pop esi
			ret 0xc
		}
	}

	// Runs the drop site with esi = the corpse, ebp = the drop kind (> 0); returns bl. The 0x40-byte
	// local frame mirrors the real FUN_0046e490 frame so the site's [esp+0x13] local write lands in
	// our own frame, exactly like the game (the args sit above the frame).
	__declspec(naked) unsigned char __stdcall run_drop(void* corpse, int kind, void* site)
	{
		__asm
		{
			push esi
			push ebp
			sub esp, 0x40
			mov esi, dword ptr [esp + 0x4c]
			mov ebp, dword ptr [esp + 0x50]
			xor bl, bl
			call dword ptr [esp + 0x54]
			mov al, bl
			add esp, 0x40
			pop ebp
			pop esi
			ret 0xc
		}
	}

	// Each run must pass its rejoin exactly once (see rejoins).
	void pain_once(void* ctx, void* attacker)
	{
		rejoins = 0;
		run_pain(ctx, attacker, reinterpret_cast<void*>(pain_site));
		if (rejoins != 1)
		{
			check(false, "the pain site ran its tail once (the game's stack is left as it was)");
		}
	}
	unsigned char drop_once(void* corpse, const int kind)
	{
		rejoins = 0;
		const auto bl = run_drop(corpse, kind, reinterpret_cast<void*>(drop_site));
		if (rejoins != 1)
		{
			check(false, "the drop site ran its tail once (the game's stack is left as it was)");
		}
		return bl;
	}
}

int main()
{
	std::setbuf(stdout, nullptr);
	AddVectoredExceptionHandler(1, &report_crash);
	const auto game = reinterpret_cast<HMODULE>(0x400000);
	const auto begin = reinterpret_cast<std::uintptr_t>(fixture_space);
	if (GetModuleHandleW(nullptr) != game || pain_site < begin || drop_rejoin > begin + sizeof(fixture_space))
	{
		std::printf("Test fixture section does not cover the retail code addresses (begin=%p end=%p)\n",
		            static_cast<void*>(fixture_space),
		            static_cast<void*>(fixture_space + sizeof fixture_space));
		return 2;
	}
	DWORD fixture_protection;
	if (!VirtualProtect(fixture_space, sizeof(fixture_space), PAGE_EXECUTE_READWRITE, &fixture_protection))
	{
		return 2;
	}

	// The actors and victims: names invented (no game data). actor[0x35c] -> stats[0x150] = name.
	alignas(8) std::uint8_t native_actor[0x400]{}, listed_actor[0x400]{}, plain_actor[0x400]{},
	    no_stats_actor[0x400]{};
	alignas(8) std::uint8_t listed_stats[0x200]{}, plain_stats[0x200]{};
	std::strcpy(reinterpret_cast<char*>(listed_stats + x1_leader_rules::stats_name), "x1_sgt_red");
	std::strcpy(reinterpret_cast<char*>(plain_stats + x1_leader_rules::stats_name), "x1_pvt_green");
	*reinterpret_cast<void**>(listed_actor + x1_leader_rules::actor_stats) = listed_stats;
	*reinterpret_cast<void**>(plain_actor + x1_leader_rules::actor_stats) = plain_stats;
	*reinterpret_cast<void**>(no_stats_actor + x1_leader_rules::actor_stats) = nullptr;
	native_actor[x1_leader_rules::actor_flags] = x1_leader_rules::native_leader_bit;

	alignas(8) std::uint8_t victim[0x800]{};
	*reinterpret_cast<std::uint8_t*>(victim + 0xac) = 0x08; // the flinch flag the gate may clear
	*reinterpret_cast<void**>(victim) = victim;             // [esi]: the victim itself; +0x765 bit1 clear

	auto pain_flag = [&] { return *reinterpret_cast<std::uint8_t*>(victim + 0xac) & 0x08; };

	// 1. The key is off: every native byte stays, both sites run the original test.
	place_native();
	x1_leader::install(game);
	check(std::memcmp(reinterpret_cast<void*>(pain_site), "\xf6\x87\xdb\x03\x00\x00\x08", 7) == 0 &&
	          std::memcmp(reinterpret_cast<void*>(drop_site), "\xf6\x86\xdb\x03\x00\x00\x08", 7) == 0,
	      "absent key preserves every native byte");
	pain_once(victim, listed_actor);
	check(pain_flag() != 0, "negative control: the unpatched gate keeps the flinch");
	check(drop_once(listed_actor, 1) == 0, "negative control: the unpatched roller skips a listed corpse");

	// 2. The key is on: a guard mismatch refuses the patch and changes nothing.
	ini::requested = true;
	x1_leader::give_for_testing("# test roster\r\nx1_sgt_red\r\n");
	place_native();
	*reinterpret_cast<std::uint8_t*>(pain_site) ^= 1;
	const auto before = *reinterpret_cast<std::uint32_t*>(pain_site);
	x1_leader::install(game);
	check(*reinterpret_cast<std::uint32_t*>(pain_site) == before && !x1_leader::installed(),
	      "guard mismatch leaves the code unchanged");

	// 3. Wrong image base refuses too.
	place_native();
	x1_leader::install(reinterpret_cast<HMODULE>(0x400001));
	check(*reinterpret_cast<std::uint8_t*>(pain_site) == 0xf6 && !x1_leader::installed(),
	      "wrong image base refuses the patch");

	// 4. The happy path: both sites patched, only their seven bytes.
	place_native();
	std::printf("debug pain bytes: %02x %02x %02x %02x %02x %02x %02x\n",
	            *reinterpret_cast<std::uint8_t*>(pain_site), *reinterpret_cast<std::uint8_t*>(pain_site + 1),
	            *reinterpret_cast<std::uint8_t*>(pain_site + 2), *reinterpret_cast<std::uint8_t*>(pain_site + 3),
	            *reinterpret_cast<std::uint8_t*>(pain_site + 4), *reinterpret_cast<std::uint8_t*>(pain_site + 5),
	            *reinterpret_cast<std::uint8_t*>(pain_site + 6));
	std::printf("debug direct matches: %d\n",
	            static_cast<int>(limits_rules::matches(reinterpret_cast<const std::uint8_t*>(pain_site),
	                                                   x1_leader_rules::guards[0].hex)));
	x1_leader::install(game);
	check(x1_leader::installed() && x1_leader::verified(), "installed and verified");
	const auto patched = *reinterpret_cast<std::uint8_t*>(pain_site) == 0xe9 &&
	                     *reinterpret_cast<std::uint8_t*>(drop_site) == 0xe9 &&
	                     *reinterpret_cast<std::uint16_t*>(pain_site + 5) == 0x9090 &&
	                     *reinterpret_cast<std::uint16_t*>(drop_site + 5) == 0x9090;
	check(patched, "both sites carry the jmp + nops");
	// The pain gate: the flinch flag (ctx+0xac bit 3) is what the gate clears on suppress.
	*reinterpret_cast<std::uint8_t*>(victim + 0xac) = 0x08;
	std::printf("about to run_pain listed\n");
	pain_once(victim, listed_actor);
	std::printf("ran_pain listed\n");
	check(pain_flag() == 0, "a listed attacker's projectile suppresses the flinch");
	*reinterpret_cast<std::uint8_t*>(victim + 0xac) = 0x08;
	pain_once(victim, native_actor);
	check(pain_flag() == 0, "a native x3-flagged attacker's projectile still suppresses it (native path)");
	*reinterpret_cast<std::uint8_t*>(victim + 0xac) = 0x08;
	pain_once(victim, plain_actor);
	check(pain_flag() != 0, "an unlisted attacker's projectile keeps the flinch");
	*reinterpret_cast<std::uint8_t*>(victim + 0xac) = 0x08;
	pain_once(victim, no_stats_actor);
	check(pain_flag() != 0, "an attacker without stats keeps the flinch");
	*reinterpret_cast<std::uint8_t*>(victim + 0xac) = 0x08;
	pain_once(victim, nullptr);
	check(pain_flag() != 0, "no attacker keeps the flinch");

	// The drop roller: bl is the roller's leader flag.
	check(drop_once(plain_actor, 1) == 0, "an unlisted corpse falls back to the probability roll");
	check(drop_once(listed_actor, 1) != 0, "a listed corpse always rolls");
	check(drop_once(native_actor, 1) != 0, "a native x3-flagged corpse still rolls (native path)");
	check(drop_once(no_stats_actor, 1) == 0, "a corpse without stats falls back to the roll");
	check(drop_once(listed_actor, 0) == 0, "drop kind 0 is not a leader roll (the kind gate stands)");

	DWORD unused;
	VirtualProtect(fixture_space, sizeof(fixture_space), fixture_protection, &unused);
	// All checks are done. Skip the CRT's static-teardown walk with _exit: this test process patched
	// its own image text and carries a large read/write/execute fixture section, and the MSVC CRT's
	// atexit walker trips over that combination on the way out. The production DLL is never unloaded
	// before the game's ExitProcess (and its sidecar state is allocated for the process lifetime), so
	// the early exit is a test-harness detail, not a product behaviour.
	_exit(failures ? 1 : 0);
}
