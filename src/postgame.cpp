#include "postgame.hpp"

#include "ini.hpp"
#include "log.hpp"
#include "postgame_rules.hpp"

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace postgame
{
	namespace
	{
		using namespace postgame_rules;

		constexpr const char* as_before = "the end credits load XML2's act5/egypt/egypt6 as before";

		// The line the patched push points at: it must live as long as the process.
		char line[console_max + 1]{};

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

		bool write_operand(const std::uint32_t value)
		{
			void* target = game_image() + (load_operand - image_base);
			DWORD protection = 0;
			if (!VirtualProtect(target, sizeof(value), PAGE_EXECUTE_READWRITE, &protection))
			{
				return false;
			}
			apply(game_image(), value);
			FlushInstructionCache(GetCurrentProcess(), target, sizeof(value));
			VirtualProtect(target, sizeof(value), protection, &protection);
			return true;
		}

		// The script in the game folder, or in a mod's folder under mods (the mod loader serves those).
		bool script_present(const std::string& relative)
		{
			std::error_code ignored;
			const auto& game_dir = logger::module_dir();
			if (std::filesystem::is_regular_file(game_dir / relative, ignored))
			{
				return true;
			}
			std::error_code walk;
			for (std::filesystem::directory_iterator mod(game_dir / L"mods", walk), end; !walk && mod != end; mod.increment(walk))
			{
				if (mod->is_directory(ignored) && std::filesystem::is_regular_file(mod->path() / relative, ignored))
				{
					return true;
				}
			}
			return false;
		}

		const guard* first_unlock_mismatch_guarded()
		{
			__try
			{
				return first_unlock_mismatch(game_image());
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return &unlock_guards[0];
			}
		}

		// [Game] EndHeroUnlock=0: the ending's state 3 jumps over XML2's hero unlock and its popup
		// (postgame_rules.hpp); the win, Hard, the save and the rest of the ending stay.
		void install_end_unlock(const HMODULE game)
		{
			constexpr const char* unlock_as_before = "the ending unlocks XML2's Deadpool with its popup, as the game has it";
			const auto chosen = parse_end_unlock(ini::text(L"Game", L"EndHeroUnlock"));
			if (!chosen.error.empty())
			{
				logger::write("postgame: [Game] EndHeroUnlock%s - %s", chosen.error.c_str(), unlock_as_before);
				return;
			}
			if (!chosen.skip)
			{
				return; // no key (or 1): the game's own ending
			}
			if (!game || reinterpret_cast<std::uintptr_t>(game) != image_base)
			{
				logger::write("postgame: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", unlock_as_before);
				return;
			}
			if (const guard* g = first_unlock_mismatch_guarded())
			{
				logger::write("postgame: 0x%08lX isn't the retail code (%s) - %s", g->va, g->what, unlock_as_before);
				return;
			}
			void* target = game_image() + (unlock_call - image_base);
			DWORD protection = 0;
			if (!VirtualProtect(target, skip_unlock.size(), PAGE_EXECUTE_READWRITE, &protection))
			{
				logger::write("postgame: ERROR: can't unprotect XMen2.exe's code at 0x%08lX (error %lu) - %s", unlock_call, GetLastError(), unlock_as_before);
				return;
			}
			apply_skip_unlock(game_image());
			FlushInstructionCache(GetCurrentProcess(), target, skip_unlock.size());
			VirtualProtect(target, skip_unlock.size(), protection, &protection);
			logger::write("postgame: the ending unlocks no hero and shows no popup for one ([Game] EndHeroUnlock=0) - CREDITS_MENU's state 3 jumps from 0x%08lX to 0x%08lX, over "
			              "XML2's Deadpool unlock and string 1194; the win, Hard after Normal and the end-of-game save stay",
			              unlock_call, unlock_skip_to);
		}

		void install_script(const HMODULE game)
		{
			const auto narrow = ini::text(L"Game", L"PostgameScript").value_or(""); // non-ASCII comes as 0x7f, refused by parse_script
			const auto chosen = parse_script(narrow);
			if (!chosen.error.empty())
			{
				logger::write("postgame: [Game] PostgameScript=%s %s - %s", narrow.c_str(), chosen.error.c_str(), as_before);
				return;
			}
			if (chosen.name.empty())
			{
				return; // no key: the game's own ending
			}

			if (!game || reinterpret_cast<std::uintptr_t>(game) != image_base)
			{
				logger::write("postgame: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", as_before);
				return;
			}
			if (const guard* g = first_mismatch_guarded())
			{
				logger::write("postgame: 0x%08lX isn't the retail code (%s) - %s", g->va, g->what, as_before);
				return;
			}

			const auto text = command_line(chosen.name);
			strncpy_s(line, text.c_str(), _TRUNCATE);
			if (!write_operand(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(line))))
			{
				logger::write("postgame: ERROR: can't unprotect XMen2.exe's code at 0x%08lX (error %lu) - %s", load_operand, GetLastError(), as_before);
				return;
			}
			const auto file = script_file(chosen.name);
			logger::write("postgame: after the end credits (and the game's end-of-game save) the game runs %s ([Game] PostgameScript) instead of loading XML2's act5/egypt/egypt6 - "
			              "CREDITS_MENU's push at 0x%08lX now points at \"%s\"",
			              file.c_str(), load_push, line);
			if (!script_present(file))
			{
				logger::write("postgame: note: %s isn't in the game folder or a mods folder - if the game can't find it either, the credits close onto the last zone and nothing else happens",
				              file.c_str());
			}
		}
	}

	void install(const HMODULE game)
	{
		install_end_unlock(game);
		install_script(game);
	}
}
