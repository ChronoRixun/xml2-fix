#include "double_heal.hpp"

#include "double_heal_rules.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <atomic>
#include <cstdint>
#include <vector>

namespace double_heal
{
	namespace
	{
		using namespace double_heal_rules;
		std::atomic<int> state{-1}; // 1 off (patched); -1 the game's own; 0 asked for but nothing patched

		const guard* first_mismatch_guarded()
		{
			__try
			{
				return first_mismatch(reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(limits_rules::image_base)));
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return &guards[0];
			}
		}
	}

	void install(const HMODULE game)
	{
		if (ini::flag(L"Game", L"DoubleHeal", true))
		{
			return; // no key or 1: the game's own double heal
		}
		state = 0;
		if (!game || reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base)
		{
			logger::write("double heal: XMen2.exe isn't loaded at 0x400000 (not the game?) - packs heal double by the game's own chance");
			return;
		}
		if (const guard* g = first_mismatch_guarded())
		{
			logger::write("double heal: 0x%08lX isn't the retail code (%s) - packs heal double by the game's own chance", static_cast<unsigned long>(g->va), g->what);
			return;
		}
		const std::vector<xml1_hook_transaction::patch> writes{
			{health_branch, {retail_jne}, {patched_jmp}},
			{energy_branch, {retail_jne}, {patched_jmp}},
		};
		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (xml1_hook_transaction::apply(memory, writes, &safe))
		{
			state = 1;
			logger::write("double heal: off ([Game] DoubleHeal=0): restoreHealth (0x%08lX) and restoreEnergy (0x%08lX) always take the single amount and the pack's "
			              "own sound; the random number is still drawn",
			              static_cast<unsigned long>(health_branch), static_cast<unsigned long>(energy_branch));
		}
		else if (safe)
		{
			logger::write("double heal: the patches were refused (protection or readback) - packs heal double by the game's own chance");
		}
		else
		{
			game_version::refuse_startup();
			logger::write("double heal: rollback/protection verification failed; process startup refused");
		}
	}

	std::string status()
	{
		const int s = state.load();
		return std::string("double heal ") + (s == 1 ? "off" : s == -1 ? "game's own" : "unavailable");
	}

	bool requested()
	{
		return state.load() != -1;
	}

	bool installed()
	{
		return state.load() == 1;
	}
}
