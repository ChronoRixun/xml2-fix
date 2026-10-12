#include "rank_costs.hpp"

#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "rank_costs_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace rank_costs
{
	namespace
	{
		using namespace rank_costs_rules;

		bool wanted = false, applied = false, level_wanted = false, level_applied = false, stat_wanted = false, stat_applied = false;
		constexpr std::uintptr_t image_base = 0x400000;

		template <typename T>
		T game_function(const std::uint32_t va)
		{
			return reinterpret_cast<T>(static_cast<std::uintptr_t>(va));
		}

		// A talent by its id: the registry (0x4c05b0) and its vt+0x14 (0x4bdbe0) with a pointer to the id.
		void* talent_by_id(int id)
		{
			using registry_fn = void*(__cdecl*)();
			using find_fn = void*(__fastcall*)(void* registry, void* edx, int* id);
			void* registry = game_function<registry_fn>(0x4c05b0)();
			if (!registry)
				return nullptr;
			return (*reinterpret_cast<find_fn**>(registry))[5](registry, nullptr, &id);
		}

		// The cost of a talent's rank `index` (0-based): its <level> for that rank (0x4bdea0 counts the levels' `count`
		// as the game does) and that level's `cost`; 1 without either.
		int cost_of_talent(void* talent, int index)
		{
			if (!talent || index < 0 || index > 63)
				return 1;
			using level_fn = void*(__fastcall*)(void* talent, void* edx, unsigned index);
			using attribute_fn = const char*(__fastcall*)(void* node, void* edx, const char* name);
			if (!*reinterpret_cast<void**>(static_cast<std::uint8_t*>(talent) + 8))
				return 1;
			void* level = game_function<level_fn>(0x4bdea0)(talent, nullptr, static_cast<unsigned>(index));
			if (!level)
				return 1;
			return parse_cost(game_function<attribute_fn>(0x5649c0)(level, nullptr, "cost"));
		}

		int cost_of_id(int id, int index) { return cost_of_talent(talent_by_id(id & 0xffff), index); }

		void* block_of(void* stats)
		{
			using block_fn = void*(__fastcall*)(void* stats);
			return stats ? game_function<block_fn>(0x4692f0)(stats) : nullptr;
		}

		int points_of(void* block)
		{
			using points_fn = int(__fastcall*)(void* block);
			return static_cast<std::int16_t>(game_function<points_fn>(0x544a30)(block));
		}

		// A talent's total rank in a saved block (committed + added in the open menu, as 0x5f00e0 adds them); -1 when
		// the block doesn't have the talent.
		int total_rank(const void* block, int id)
		{
			if (!block)
				return -1;
			const auto* b = static_cast<const std::uint8_t*>(block);
			int count = 0;
			std::memcpy(&count, b + 0xb8, 4);
			if (count < 0 || count > 32)
				return -1;
			for (int i = 0; i < count; ++i)
			{
				const auto* e = b + 0x1c + 8 * i;
				std::uint16_t entry_id = 0;
				std::memcpy(&entry_id, e, 2);
				if (entry_id == static_cast<std::uint16_t>(id))
					return (e[2] & 0x3f) + (e[3] & 0x3f);
			}
			return -1;
		}

		// ---- what the stubs call (cdecl; the stubs keep every register the sites rely on) ----

		// The cost of `stats`' talent `id` at its total rank + offset (rank_costs_rules::moment).
		int __cdecl cost_at_stats(void* stats, int id, int offset)
		{
			const int total = total_rank(block_of(stats), id & 0xffff);
			return total < 0 ? 1 : cost_of_id(id, rank_index(total, static_cast<moment>(offset)));
		}

		// can-add (0x5f0590): ECX = the block, EBX = the talent, the total before the rank.
		int __cdecl can_pay_next(void* block, void* talent, int total)
		{
			return affordable(points_of(block), cost_of_talent(talent, rank_index(total, moment::next_rank))) ? 1 : 0;
		}

		// automatic spending: whether `stats` can pay for the next rank of talent `id`.
		int __cdecl can_pay_next_id(void* stats, int id)
		{
			void* block = block_of(stats);
			const int total = total_rank(block, id & 0xffff);
			if (total < 0)
				return 1; // not in the block: the game's own add refuses it; stock behaviour
			return affordable(points_of(block), cost_of_id(id, rank_index(total, moment::next_rank))) ? 1 : 0;
		}

		int __cdecl respec_refund_of(int id, int committed, int base)
		{
			void* talent = talent_by_id(id & 0xffff);
			return respec_refund(committed, base, [&](int index) { return cost_of_talent(talent, index); });
		}

		int __cdecl level_down_extra_of(int id, int index) { return level_down_extra(cost_of_id(id, index)); }

		// ---- stubs ----

		// 0x5f05ea in 0x5f0590, replacing CALL 0x544a30: ax = 1 when the hero can pay for the next rank, else 0
		// (the next TEST AX,AX / JLE refuses). The function keeps the total at [esp+0x14].
		__declspec(naked) void can_add_stub()
		{
			__asm {
				push ecx
				mov eax, [esp + 8 + 0x14]
				push eax
				push ebx
				push ecx
				call can_pay_next
				add esp, 12
				pop ecx
				ret
			}
		}

		// 0x5e5d86 in the skills screen, replacing DEC EDI; CALL 0x46dce0: EDI (the points) -= the added rank's cost,
		// then the game-object getter it called. EBP = the CStats, [esp+0x14] = the talent's id.
		__declspec(naked) void screen_add_stub()
		{
			__asm {
				movzx eax, word ptr [esp + 4 + 0x14]
				push -1
				push eax
				push ebp
				call cost_at_stats
				add esp, 12
				sub edi, eax
				mov eax, 0x46dce0
				jmp eax
			}
		}

		// 0x5e5dcf, replacing TEST AL,AL / JZ / INC EDI / MOV BL,1 / JMP / MOV BL,[esp+0x13]: the same branches, with
		// EDI += the removed rank's cost; the site jumps on to the common tail 0x5e5ddc.
		__declspec(naked) void screen_remove_stub()
		{
			__asm {
				test al, al
				jz not_removed
				movzx eax, word ptr [esp + 4 + 0x14]
				push 0
				push eax
				push ebp
				call cost_at_stats
				add esp, 12
				add edi, eax
				mov bl, 1
				ret
			not_removed:
				mov bl, byte ptr [esp + 4 + 0x13]
				ret
			}
		}

		// 0x5de6f8 in the dependency sweep, replacing CALL 0x544a30; MOV ECX,EDI; INC EAX: eax = points + the cost of
		// the rank about to be removed, ECX = EDI (the CStats). [esp+0x10] = the talent's id.
		__declspec(naked) void sweep_stub()
		{
			__asm {
				mov eax, 0x544a30
				call eax
				push eax
				movzx ecx, word ptr [esp + 8 + 0x10]
				push -1
				push ecx
				push edi
				call cost_at_stats
				add esp, 12
				pop ecx
				add eax, ecx
				mov ecx, edi
				ret
			}
		}

		// 0x43c7c8 in the automatic spending, replacing CALL 0x43cc40 (__thiscall, 2 arguments, ret 8): the pick; a
		// talent whose next rank the hero can't pay for ends the spending at the loop's own end (0x43c8a8), as running
		// out of points does. ESI = the CStats.
		__declspec(naked) void auto_pick_stub()
		{
			__asm {
				push dword ptr [esp + 8]
				push dword ptr [esp + 8]
				mov eax, 0x43cc40
				call eax
				cmp ax, 0xffff
				je picked
				push eax
				movzx ecx, ax
				push ecx
				push esi
				call can_pay_next_id
				add esp, 8
				test eax, eax
				pop eax
				jz cannot_pay
			picked:
				ret 8
			cannot_pay:
				add esp, 12
				mov eax, 0x43c8a8
				jmp eax
			}
		}

		// 0x43c80d, replacing CALL 0x544a30; MOV ECX,ESI; DEC EAX: eax = points - the added rank's cost, ECX = ESI.
		__declspec(naked) void auto_charge_stub()
		{
			__asm {
				mov eax, 0x544a30
				call eax
				push eax
				movzx ecx, word ptr [esp + 8 + 0x10]
				push -1
				push ecx
				push esi
				call cost_at_stats
				add esp, 12
				pop ecx
				sub ecx, eax
				mov eax, ecx
				mov ecx, esi
				ret
			}
		}

		// 0x5d2898 in the respec, replacing AND EAX,0x3f; SUB EAX,EBX; MOV EBP,EAX: EBP = the refund. AL = the entry's
		// committed byte, EBX = the base rank, ESI = the entry + 2, ECX = EDI (the CStats) on return.
		__declspec(naked) void respec_stub()
		{
			__asm {
				and eax, 0x3f
				movzx ecx, word ptr [esi - 2]
				push ebx
				push eax
				push ecx
				call respec_refund_of
				add esp, 12
				mov ebp, eax
				mov ecx, edi
				ret
			}
		}

		// 0x4bbd19 in the level-down, replacing MOV ECX,EDI; CALL 0x43b2f0 (__thiscall(&id, new rank), ret 8): EBP (the
		// missing points) -= the removed rank's cost - 1 (the loop's own DEC EBP takes the last one), then the setter.
		__declspec(naked) void level_down_stub()
		{
			__asm {
				mov eax, [esp + 4]
				movzx eax, word ptr [eax]
				movzx ecx, byte ptr [esp + 8]
				push ecx
				push eax
				call level_down_extra_of
				add esp, 8
				sub ebp, eax
				mov ecx, edi
				mov eax, 0x43b2f0
				jmp eax
			}
		}

		int __cdecl level_points_c(int level) { return level_skill_points(level); }
		int __cdecl level_loss_missing_c(int level, int points) { return level_loss(points, level).missing; }
		int __cdecl level_loss_points_c(int level, int points) { return level_loss(points, level).points_after; }

		// 0x4bbb68 in 0x4bbae0's level-up loop, replacing CALL 0x544a30; MOV ECX,ESI; INC EAX: eax = points + the
		// level's skill points (EDI = the level being given, ESI = the block), ECX = ESI.
		__declspec(naked) void level_gained_stub()
		{
			__asm {
				mov eax, 0x544a30
				call eax
				push eax
				push edi
				call level_points_c
				add esp, 4
				pop ecx
				add eax, ecx
				mov ecx, esi
				ret
			}
		}

		// 0x4bbc36 in the level-loss loop, replacing CALL 0x544a30 .. SUB EBP,ECX: eax = the unspent points after the
		// level's points are taken, EBP = what is still missing (taken back as ranks). ECX = EDI (the block) on entry;
		// [esp+0x10] = the level being taken back.
		__declspec(naked) void level_lost_stub()
		{
			__asm {
				mov eax, 0x544a30
				call eax
				movsx eax, ax
				push eax
				push eax
				push dword ptr [esp + 12 + 0x10]
				call level_loss_missing_c
				add esp, 8
				mov ebp, eax
				pop eax
				push eax
				push dword ptr [esp + 8 + 0x10]
				call level_loss_points_c
				add esp, 8
				ret
			}
		}

		int __cdecl stat_points_c(int level) { return level_stat_points(level); }
		int __cdecl stat_loss_missing_c(int level, int points) { return level_stat_loss(points, level).missing; }
		int __cdecl stat_loss_points_c(int level, int points) { return level_stat_loss(points, level).points_after; }

		// 0x4bbb56, replacing CALL 0x544a40; MOV ECX,ESI; ADD EAX,4: eax = stat points + the level's (EDI = the level).
		__declspec(naked) void stat_gained_stub()
		{
			__asm {
				mov eax, 0x544a40
				call eax
				push eax
				push edi
				call stat_points_c
				add esp, 4
				pop ecx
				add eax, ecx
				mov ecx, esi
				ret
			}
		}

		// 0x4bbc0f, replacing CALL 0x544a40 .. SUB ESI,ECX: eax = the stat points after the level's are taken, ESI = what
		// is still missing (taken from the stats). ECX = EDI (the block) on entry; [esp+0x10] = the level.
		__declspec(naked) void stat_lost_stub()
		{
			__asm {
				mov eax, 0x544a40
				call eax
				movsx eax, ax
				push eax
				push eax
				push dword ptr [esp + 12 + 0x10]
				call stat_loss_missing_c
				add esp, 8
				mov esi, eax
				pop eax
				push eax
				push dword ptr [esp + 8 + 0x10]
				call stat_loss_points_c
				add esp, 8
				ret
			}
		}

		std::uint32_t address_of(void (*f)()) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(f)); }

		const guard* first_mismatch()
		{
			for (const auto& g : guards)
			{
				__try
				{
					const auto* at = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(g.va));
					for (std::size_t i = 0; i < hex_size(g.hex); ++i)
						if (at[i] != hex_byte(g.hex, i))
							return &g;
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
					return &g;
				}
			}
			return nullptr;
		}
	}

	bool requested() { return wanted; }
	bool installed() { return applied; }

	bool level_requested() { return level_wanted; }
	bool level_installed() { return level_applied; }
	bool stat_installed() { return stat_applied; }

	xml1_capability_rules::registration capability()
	{
		if ((wanted && !applied) || (level_wanted && !level_applied) || (stat_wanted && !stat_applied))
			return {{0, 0, 0, 0}, false};
		const std::uint32_t parts = (applied ? 1u : 0u) | (level_applied ? 2u : 0u) | (stat_applied ? 4u : 0u);
		const std::uint32_t step = level_applied || stat_applied ? static_cast<std::uint32_t>(bonus_every) : 0u;
		const std::uint32_t points = (level_applied ? static_cast<std::uint32_t>(bonus_points) : 0u) |
		                             (stat_applied ? static_cast<std::uint32_t>(level_stat_points(bonus_every)) << 8 : 0u);
		return {{parts, applied ? static_cast<std::uint32_t>(max_cost) : 0u, step, points}, true};
	}

	void install(const HMODULE game)
	{
		if (applied || level_applied || stat_applied)
			return;
		wanted = ini::flag(L"Game", L"RankCosts", false);
		const auto level_rule = ini::text(L"Game", L"LevelSkillPoints");
		level_wanted = level_rule.has_value() && _stricmp(level_rule->c_str(), "xml1") == 0;
		const auto stat_rule = ini::text(L"Game", L"LevelStatPoints");
		stat_wanted = stat_rule.has_value() && _stricmp(stat_rule->c_str(), "xml1") == 0;
		if (!wanted)
			logger::write("rank costs: off - every talent rank costs 1 skill point (the game's own)");
		if (!level_wanted)
			logger::write("level skill points: off - 1 skill point per level (the game's own)");
		if (!stat_wanted)
			logger::write("level stat points: off - 4 stat points per level (the game's own)");
		if (!wanted && !level_wanted && !stat_wanted)
			return;
		const char* stock = "every rank costs 1 skill point and a level gives 1 (the game's own)";
		if (reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("rank costs: XMen2.exe isn't loaded at 0x400000 - %s", stock);
			return;
		}
		if (const guard* g = first_mismatch())
		{
			logger::write("rank costs: 0x%08lX isn't the retail code (%s) - %s", static_cast<unsigned long>(g->va), g->what, stock);
			return;
		}
		std::vector<xml1_hook_transaction::patch> writes;
		if (wanted)
		{
			const std::uint32_t stubs[site_count] = {
				address_of(can_add_stub), address_of(screen_add_stub), address_of(screen_remove_stub), address_of(sweep_stub),
				address_of(auto_pick_stub), address_of(auto_charge_stub), address_of(respec_stub), address_of(level_down_stub), 0};
			for (std::size_t i = 0; i < site_count; ++i)
				writes.push_back({sites[i].va, bytes_of(sites[i].before), patched(sites[i], stubs[i])});
		}
		if (level_wanted)
		{
			const std::uint32_t stubs[level_site_count] = {address_of(level_gained_stub), address_of(level_lost_stub)};
			for (std::size_t i = 0; i < level_site_count; ++i)
				writes.push_back({level_sites[i].va, bytes_of(level_sites[i].before), patched(level_sites[i], stubs[i])});
		}
		if (stat_wanted)
		{
			const std::uint32_t stubs[stat_site_count] = {address_of(stat_gained_stub), address_of(stat_lost_stub)};
			for (std::size_t i = 0; i < stat_site_count; ++i)
				writes.push_back({stat_sites[i].va, bytes_of(stat_sites[i].before), patched(stat_sites[i], stubs[i])});
		}
		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (xml1_hook_transaction::apply(memory, writes, &safe))
		{
			applied = wanted;
			level_applied = level_wanted;
			stat_applied = stat_wanted;
			if (applied)
				logger::write("rank costs: on - a talent rank costs its <level cost> (1 without one) in the skills screen, its "
				              "buyable/greyed test, the automatic spending, the respec and losing levels (%zu sites)", site_count);
			if (level_applied)
				logger::write("level skill points: xml1 - a level divisible by %d gives (and a level lost takes back) %d skill points, "
				              "any other level 1", bonus_every, bonus_points);
			if (stat_applied)
				logger::write("level stat points: xml1 - a level divisible by %d gives (and a level lost takes back) %d stat points, any other "
				              "level %d (X-Men Legends I's 2 / 1 on the port's x%d stat scale)", bonus_every, level_stat_points(bonus_every),
				              stat_points_per_level, stat_points_per_level);
		}
		else if (safe)
			logger::write("rank costs: the code transaction was refused - %s", stock);
		else
		{
			game_version::refuse_startup();
			logger::write("rank costs: ERROR: rollback could not be verified - process startup refused");
		}
	}
}
