// [Game] RankCosts: the cost rules, and the production install + stubs executed at the retail addresses against
// invented talents (the image fixture owns 0x401000..0x801000). Optional argument: a legally owned XMen2.exe, read
// only, to check every guard and site against the retail bytes (skipped without it).
#include "rank_costs.hpp"
#include "rank_costs_rules.hpp"

#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

extern "C" std::uint8_t fixture_space[0x400000];

// The production installer against isolated settings/logger/startup providers.
namespace ini
{
	bool requested = true;
	std::optional<std::string> level_rule, stat_rule;
	bool flag(const wchar_t*, const wchar_t*, bool) { return requested; }
	std::optional<std::string> text(const wchar_t*, const wchar_t* key)
	{
		return std::wstring(key) == L"LevelStatPoints" ? stat_rule : level_rule;
	}
}
namespace logger { void write(const char*, ...) {} }
namespace game_version { bool refused = false; void refuse_startup() { refused = true; } }

namespace
{
	using namespace rank_costs_rules;
	int failures = 0;
	void check(bool ok, const char* what)
	{
		std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
		failures += !ok;
	}
	std::uint8_t* at(std::uint32_t va) { return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(va)); }
	void put_hex(std::uint32_t va, std::string_view hex) { auto b = bytes_of(hex); std::memcpy(at(va), b.data(), b.size()); }
	void jump(std::uint32_t va, const void* to)
	{
		at(va)[0] = 0xe9;
		const auto rel = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(to)) - (va + 5);
		std::memcpy(at(va) + 1, &rel, 4);
	}

	// ---- invented talents: "aurora" (id 7, ranks 1, 6 and 11 cost 2) and "brook" (id 9, no costs) ----
	struct level { const char* cost; };
	struct talent { void* vtable; std::uint32_t pad; const void* node; level levels[12]; };
	talent aurora{nullptr, 0, &aurora, {{"2"}, {}, {}, {}, {}, {"2"}, {}, {}, {}, {}, {"2"}, {}}};
	talent brook{nullptr, 0, &brook, {}};
	struct fake_registry { void** vtable; } registry;
	void* __fastcall find_talent(void*, void*, int* id) { return (*id & 0xffff) == 7 ? &aurora : (*id & 0xffff) == 9 ? &brook : nullptr; }
	void* registry_vtable[8] = {nullptr, nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void*>(&find_talent), nullptr, nullptr};
	void* __fastcall level_node(talent* t, void*, unsigned index) { return index < 12 ? &t->levels[index] : nullptr; }
	const char* __fastcall attribute(level* l, void*, const char* name) { return std::strcmp(name, "cost") ? nullptr : l->cost; }

	// A CStats: the saved block at +4, its points at block+0x14, talents from block+0x1c, the count at block+0xb8.
	struct stats_image { std::uint8_t bytes[0x200]; };
	stats_image hero;
	std::uint8_t* block() { return hero.bytes + 4; }
	void set_points(int p) { const auto v = static_cast<std::int16_t>(p); std::memcpy(block() + 0x14, &v, 2); }
	int points() { std::int16_t v; std::memcpy(&v, block() + 0x14, 2); return v; }
	void set_talents(int committed7, int added7, int committed9)
	{
		std::memset(hero.bytes, 0, sizeof hero.bytes);
		const int count = 2;
		std::memcpy(block() + 0xb8, &count, 4);
		std::uint8_t* e = block() + 0x1c;
		e[0] = 7; e[2] = static_cast<std::uint8_t>(committed7); e[3] = static_cast<std::uint8_t>(added7 | 0x40);
		e[8] = 9; e[10] = static_cast<std::uint8_t>(committed9);
	}

	// ---- drivers: registers and frames as each retail site has them; the instruction after a site is a RET ----
	std::uint32_t r_eax, r_edi, r_ebp, r_ebx;
	void* stats_ptr = hero.bytes;
	void* block_ptr = hero.bytes + 4;
	int frame_id, frame_total, frame_byte13;

	// can-add 0x5f05e8 (MOV ECX,EBP; CALL): EBP = block, EBX = talent, [esp+0x14] = total; returns ax.
	void* can_add_talent;
	__declspec(naked) void drive_can_add()
	{
		__asm {
			pushad
			push frame_total
			push 0
			push 0
			push 0
			push 0
			mov ebp, block_ptr
			mov ebx, can_add_talent
			mov eax, 0x5f05e8
			call eax
			movzx eax, ax
			mov r_eax, eax
			add esp, 20
			popad
			ret
		}
	}
	// skills screen + / -: 0x5e5d86 or 0x5e5dcf with EBP = CStats, EDI = points, AL = the setter's answer,
	// [esp+0x14] = id, [esp+0x13] = the "changed" byte; returns EDI, BL.
	std::uint32_t screen_site;
	__declspec(naked) void drive_screen()
	{
		__asm {
			pushad
			push 0
			push frame_id
			mov eax, frame_byte13
			shl eax, 24
			push eax
			push 0
			push 0
			push 0
			mov ebp, stats_ptr
			mov edi, r_edi
			mov eax, r_eax
			xor ebx, ebx
			mov ecx, screen_site
			call ecx
			mov r_edi, edi
			mov r_ebx, ebx
			mov r_eax, eax
			add esp, 24
			popad
			ret
		}
	}
	// sweep 0x5de6f6 (MOV ECX,EAX; CALL): EAX = block, EDI = CStats, [esp+0x10] = id; returns EAX (points + cost).
	// auto charge 0x43c80b: the same with ESI = CStats; returns EAX (points - cost).
	__declspec(naked) void drive_refund_site()
	{
		__asm {
			pushad
			push 0
			push frame_id
			push 0
			push 0
			push 0
			mov eax, block_ptr
			mov edi, stats_ptr
			mov esi, stats_ptr
			mov ecx, screen_site
			call ecx
			mov r_eax, eax
			add esp, 20
			popad
			ret
		}
	}
	// auto pick 0x43c7c4 (PUSH EBX; PUSH ESI; MOV ECX,EDI; CALL): ESI = CStats; returns EAX (id, or the after-loop mark)
	std::uint16_t pick_answer;
	int __fastcall fake_pick(void*, void*, void*, int) { return pick_answer; }
	__declspec(naked) void drive_pick()
	{
		__asm {
			pushad
			mov esi, stats_ptr
			mov edi, 0
			mov ebx, 1
			mov eax, 0x43c7c4
			call eax
			mov r_eax, eax
			popad
			ret
		}
	}
	// respec 0x5d2892 (XOR EAX,EAX; MOV AL,[ESI]; MOV ECX,EDI; CALL): ESI = entry + 2, EBX = base; returns EBP.
	void* respec_entry;
	__declspec(naked) void drive_respec()
	{
		__asm {
			pushad
			mov esi, respec_entry
			mov ebx, r_ebx
			mov edi, stats_ptr
			mov eax, 0x5d2892
			call eax
			mov r_ebp, ebp
			popad
			ret
		}
	}
	// level-down 0x4bbd13 (LEA ECX,[esp+0x1c]; PUSH EAX; PUSH ECX; CALL): AL = the new committed rank, [esp+0x1c] = id,
	// EBP = missing points; runs the loop's DEC EBP / JG and returns 1 when it would loop again, EBP.
	int setter_rank, setter_calls;
	int __fastcall fake_setter(void*, void*, int* id, int rank) { setter_rank = rank; setter_calls += (*id & 0xffff) == 7; return 0; }
	__declspec(naked) void drive_level_down()
	{
		__asm {
			pushad
			sub esp, 0x20
			mov eax, frame_id
			mov [esp + 0x18], eax
			mov eax, r_eax
			mov ebp, r_ebp
			mov edi, block_ptr
			mov ecx, 0x4bbd13
			call ecx
			mov r_ebp, ebp
			mov r_eax, eax
			add esp, 0x20
			popad
			ret
		}
	}

	// a level gained, 0x4bbb66 (MOV ECX,ESI; CALL): ESI = block, EDI = level; returns EAX (points after)
	std::uint32_t r_level;
	__declspec(naked) void drive_level_gained()
	{
		__asm {
			pushad
			mov esi, block_ptr
			mov edi, r_level
			mov eax, 0x4bbb66
			call eax
			mov r_eax, eax
			popad
			ret
		}
	}
	// stats: a level gained 0x4bbb54 (MOV ECX,ESI; CALL), a level lost 0x4bbc0d (MOV ECX,EDI; CALL; ESI = missing)
	__declspec(naked) void drive_stat_gained()
	{
		__asm {
			pushad
			mov esi, block_ptr
			mov edi, r_level
			mov eax, 0x4bbb54
			call eax
			mov r_eax, eax
			popad
			ret
		}
	}
	std::uint32_t r_esi;
	__declspec(naked) void drive_stat_lost()
	{
		__asm {
			pushad
			push 0
			push r_level
			push 0
			push 0
			push 0
			mov edi, block_ptr
			mov esi, 0x5a5a
			mov eax, 0x4bbc0d
			call eax
			mov r_eax, eax
			mov r_esi, esi
			add esp, 20
			popad
			ret
		}
	}
	// a level lost, 0x4bbc34 (MOV ECX,EDI; CALL): EDI = block, [esp+0x10] = level; returns EAX (points), EBP (missing)
	__declspec(naked) void drive_level_lost()
	{
		__asm {
			pushad
			push 0
			push r_level
			push 0
			push 0
			push 0
			mov edi, block_ptr
			mov ebp, 0x5a5a
			mov eax, 0x4bbc34
			call eax
			mov r_eax, eax
			mov r_ebp, ebp
			add esp, 20
			popad
			ret
		}
	}

	bool retail_check(const char* path)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f)
			return false;
		std::vector<std::uint8_t> d((std::istreambuf_iterator<char>(f)), {});
		auto u32 = [&](std::size_t o) { std::uint32_t v; std::memcpy(&v, &d[o], 4); return v; };
		auto u16 = [&](std::size_t o) { std::uint16_t v; std::memcpy(&v, &d[o], 2); return v; };
		const std::uint32_t pe = u32(0x3c), n = u16(pe + 6), opt = u16(pe + 20);
		auto file_of = [&](std::uint32_t va) -> std::size_t {
			for (std::uint32_t i = 0; i < n; ++i)
			{
				const auto s = pe + 24 + opt + i * 40;
				const auto v = 0x400000 + u32(s + 12), size = u32(s + 16), raw = u32(s + 20);
				if (va >= v && va < v + size)
					return raw + (va - v);
			}
			return 0;
		};
		bool ok = true;
		for (const auto& g : guards)
		{
			const auto o = file_of(g.va);
			ok = ok && o && std::memcmp(&d[o], bytes_of(g.hex).data(), hex_size(g.hex)) == 0;
			if (!o || std::memcmp(&d[o], bytes_of(g.hex).data(), hex_size(g.hex)))
				std::printf("  guard 0x%08X differs: %s\n", g.va, g.what);
		}
		for (const auto& s : sites)
		{
			const auto o = file_of(s.va);
			if (!o || std::memcmp(&d[o], bytes_of(s.before).data(), hex_size(s.before)))
			{
				ok = false;
				std::printf("  site 0x%08X differs: %s\n", s.va, s.what);
			}
		}
		for (const auto& s : stat_sites)
		{
			const auto o = file_of(s.va);
			if (!o || std::memcmp(&d[o], bytes_of(s.before).data(), hex_size(s.before)))
			{
				ok = false;
				std::printf("  stat site 0x%08X differs\n", s.va);
			}
		}
		for (const auto& s : level_sites)
		{
			const auto o = file_of(s.va);
			if (!o || std::memcmp(&d[o], bytes_of(s.before).data(), hex_size(s.before)))
			{
				ok = false;
				std::printf("  level site 0x%08X differs: %s\n", s.va, s.what);
			}
		}
		return ok;
	}
}

int main(int argc, char** argv)
{
	std::setbuf(stdout, nullptr);

	// ---- rules ----
	check(parse_cost("2") == 2 && parse_cost(" 3 ") == 3 && parse_cost("7") == 7, "authored costs 2, 3, 7");
	check(parse_cost(nullptr) == 1 && parse_cost("") == 1 && parse_cost("0") == 1 && parse_cost("8") == 1 &&
	      parse_cost("2x") == 1 && parse_cost("-2") == 1 && parse_cost("99999") == 1, "absent or invalid cost is the stock 1");
	check(affordable(2, 2) && affordable(5, 1) && !affordable(1, 2) && !affordable(0, 1) && !affordable(-3, 1), "affordable needs the whole cost");
	check(rank_index(5, moment::next_rank) == 5 && rank_index(6, moment::after_add) == 5 && rank_index(5, moment::after_remove) == 5 &&
	      rank_index(6, moment::before_remove) == 5, "every moment prices the same rank (index 5)");
	auto xml1 = [](int i) { return (i == 0 || i == 5 || i == 10) ? 2 : 1; };
	check(respec_refund(11, 0, xml1) == 14 && respec_refund(6, 1, xml1) == 6 && respec_refund(1, 1, xml1) == 0 &&
	      respec_refund(0, 1, xml1) == -1, "respec refunds the costs above base (stock difference below it)");
	check(level_skill_points(4) == 1 && level_skill_points(5) == 2 && level_skill_points(9) == 1 && level_skill_points(10) == 2 &&
	      level_skill_points(45) == 2 && level_skill_points(1) == 1, "XML1: 2 skill points on levels 5, 10 .. 45, else 1");
	check(level_loss(3, 10).points_after == 1 && level_loss(3, 10).missing == 0 && level_loss(1, 10).points_after == 0 &&
	      level_loss(1, 10).missing == 1 && level_loss(0, 9).missing == 1 && level_loss(-2, 5).points_after == 0 &&
	      level_loss(-2, 5).missing == 4, "losing a level: its 1 / 2 points from the unspent word first (the game's signed min)");
	check(level_stat_points(4) == 4 && level_stat_points(5) == 8 && level_stat_points(10) == 8 && level_stat_points(11) == 4 &&
	      level_stat_loss(10, 5).points_after == 2 && level_stat_loss(10, 5).missing == 0 && level_stat_loss(3, 10).points_after == 0 &&
	      level_stat_loss(3, 10).missing == 5 && level_stat_loss(0, 9).missing == 4,
	      "stat points: 8 on levels divisible by 5, else 4 (XML1's 2 / 1 x the port's scale 4), taken back alike");
	check(level_down_remaining(1, 2) == -1 && level_down_remaining(1, 1) == 0 && level_down_extra(2) == 1 && level_down_extra(1) == 0,
	      "losing a level: a 2-point rank covers the missing point, the overshoot is not refunded (XML1)");
	bool layout = true;
	for (const auto& s : sites)
	{
		const auto p = patched(s, 0x10000000);
		layout = layout && p.size() == hex_size(s.before) && (s.kind == shape::jg ? p[0] == 0x0f && p[1] == 0x8f : p[0] == 0xe8);
		if (s.kind != shape::jg)
		{
			std::uint32_t rel;
			std::memcpy(&rel, &p[1], 4);
			layout = layout && s.va + 5 + rel == 0x10000000;
		}
	}
	const auto rm = patched(sites[screen_remove], 0x10000000);
	check(layout && rm[5] == 0xeb && 0x5e5dcf + 7 + rm[6] == 0x5e5ddc, "patch bytes: CALL rel32 to the stub, the - site jumps to 0x5e5ddc");

	// ---- the production install at the retail addresses ----
	const auto game = reinterpret_cast<HMODULE>(0x400000);
	const auto base = reinterpret_cast<std::uintptr_t>(fixture_space);
	if (GetModuleHandleW(nullptr) != game || base > 0x401000 || base + sizeof fixture_space < 0x801000)
	{
		std::puts("the image fixture does not cover the retail addresses");
		return 2;
	}
	DWORD old;
	VirtualProtect(fixture_space, sizeof fixture_space, PAGE_EXECUTE_READWRITE, &old);
	auto lay_retail = [] {
		for (const auto& g : guards)
			put_hex(g.va, g.hex);
		for (const auto& s : sites)
			put_hex(s.va, s.before);
		for (const auto& s : level_sites)
			put_hex(s.va, s.before);
		for (const auto& s : stat_sites)
			put_hex(s.va, s.before);
	};
	lay_retail();
	ini::requested = false;
	rank_costs::install(game);
	check(!rank_costs::installed() && rank_costs::capability().verified && rank_costs::capability().values[0] == 0 &&
	      std::memcmp(at(0x5e5d86), "\x4f\xe8", 2) == 0, "RankCosts off: nothing patched, rule 5 verified off");
	ini::requested = true;
	*at(0x544a31) ^= 0xff;
	rank_costs::install(game);
	check(!rank_costs::installed() && !rank_costs::capability().verified && at(0x5e5d86)[0] == 0x4f, "a guard mismatch patches nothing; requested but not installed is unverified");
	ini::requested = false; ini::level_rule = "xml1";
	*at(0x4bbb55) ^= 0xff;
	rank_costs::install(game);
	check(!rank_costs::level_installed() && !rank_costs::capability().verified && *at(0x4bbb68) == 0xe8 &&
	      at(0x4bbb68)[1] == 0xc3 && at(0x4bbb68)[2] == 0x8e, "LevelSkillPoints alone, guard mismatch: nothing patched, unverified");
	ini::requested = true; ini::stat_rule = "xml1";
	lay_retail();
	rank_costs::install(game);
	{
		const auto c = rank_costs::capability();
		check(rank_costs::installed() && rank_costs::level_installed() && rank_costs::stat_installed() && c.verified &&
		      c.values[0] == 7 && c.values[1] == 7 && c.values[2] == 5 && c.values[3] == (2u | 8u << 8) && !game_version::refused,
		      "all three installed: rule 5 revision 3 {7, 7, 5, 2 | 8 << 8}");
	}

	// The callees: the registry and attribute lookup become the fakes; the block getter and the points getter stay the
	// retail bytes (lea eax,[ecx+4] / mov ax,[ecx+0x14]); each site's next instruction returns to the driver.
	std::uint32_t registry_global = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&registry));
	registry.vtable = registry_vtable;
	std::memcpy(at(0x77339c), &registry_global, 4);
	jump(0x4bdea0, reinterpret_cast<const void*>(&level_node));
	jump(0x5649c0, reinterpret_cast<const void*>(&attribute));
	jump(0x43cc40, reinterpret_cast<const void*>(&fake_pick));
	jump(0x43b2f0, reinterpret_cast<const void*>(&fake_setter));
	put_hex(0x46dce0, "b834120000c3");          // mov eax, 0x1234 ; ret
	put_hex(0x43c8a8, "b8eeffc000c3");          // the after-loop mark: mov eax, 0xc0ffee ; ret
	*at(0x5f05ef) = 0xc3;
	*at(0x5e5d8b) = 0xc3;                         // after CALL + NOP (the getter's result)
	*at(0x5e5ddc) = 0xc3;                         // the screen's common tail
	*at(0x5de700) = 0xc3;
	*at(0x43c7cd) = 0xc3;
	*at(0x43c815) = 0xc3;
	*at(0x5d289f) = 0xc3;
	put_hex(0x4bbd27, "b801000000c3");          // the loop ends: eax = 1
	put_hex(0x4bbc95, "31c0c3");                // the loop goes round (JG): eax = 0
	*at(0x4bbb70) = 0xc3;                        // after the level-gained CALL + NOPs (PUSH EAX)
	*at(0x4bbb60) = 0xc3;                        // after the stat-gained CALL + NOPs (PUSH EAX)
	*at(0x4bbc2c) = 0xc3;                        // after the stat-lost CALL + NOPs (PUSH EAX)
	*at(0x4bbc53) = 0xc3;                        // after the level-lost CALL + NOPs (PUSH EAX)
	FlushInstructionCache(GetCurrentProcess(), nullptr, 0);

	// can-add: aurora at total 5 (next = rank 6, cost 2), brook at 3 (cost 1)
	set_talents(4, 1, 3);
	can_add_talent = &aurora; frame_total = 5;
	set_points(1); drive_can_add(); const auto refused = r_eax;
	set_points(2); drive_can_add(); const auto allowed = r_eax;
	can_add_talent = &brook; frame_total = 3; set_points(1); drive_can_add();
	check(refused == 0 && allowed == 1 && r_eax == 1, "can-add: 1 point can't buy a 2-point rank (greyed), 2 can; a 1-point rank with 1");
	can_add_talent = &aurora; frame_total = 11; set_points(5); drive_can_add();
	check(r_eax == 1, "can-add: past the authored levels a rank costs 1");

	// skills screen +: aurora's rank 6 just added (committed 5 + added 1) costs 2; brook's rank 4 costs 1
	set_talents(5, 1, 3);
	screen_site = 0x5e5d86; frame_id = 7; r_edi = 3; drive_screen();
	check(r_edi == 1 && r_eax == 0x1234, "screen +: a Legend rank takes 2 points (3 -> 1), the getter's result kept");
	set_talents(3, 1, 3); frame_id = 7; r_edi = 3; drive_screen();
	check(r_edi == 2, "screen +: an ordinary rank takes 1 (3 -> 2)");
	set_talents(0, 1, 0); frame_id = 7; r_edi = 2; drive_screen();
	check(r_edi == 0, "screen +: the Xtreme-style rank 1 (cost 2) takes 2");
	// skills screen -: aurora back from 6 to 5 (rank 6 removed) refunds 2; a refused remove keeps EDI and copies the byte
	set_talents(5, 0, 3);
	screen_site = 0x5e5dcf; frame_id = 7; r_edi = 1; r_eax = 1; drive_screen();
	check(r_edi == 3 && (r_ebx & 0xff) == 1, "screen -: removing a Legend rank returns 2 (1 -> 3)");
	frame_id = 7; r_edi = 1; r_eax = 0; frame_byte13 = 0x5a; drive_screen();
	check(r_edi == 1 && (r_ebx & 0xff) == 0x5a, "screen -: a refused remove changes nothing (the native else branch)");
	frame_byte13 = 0;

	// dependency sweep: aurora at total 6 is about to lose rank 6: points + 2
	set_talents(5, 1, 3); set_points(4);
	screen_site = 0x5de6f6; frame_id = 7; drive_refund_site();
	check(static_cast<std::int16_t>(r_eax) == 6, "dependency sweep: the removed Legend rank returns 2");
	// automatic spending: a rank just added (aurora total 6) costs 2
	set_talents(5, 1, 3); set_points(4);
	screen_site = 0x43c80b; frame_id = 7; drive_refund_site();
	check(static_cast<std::int16_t>(r_eax) == 2, "automatic spending: - the added Legend rank's 2");
	set_talents(5, 1, 3); set_points(4); frame_id = 9; drive_refund_site();
	check(static_cast<std::int16_t>(r_eax) == 3, "automatic spending: - 1 for an ordinary rank");
	// automatic spending's pick: aurora at total 5 (next costs 2) with 1 point ends the spending at the loop's end
	set_talents(4, 1, 3); set_points(1); pick_answer = 7; drive_pick();
	check(r_eax == 0xc0ffee, "automatic spending: 1 point and a 2-point next rank -> the loop's own end (slots pass)");
	set_points(2); drive_pick();
	check((r_eax & 0xffff) == 7, "automatic spending: 2 points buy it (the pick returns as usual)");
	pick_answer = 0xffff; set_points(0); drive_pick();
	check((r_eax & 0xffff) == 0xffff, "automatic spending: no pick stays the native exit");

	// respec: aurora committed 11 from base 0 refunds 14 (11 ranks, 3 of them 2-point); brook 3 from 1 refunds 2
	set_talents(11, 0, 3);
	respec_entry = block() + 0x1c + 2; r_ebx = 0; drive_respec();
	check(r_ebp == 14, "respec: 11 ranks with three 2-point ranks return 14");
	respec_entry = block() + 0x1c + 8 + 2; r_ebx = 1; drive_respec();
	check(r_ebp == 2, "respec: ordinary ranks return 1 each");

	// losing a level with 1 point missing: taking back aurora's rank 6 (new committed 5) covers it - no further rank
	frame_id = 7; r_ebp = 1; r_eax = 5; setter_calls = 0; drive_level_down();
	check(r_eax == 1 && r_ebp == 0xffffffffu && setter_calls == 1 && setter_rank == 5, "losing a level: a 2-point rank taken back covers the point; the loop stops");
	frame_id = 9; r_ebp = 1; r_eax = 2; drive_level_down();
	check(r_eax == 1 && r_ebp == 0, "losing a level: a 1-point rank covers 1");
	frame_id = 9; r_ebp = 2; r_eax = 2; drive_level_down();
	check(r_eax == 0 && r_ebp == 1, "losing a level: still missing -> the loop takes another rank");

	// levels gained: 4 -> 5 gives 2, 5 -> 6 gives 1, 9 -> 10 gives 2
	set_talents(0, 0, 0);
	set_points(3); r_level = 5; drive_level_gained();
	check(static_cast<std::int16_t>(r_eax) == 5, "level 5 gained: 3 -> 5 skill points");
	r_level = 6; drive_level_gained();
	check(static_cast<std::int16_t>(r_eax) == 4, "level 6 gained: 3 -> 4");
	set_points(0); r_level = 10; drive_level_gained();
	check(static_cast<std::int16_t>(r_eax) == 2, "level 10 gained: 0 -> 2");
	// levels lost: level 10 with 3 points -> 1, nothing missing; with 1 point -> 0, one missing; level 9 with 0 -> 1 missing
	set_points(3); r_level = 10; drive_level_lost();
	check(static_cast<std::int16_t>(r_eax) == 1 && r_ebp == 0, "level 10 lost with 3 points: 1 left, none missing");
	set_points(1); r_level = 10; drive_level_lost();
	check(static_cast<std::int16_t>(r_eax) == 0 && r_ebp == 1, "level 10 lost with 1 point: 0 left, 1 taken back as a rank");
	set_points(0); r_level = 9; drive_level_lost();
	check(static_cast<std::int16_t>(r_eax) == 0 && r_ebp == 1, "level 9 lost with 0 points: 1 taken back as a rank");

	// stat points (block+0x16): 4 -> 5 gives 8, 9 -> 10 gives 8, 10 -> 11 gives 4; level 10 lost with 3 points: 5 missing
	auto set_stat = [](int p) { const auto v = static_cast<std::int16_t>(p); std::memcpy(block() + 0x16, &v, 2); };
	set_stat(12); r_level = 5; drive_stat_gained();
	check(static_cast<std::int16_t>(r_eax) == 20, "level 5 gained: 12 -> 20 stat points");
	set_stat(0); r_level = 10; drive_stat_gained();
	check(static_cast<std::int16_t>(r_eax) == 8, "level 10 gained: 0 -> 8 stat points");
	r_level = 11; drive_stat_gained();
	check(static_cast<std::int16_t>(r_eax) == 4, "level 11 gained: 0 -> 4 stat points");
	set_stat(3); r_level = 10; drive_stat_lost();
	check(static_cast<std::int16_t>(r_eax) == 0 && r_esi == 5, "level 10 lost with 3 stat points: 0 left, 5 taken from the stats");
	set_stat(9); r_level = 7; drive_stat_lost();
	check(static_cast<std::int16_t>(r_eax) == 5 && r_esi == 0, "level 7 lost with 9 stat points: 5 left");

	VirtualProtect(fixture_space, sizeof fixture_space, old, &old);

	if (argc > 1)
		check(retail_check(argv[1]), "every guard and site is the retail executable's");
	else
		std::puts("SKIP retail byte check (no executable given)");
	std::printf("rank costs: %d failures\n", failures);
	return failures ? 1 : 0;
}
