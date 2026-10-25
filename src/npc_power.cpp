#include "npc_power.hpp"

#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "npc_power_rules.hpp"
#include "npc_power_withdraw.hpp"
#include "npc_visibility_rules.hpp"
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
		std::vector<xml1_hook_transaction::patch> written; // the installed sites (retail -> rule bytes)
		void* bridges = nullptr;

		template<class T> T& field(void* object, const std::size_t offset)
		{
			return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
		}
		struct visibility_engine
		{
			using point = npc_visibility_rules::point;
			void* ai;
			void* target;
			explicit visibility_engine(void* brain) : ai(brain)
			{
				// The same physent class check as the stock visibility entry, with a by-value handle.
				target = reinterpret_cast<void* (__cdecl*)(std::uint32_t)>(0x4297b0)(field<std::uint32_t>(ai, 0x18));
			}
			bool has_target() const { return target != nullptr; }
			point actor_position() const { return field<point>(field<void*>(ai, 0x10), 0x20); }
			point target_position() const { return field<point>(target, 0x20); }
			npc_visibility_rules::query_result query(const point& from, const point& to, const point& extents)
			{
				void* hit = nullptr;
				std::uint8_t valid = 1;
				using query_fn = std::uint8_t (__fastcall*)(void*, void*, const point*, const point*, const point*, void**, int, std::uint8_t*, int);
				const bool served = reinterpret_cast<query_fn>(0x502730)(ai, nullptr, &from, &to, &extents, &hit, 1, &valid, 0) != 0;
				if (!served || !valid || !hit) return {served, false, false};
				// Trace record +0xc is the packed entity token in both layouts (entity_query's 64-byte records).
				// Comparing it directly avoids calling/guarding 0x4a9590, which the capacity owner already hooks.
				return {true, true, field<std::uint32_t>(hit, 0xc) == field<std::uint32_t>(target, 0x1c)};
			}
		};
		std::uint8_t __fastcall visibility_xml1(void* ai, void*, std::uint8_t* deferred)
		{
			visibility_engine engine(ai);
			return npc_visibility_rules::visible(engine, *deferred) ? 1 : 0;
		}

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
	std::array<std::uint32_t, 4> values() { return {installed() ? 1u : 0u, installed() ? 1u : 0u, 0, 0}; }
	bool withdraw()
	{
		if (!applied)
		{
			return true;
		}
		xml1_hook_transaction::memory memory;
		if (npc_power_withdraw::run(memory, written) != npc_power_withdraw::result::withdrawn)
		{
			// The rule's sites may still be installed without the weapon brain the configuration requires: that half
			// configuration must not run offline either (online is closed anyway), so startup is refused.
			game_version::refuse_startup();
			logger::write("npc power rule: withdrawing the sites failed (protection, write or readback) - NpcPowerRule=xml1 "
			              "would run without the requested NpcSemiAuto weapon brain; process startup refused");
			return false;
		}
		applied = false;
		written.clear();
		if (bridges)
		{
			VirtualFree(bridges, 0, MEM_RELEASE);
			bridges = nullptr;
		}
		logger::write("npc power rule: withdrawn - every site is retail again (%s)", as_before);
		return true;
	}

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
		std::memcpy(memory_block, code.data(), code.size()); // the data page (next press times) stays zero, read-write
		DWORD old = 0;
		if (!VirtualProtect(memory_block, code_size, PAGE_EXECUTE_READ, &old) || !FlushInstructionCache(GetCurrentProcess(), memory_block, code_size))
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
			{combo_exit_va, sub_bytes(combo_exit_site, 3, 6), combo_exit_jump(combo_exit_va, l.pre_start)},
			{combo_exit2_va, sub_bytes(combo_exit2_site, 3, 6), combo_exit_jump(combo_exit2_va, l.pre_start)},
			{combo_continue_va, retail_bytes(combo_continue_site, combo_continue_site.patched), continue_jump(l)},
			{combo_end_delay_site.va, retail_bytes(combo_end_delay_site, combo_end_delay_site.patched), end_delay_call(l)},
			{combo_buttons_va, retail_bytes(combo_buttons_site, 24), combo_buttons_bytes(l)},
			{combo_lengths_va, retail_bytes(combo_lengths_site, 24), combo_lengths_bytes()},
			{combo_row2_site.va, retail_bytes(combo_row2_site, combo_row_size), combo_row_bytes(2)},
			{combo_row3_site.va, retail_bytes(combo_row3_site, combo_row_size), combo_row_bytes(3)},
			{combo_row11_site.va, retail_bytes(combo_row11_site, combo_row_size), combo_row_bytes(6)},
			{query_priority_site.va, retail_bytes(query_priority_site, query_priority_site.patched), query_priority_bytes()},
			{query_budget_site.va, retail_bytes(query_budget_site, query_budget_site.patched), query_budget_bytes()},
			// Revision 10: a hit that makes an NPC react spends the combo steps that come due before its brain runs again.
			{reaction_site.va, retail_bytes(reaction_site, reaction_site.patched), mark_jump(l)},
			{power_entry_site.va, retail_bytes(power_entry_site, power_entry_site.patched), power_entry_jump(l)},
			{attack_entry_site.va, retail_bytes(attack_entry_site, attack_entry_site.patched), attack_entry_jump(l)},
			// Revision 10: a block is a counted to-hit miss, as in XML1 - a power attack is never rolled, and the
			// receive's block_chance roll never zeroes a hit.
			{hit_roll_site.va, retail_bytes(hit_roll_site, hit_roll_site.patched), hit_roll_jump(l)},
			{block_chance_site.va, retail_bytes(block_chance_site, block_chance_site.patched), block_chance_skip()},
			{visibility_site.va, retail_bytes(visibility_site, visibility_site.patched),
			 visibility_jump(static_cast<address>(reinterpret_cast<std::uintptr_t>(&visibility_xml1)))},
			// Revision 10: the brain runs through locked moves, as XML1's: no attack-state input-lock exit, and the melee
			// brain's power decision and its picker don't refuse a locked body (the move's chain test still does).
			{lock_exit_va, sub_bytes(lock_exit_site, 10, 2), lock_exit_bytes()},
			{power_lock_va, sub_bytes(power_lock_site, 11, 6), power_lock_jump(l)},
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
		written = all;
		bridges = memory_block;
		logger::write("npc power rule: X-Men Legends 1's ([Game] NpcPowerRule=xml1): enemies pay for their powers (0x4318e7 tests the AI time as "
		              "default.xbe 0xcde95), the melee-range power decision re-checks every 2.0 s / 0.5 s (0x5188d8) and needs half of the "
		              "character's energy (0x518920); NPCs combo with the first game's levels and combos (0x505011, rows 0x6dd508) and pause after a combo start (0x51993a, 0x519b04); "
		              "AI visibility uses XML1's ray and query reservation/budget (0x505250, 0x5027bd, 0x5027e0); "
		              "combo presses and completion use XML1 delays (0x51920b, 0x5192cb); a hit reaction spends the steps that come due "
		              "during it, and a power re-check or attack decision that fell due then is spent, as XML1's brain does "
		              "(0x435b6e, 0x51920b, 0x518770, 0x518fd0); the attack-state update, the melee brain's power decision and its "
		              "picker run through locked moves as XML1's (0x512f0b, 0x5187c4, 0x50fe5c); "
		              "a power attack (damagescale not normal) is never rolled to hit and the hit's block_chance roll never "
		              "zeroes it, so only a counted melee miss is blocked, as default.xbe 0x5c240 / 0x5c965 (0x44f58e, 0x437b08); "
		              "bridges at 0x%08lX",
		              static_cast<unsigned long>(base));
	}
}
