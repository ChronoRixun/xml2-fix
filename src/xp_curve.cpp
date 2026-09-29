#include "xp_curve.hpp"

#include "ini.hpp"
#include "log.hpp"
#include "xp_curve_rules.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace xp_curve
{
	namespace
	{
		using namespace xp_curve_rules;

		constexpr const char* as_before = "XML2's own levels and kill XP stay";

		// The registry's vt+0xcc once patched: XMen2.exe calls it __thiscall with the victim's level pushed and pops
		// nothing itself (ret 4) - __fastcall takes `this` in ecx, ignores edx and pops the level the same way.
		int __fastcall xml1_kill_xp(void* /*registry*/, void* /*edx*/, const int level)
		{
			return kill_xp(level);
		}

		std::uint8_t* game_image()
		{
			return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(image_base));
		}

		DWORD address_of(const void* pointer)
		{
			return static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(pointer));
		}

		const guard* first_mismatch_guarded()
		{
			__try
			{
				return first_mismatch(game_image());
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return &guards[0];
			}
		}

		// Every write at once: the pages they are on are made writable first, all of them, so a refusal leaves the
		// code as it was; then every write; then the pages' protection goes back, in reverse order. Each page is
		// unprotected and restored exactly once (the sites share pages; see limits.cpp).
		bool patch(const std::vector<byte_write>& writes)
		{
			SYSTEM_INFO system{};
			GetSystemInfo(&system);
			const std::uintptr_t page_size = system.dwPageSize;
			std::vector<std::uintptr_t> pages;
			for (const auto& w : writes)
			{
				const auto first = reinterpret_cast<std::uintptr_t>(game_image() + (w.va - image_base));
				pages.push_back(first & ~(page_size - 1));
				pages.push_back((first + w.bytes.size() - 1) & ~(page_size - 1));
			}
			std::ranges::sort(pages);
			pages.erase(std::unique(pages.begin(), pages.end()), pages.end());

			std::vector<DWORD> protection(pages.size());
			for (std::size_t i = 0; i < pages.size(); ++i)
			{
				if (!VirtualProtect(reinterpret_cast<void*>(pages[i]), page_size, PAGE_EXECUTE_READWRITE, &protection[i]))
				{
					logger::write("xp curve: ERROR: can't unprotect XMen2.exe's code at 0x%08lX (error %lu) - %s", static_cast<unsigned long>(pages[i]), GetLastError(),
					              as_before);
					for (std::size_t j = i; j-- > 0;)
					{
						VirtualProtect(reinterpret_cast<void*>(pages[j]), page_size, protection[j], &protection[j]);
					}
					return false;
				}
			}
			apply(game_image(), writes);
			for (const auto page : pages)
			{
				FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(page), page_size);
			}
			for (std::size_t i = pages.size(); i-- > 0;)
			{
				VirtualProtect(reinterpret_cast<void*>(pages[i]), page_size, protection[i], &protection[i]);
			}
			return true;
		}
	}

	void install(const HMODULE game)
	{
		const auto narrow = ini::text(L"Game", L"XPCurve").value_or("");
		const auto chosen = parse_curve(narrow);
		if (!chosen.error.empty())
		{
			logger::write("xp curve: [Game] XPCurve=%s %s - %s", narrow.c_str(), chosen.error.c_str(), as_before);
			return;
		}
		if (!chosen.set)
		{
			return; // no key: the game's own curve
		}
		if (chosen.value == curve::xml2)
		{
			logger::write("xp curve: XML2's own ([Game] XPCurve=xml2) - nothing patched");
			return;
		}

		if (!game || reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("xp curve: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", as_before);
			return;
		}
		if (const guard* g = first_mismatch_guarded())
		{
			logger::write("xp curve: 0x%08lX isn't the retail code (%s) - %s", g->va, g->what, as_before);
			return;
		}

		addresses dll;
		dll.level_table = address_of(xml1_level_xp.data());
		dll.kill_xp_function = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&xml1_kill_xp));
		dll.far_teammate = address_of(&xp_curve_rules::far_teammate);
		dll.teammate_floor = address_of(&xp_curve_rules::teammate_floor);
		dll.teammate_falloff = address_of(&xp_curve_rules::teammate_falloff);
		const auto writes = writes_for(dll);
		if (!patch(writes))
		{
			return;
		}
		logger::write("xp curve: X-Men Legends 1's levels ([Game] XPCurve=xml1): XML1's XP table, levels 1-45 (2,000,000 XP is level %d; on XML2's table %d), "
		              "the cap 45 (was 99) in the level recompute, level-up-by-N, the shop's level-up item and the every-hero-to-the-top cheat, at most %lu XP",
		              level_for_xp(2000000), xml2_level_for_xp(2000000), static_cast<unsigned long>(max_xp()));
		logger::write("xp curve: X-Men Legends 1's kill XP: an enemy of level L is worth 2.5 x (4/3)^(L-1) XP (level 20: %d; XML2's (20 L + 80) x its party-level "
		              "multiplier dropped), half of it goes to every hero, the bench included (XML2 gives the bench 1 XP), and each hero in the party gets 3 x (half + 1) "
		              "more (an AI teammate that didn't make the kill: all of it next to the victim or the killer, down to a third at 300 units, nothing beyond) - %zu sites "
		              "patched; the Danger Room's fixed level 30 and Hard's start at 45 stay (both within the cap)",
		              kill_xp(20), writes.size());
	}
}
