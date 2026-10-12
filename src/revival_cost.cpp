#include "revival_cost.hpp"

#include "ini.hpp"
#include "log.hpp"
#include "revival_cost_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <algorithm>
#include <string>

namespace revival_cost
{
	namespace
	{
		using namespace revival_cost_rules;
		using getter_t = void*(__cdecl*)();
		using level_t = int(__fastcall*)(void*, void*);
		bool is_installed = false;
		int last_logged = -1;

		int highest_level()
		{
			__try
			{
				void* registry = reinterpret_cast<getter_t>(registry_getter)();
				if (!registry || *static_cast<DWORD*>(registry) != registry_vtable) return 1;
				return reinterpret_cast<level_t>(roster_level)(registry, nullptr);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return 1;
			}
		}

		// Replaces 0x4b8830 (thiscall, the hero in ecx, no stack arguments): the first game's 200 * f * f.
		int __fastcall price(void*, void*)
		{
			const int f = highest_level();
			const auto p = static_cast<int>(std::min<std::int64_t>(original_price(f), 0x7fffffff));
			if (p != last_logged)
			{
				last_logged = p;
				logger::write("revival cost: 200 * %d^2 = %d (the roster's highest level %d)", f, p, f);
			}
			return p;
		}
	}

	bool requested()
	{
		const auto v = ini::text(L"Game", L"RevivalCost");
		if (!v) return false;
		std::string s = *v;
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s == "xml1";
	}
	bool installed() { return is_installed; }
	bool verified() { return !requested() || is_installed; }

	void install(const HMODULE game)
	{
		if (!requested()) return;
		constexpr const char* kept = "the revival price stays XMen2.exe's (max(200, 2 * level^2))";
		if (reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base)
		{
			logger::write("revival cost: XMen2.exe isn't loaded at 0x400000 - %s", kept);
			return;
		}
		for (const auto& g : guards)
		{
			if (!limits_rules::matches(reinterpret_cast<const std::uint8_t*>(g.va), g.hex))
			{
				logger::write("revival cost: 0x%08lX isn't the retail code (%s) - %s", g.va, g.what, kept);
				return;
			}
		}
		const auto jump = jump_to(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&price)));
		const auto* at = reinterpret_cast<const std::uint8_t*>(cost);
		std::vector<xml1_hook_transaction::patch> writes{{cost, std::vector<unsigned char>(at, at + 5), std::vector<unsigned char>(jump.begin(), jump.end())}};
		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (xml1_hook_transaction::apply(memory, writes, &safe))
		{
			is_installed = true;
			logger::write("revival cost: [Game] RevivalCost=xml1 - a fallen hero costs 200 * (the roster's highest level)^2, the first game's price");
		}
		else
		{
			logger::write("revival cost: ERROR: the patch at 0x%08lX didn't take%s - %s", cost, safe ? "" : " and couldn't be undone", kept);
		}
	}
}
