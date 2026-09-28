#include "main_menu.hpp"

#include "log.hpp"
#include "main_menu_rules.hpp"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace main_menu
{
	namespace
	{
		using namespace main_menu_rules;

		constexpr const char* as_before = "the main menu keeps XML2's item names";

		// The names the patched pushes point at: they must live as long as the process.
		char names[slot_count][name_max + 1]{};

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

		bool sites_are_retail_guarded()
		{
			__try
			{
				return sites_are_retail(game_image());
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		// Every operand at once: they lie in one span on one page of the exe's code, made writable,
		// written, and put back. A refusal writes nothing.
		bool write_operands(const std::vector<operand_write>& writes)
		{
			void* span = game_image() + (span_begin - image_base);
			const SIZE_T size = span_end - span_begin;
			DWORD protection = 0;
			if (!VirtualProtect(span, size, PAGE_EXECUTE_READWRITE, &protection))
			{
				return false;
			}
			apply(game_image(), writes);
			FlushInstructionCache(GetCurrentProcess(), span, size);
			VirtualProtect(span, size, protection, &protection);
			return true;
		}
	}

	void install(const HMODULE game)
	{
		const auto ini = (logger::module_dir() / L"xml2-fix.ini").wstring();
		wchar_t value[512]{};
		GetPrivateProfileStringW(L"Game", L"MainMenuItems", L"", value, static_cast<DWORD>(std::size(value)), ini.c_str());
		std::string narrow;
		for (const wchar_t* p = value; *p; ++p)
		{
			narrow += *p < 128 ? static_cast<char>(*p) : '\x7f'; // non-ASCII is refused by parse_items
		}
		const auto chosen = parse_items(narrow);
		if (!chosen.error.empty())
		{
			logger::write("main menu: [Game] MainMenuItems=%s %s - %s", narrow.c_str(), chosen.error.c_str(), as_before);
			return;
		}
		if (!chosen.set)
		{
			return; // no key: XML2's names
		}
		std::size_t changed = 0;
		for (std::size_t i = 0; i < slot_count; ++i)
		{
			changed += changes(chosen, i);
		}
		if (!changed)
		{
			logger::write("main menu: [Game] MainMenuItems=%s names only the game's own items - nothing patched", narrow.c_str());
			return;
		}

		if (!game || reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("main menu: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", as_before);
			return;
		}
		if (const guard* g = first_mismatch_guarded())
		{
			logger::write("main menu: 0x%08lX isn't the retail code (%s) - %s", g->va, g->what, as_before);
			return;
		}
		if (!sites_are_retail_guarded())
		{
			logger::write("main menu: a name push isn't the retail one - %s", as_before);
			return;
		}

		std::array<std::uint32_t, slot_count> pointers{};
		for (std::size_t i = 0; i < slot_count; ++i)
		{
			strncpy_s(names[i], chosen.names[i].c_str(), _TRUNCATE);
			pointers[i] = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(names[i]));
		}
		const auto writes = writes_for(chosen, pointers);
		if (!write_operands(writes))
		{
			logger::write("main menu: ERROR: can't unprotect XMen2.exe's code at 0x%08lX (error %lu) - %s", span_begin, GetLastError(), as_before);
			return;
		}
		std::string described;
		for (std::size_t i = 0; i < slot_count; ++i)
		{
			described += i ? ", " : "";
			described += std::string(slots[i].retail) + " (" + slots[i].role + ") -> ";
			described += changes(chosen, i) ? chosen.names[i] : std::string("as the game has it");
		}
		logger::write("main menu: MAIN_MENU's item names ([Game] MainMenuItems): %s - %zu name pushes re-pointed; the Danger Room gate and Play Online still "
		              "answer to label_option06 / label_option09 only",
		              described.c_str(), writes.size());
	}
}
