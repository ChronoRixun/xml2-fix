#include "review_menu.hpp"

#include "ini.hpp"
#include "log.hpp"
#include "review_menu_rules.hpp"

#include <Windows.h>

#include <cstdint>

namespace review_menu
{
	namespace
	{
		using namespace review_menu_rules;

		constexpr const char* as_before = "the Review menu keeps its Stats tab";

		std::uint8_t* game_image()
		{
			return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(image_base));
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

		// Every byte at once: they lie in one span over two pages of the exe's code, made writable,
		// written, and put back. A refusal writes nothing.
		bool write_bytes()
		{
			void* span = game_image() + (span_begin - image_base);
			const SIZE_T size = span_end - span_begin;
			DWORD protection = 0;
			if (!VirtualProtect(span, size, PAGE_EXECUTE_READWRITE, &protection))
			{
				return false;
			}
			apply(game_image());
			FlushInstructionCache(GetCurrentProcess(), span, size);
			VirtualProtect(span, size, protection, &protection);
			return true;
		}
	}

	void install(const HMODULE game)
	{
		if (ini::flag(L"Game", L"ReviewStats", true))
		{
			return; // no key, or 1: XML2's five tabs
		}
		if (!game || reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("review menu: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", as_before);
			return;
		}
		if (const guard* g = first_mismatch_guarded())
		{
			logger::write("review menu: 0x%08lX isn't the retail code (%s) - %s", g->va, g->what, as_before);
			return;
		}
		if (!write_bytes())
		{
			logger::write("review menu: ERROR: can't unprotect XMen2.exe's code at 0x%08lX (error %lu) - %s", span_begin, GetLastError(), as_before);
			return;
		}
		logger::write("review menu: no Stats tab ([Game] ReviewStats=0): Screens, Cinematics, Comics, Concepts - the tab change wraps at 4 "
		              "(0x5d180a, 0x5d1817), the mouse hit-tests 4 tabs (0x5d05b3), reviewmode 4 opens all four (0x5d1c89, 0x5d17f8); "
		              "the menu file's option05_text / option05_focus, if it has them, are still drawn");
	}
}
