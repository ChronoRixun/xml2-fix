#include "cutscene_mouse.hpp"

#include "cutscene_mouse_rules.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <Windows.h>

#include <cstdint>
#include <vector>

namespace cutscene_mouse
{
	namespace
	{
		using namespace cutscene_mouse_rules;

		constexpr const char* as_before = "the mouse keeps the game's own rule in cinematics (only lockControls stops it)";

		using getter_t = void*(__cdecl*)();
		using test_t = bool(__fastcall*)(void* self); // __thiscall without arguments

		bool call_test(void* object, const DWORD slot)
		{
			const auto* vtable = *static_cast<void* const* const*>(object);
			return reinterpret_cast<test_t>(vtable[slot / 4])(object);
		}

		// In place of the game's "are the controls locked" at 0x5fa2d4 (the mouse handler, on the game's
		// thread): the same answer, or a cinematic is on. The caller tests al.
		bool __cdecl mouse_locked()
		{
			const bool locked = call_test(reinterpret_cast<getter_t>(game_getter)(), controls_locked_slot);
			return mouse_ignored(locked, !locked && call_test(reinterpret_cast<getter_t>(hud_getter)(), cinematic_slot));
		}

		const guard* first_mismatch_guarded()
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
		if (!ini::flag(L"Input", L"CutsceneMouse", true))
		{
			logger::write("cutscene mouse: off ([Input] CutsceneMouse=0) - %s", as_before);
			return;
		}
		if (!game || reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("cutscene mouse: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", as_before);
			return;
		}
		if (const guard* g = first_mismatch_guarded())
		{
			logger::write("cutscene mouse: 0x%08lX isn't the retail code (%s) - %s", g->va, g->what, as_before);
			return;
		}

		const auto after = patch_bytes(static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&mouse_locked)));
		const auto* retail = reinterpret_cast<const unsigned char*>(static_cast<std::uintptr_t>(site));
		const std::vector<xml1_hook_transaction::patch> writes{
		    {site, std::vector<unsigned char>(retail, retail + site_size), std::vector<unsigned char>(after.begin(), after.end())}};
		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (xml1_hook_transaction::apply(memory, writes, &safe))
		{
			logger::write("cutscene mouse: the mouse leaves the heroes alone in a cinematic as the keys and pads do - the mouse handler's "
			              "lock test (0x5fa2d4) also asks the HUD (vt+0xec)");
		}
		else if (safe)
		{
			logger::write("cutscene mouse: ERROR: the patch at 0x%08lX didn't verify, nothing changed - %s", site, as_before);
		}
		else
		{
			game_version::refuse_startup();
			logger::write("cutscene mouse: ERROR: the patch at 0x%08lX didn't verify and the retail bytes couldn't be put back - startup refused", site);
		}
	}
}
