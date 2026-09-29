#include "game_version.hpp"

#include "game_version_rules.hpp"
#include "ini.hpp"
#include "log.hpp"

#include <Windows.h>

#include <cstdint>

namespace game_version
{
	namespace
	{
		using namespace game_version_rules;

		constexpr const char* as_before = "online games stay on the game's own version, 1.30";

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
	}

	void install(const HMODULE game)
	{
		const auto chosen = parse_version(ini::text(L"Online", L"GameVersion"));
		if (!chosen.error.empty())
		{
			logger::write("online: [Online] GameVersion %s - %s", chosen.error.c_str(), as_before);
			return;
		}
		if (chosen.version.empty())
		{
			return; // no key: the game's own version
		}
		if (chosen.version == retail_version)
		{
			logger::write("online: [Online] GameVersion=%s is the game's own - nothing patched", chosen.version.c_str());
			return;
		}
		if (!game || reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("online: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", as_before);
			return;
		}
		if (const guard* g = first_mismatch_guarded())
		{
			logger::write("online: 0x%08lX isn't the retail code (%s) - %s", g->va, g->what, as_before);
			return;
		}

		void* target = game_image() + (version_va - image_base);
		DWORD protection = 0;
		if (!VirtualProtect(target, patched_bytes, PAGE_READWRITE, &protection))
		{
			logger::write("online: ERROR: can't unprotect XMen2.exe's data at 0x%08lX (error %lu) - %s", version_va, GetLastError(), as_before);
			return;
		}
		game_version_rules::apply(game_image(), chosen.version);
		VirtualProtect(target, patched_bytes, protection, &protection);
		logger::write("online: GameSpy game version %s ([Online] GameVersion; the game's own is 1.30, at 0x%08lX) - games of another version don't list or join",
		              chosen.version.c_str(), version_va);
	}
}
