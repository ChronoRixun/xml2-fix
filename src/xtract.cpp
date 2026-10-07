#include "xtract.hpp"

#include "ini.hpp"
#include "log.hpp"
#include "xtract_rules.hpp"

#include <cstdint>
#include <cstring>

namespace xtract
{
	namespace
	{
		using namespace xtract_rules;

		constexpr const char* as_before = "an Xtraction Point's menu offers the game's Xtract choice";

		const limits_rules::guard* first_xtract_mismatch()
		{
			__try
			{
				return first_mismatch(reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(image_base)));
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return &guards[0];
			}
		}
	}

	void install(const HMODULE game)
	{
		if (ini::flag(L"Game", L"Xtract", true))
		{
			return; // no key, or 1: the game's own menu
		}
		if (!game || reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("xtract: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", as_before);
			return;
		}
		if (const auto* g = first_xtract_mismatch())
		{
			logger::write("xtract: 0x%08lX isn't the retail code (%s) - %s", g->va, g->what, as_before);
			return;
		}
		auto* target = reinterpret_cast<void*>(static_cast<std::uintptr_t>(choice_block));
		DWORD old_protect = 0;
		if (!VirtualProtect(target, patched_jump.size(), PAGE_EXECUTE_READWRITE, &old_protect))
		{
			logger::write("xtract: ERROR: can't unprotect XMen2.exe's code at 0x%08lX (error %lu) - %s", static_cast<unsigned long>(choice_block), GetLastError(), as_before);
			return;
		}
		apply(reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(image_base)));
		VirtualProtect(target, patched_jump.size(), old_protect, &old_protect);
		FlushInstructionCache(GetCurrentProcess(), target, patched_jump.size());
		logger::write("xtract: no Xtract choice ([Game] Xtract=0): an Xtraction Point's menu offers its title, Change Team and Save only "
		              "(extractionPoint's Xtract block at 0x%08lX jumps over the choice to 0x%08lX)",
		              static_cast<unsigned long>(choice_block), static_cast<unsigned long>(next_choice));
	}
}
