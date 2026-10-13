#include "npc_power.hpp"

#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "npc_power_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <cstring>

namespace npc_power
{
	namespace
	{
		using namespace npc_power_rules;

		constexpr const char* as_before = "computer-controlled characters use powers as XMen2.exe has it (no energy cost, a decision every 0.3 s)";
		bool wanted = false, applied = false;

		const site* first_mismatch_guarded()
		{
			__try
			{
				return first_mismatch(reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(image_base)));
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return sites[0];
			}
		}
	}

	bool requested() { return wanted; }
	bool installed() { return wanted && applied; }
	bool verified() { return !wanted || applied; }
	std::array<std::uint32_t, 4> values() { return {installed() ? 1u : 0u, 0, 0, 0}; }

	void install(const HMODULE game)
	{
		if (applied)
		{
			return;
		}
		const auto narrow = ini::text(L"Game", L"NpcPowerRule").value_or("");
		const auto chosen = parse_rule(narrow);
		if (!chosen.error.empty())
		{
			wanted = true;
			logger::write("npc power rule: [Game] NpcPowerRule=%s %s - %s", narrow.c_str(), chosen.error.c_str(), as_before);
			return;
		}
		if (!chosen.set || chosen.value == rule::xml2)
		{
			if (chosen.set)
			{
				logger::write("npc power rule: XML2's own ([Game] NpcPowerRule=xml2) - nothing patched");
			}
			return;
		}
		wanted = true;
		if (!game || reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("npc power rule: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", as_before);
			return;
		}
		if (const site* s = first_mismatch_guarded())
		{
			logger::write("npc power rule: 0x%08lX isn't the retail code (%s) - %s", static_cast<unsigned long>(s->va), s->what, as_before);
			return;
		}
		auto* memory_block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, block_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		if (!memory_block)
		{
			logger::write("npc power rule: no memory for the bridges (error %lu) - %s", GetLastError(), as_before);
			return;
		}
		const auto base = static_cast<address>(reinterpret_cast<std::uintptr_t>(memory_block));
		const auto code = block(base);
		std::memcpy(memory_block, code.data(), code.size());
		DWORD old = 0;
		if (!VirtualProtect(memory_block, block_size, PAGE_EXECUTE_READ, &old) || !FlushInstructionCache(GetCurrentProcess(), memory_block, block_size))
		{
			logger::write("npc power rule: can't make the bridges executable (error %lu) - %s", GetLastError(), as_before);
			VirtualFree(memory_block, 0, MEM_RELEASE);
			return;
		}
		const auto l = layout_at(base);
		const auto picker_retail = retail_bytes(picker_site, 7); // push 4; call 0x50fe50: the call is the last 5
		const std::vector<xml1_hook_transaction::patch> all{
			{charge_site.va, retail_bytes(charge_site, charge_site.patched), charge_jump(l)},
			{recheck_site.va, retail_bytes(recheck_site, recheck_site.patched), recheck_call(l)},
			{picker_call_va, std::vector<std::uint8_t>(picker_retail.begin() + 2, picker_retail.end()), picker_call(l)},
			// The combos: no combolevel override, XML1's levels 2 / 3 / 6 in rows 2 / 3 / 11 over XML1's six combos in
			// slots 0..5, and the tail after a combo start.
			{combolevel_call_va, sub_bytes(combolevel_site, 3, 3), combolevel_skip()},
			{combo_exit_va, sub_bytes(combo_exit_site, 3, 6), combo_exit_jump(combo_exit_va)},
			{combo_exit2_va, sub_bytes(combo_exit2_site, 3, 6), combo_exit_jump(combo_exit2_va)},
			{combo_buttons_va, retail_bytes(combo_buttons_site, 24), combo_buttons_bytes(l)},
			{combo_lengths_va, retail_bytes(combo_lengths_site, 24), combo_lengths_bytes()},
			{combo_row2_site.va, retail_bytes(combo_row2_site, combo_row_size), combo_row_bytes(2)},
			{combo_row3_site.va, retail_bytes(combo_row3_site, combo_row_size), combo_row_bytes(3)},
			{combo_row11_site.va, retail_bytes(combo_row11_site, combo_row_size), combo_row_bytes(6)},
		};
		xml1_hook_transaction::memory memory;
		bool safe = true;
		applied = xml1_hook_transaction::apply(memory, all, &safe);
		if (!applied && safe)
		{
			VirtualFree(memory_block, 0, MEM_RELEASE);
			logger::write("npc power rule: writing the sites failed and every byte went back - %s", as_before);
			return;
		}
		if (!applied)
		{
			game_version::refuse_startup();
			logger::write("npc power rule: rollback/protection verification failed; process startup refused");
			return;
		}
		logger::write("npc power rule: X-Men Legends 1's ([Game] NpcPowerRule=xml1): enemies pay for their powers (0x4318e7 tests the AI time as "
		              "default.xbe 0xcde95), the melee-range power decision re-checks every 2.0 s / 0.5 s (0x5188d8) and needs half of the "
		              "character's energy (0x518920); NPCs combo with the first game's levels and combos (0x505011, rows 0x6dd508) and pause after a combo start (0x51993a, 0x519b04); bridges at 0x%08lX",
		              static_cast<unsigned long>(base));
	}
}
