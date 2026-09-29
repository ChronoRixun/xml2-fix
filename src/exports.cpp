// dinput.dll replacement for X-Men Legends II (2005 PC). Every export forwards to Windows'
// own dinput.dll in the system folder.
//
// The game reads pads twice: the engine (libIGDisplay.dll) through this DLL's DirectInput 7,
// and the game's own binding code through DirectInput 8, which it loads from the system
// folder by full path. The DirectInput 8 one is caught by hooking the game's GetProcAddress.
// Both are shown a Logitech Dual Action, and the game's bindings get a console-style layout
// for it (the PC version ships keyboard-only defaults).
//
// Loading also redirects the game's GameSpy lookups to OpenSpy, for online play, and, when
// xml2-fix.ini asks for it, runs the game windowed or borderless at the desktop's resolution,
// raises engine caps (the actor and resource name tables, limits.hpp), gives a mod's campaign what
// it needs (its New Game, saves, main menu, ending, forced parties, X-Men Legends 1's XP curve) and
// opens a named pipe through which tests press keys and take screenshots without the focus.

#include "display.hpp"
#include "forced_teams.hpp"
#include "gamepad_fix.hpp"
#include "iat_hook.hpp"
#include "limits.hpp"
#include "log.hpp"
#include "main_menu.hpp"
#include "mod_loader.hpp"
#include "new_game.hpp"
#include "net_trace.hpp"
#include "openspy_redirect.hpp"
#include "pad_profile.hpp"
#include "pad_bindings.hpp"
#include "pad_prompts.hpp"
#include "postgame.hpp"
#include "test_input.hpp"
#include "xp_curve.hpp"

#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <dinput.h>

#include <cstring>
#include <string>

namespace
{
	HMODULE system_dinput()
	{
		static const HMODULE module = []
		{
			wchar_t folder[MAX_PATH]{};
			const UINT length = GetSystemDirectoryW(folder, MAX_PATH); // SysWOW64 for this 32-bit DLL
			const auto path = std::wstring(folder, length) + L"\\dinput.dll";

			const auto loaded = LoadLibraryW(path.c_str());
			if (loaded)
			{
				logger::write("loaded %ls", path.c_str());
			}
			else
			{
				logger::write("ERROR: could not load %ls (error %lu)", path.c_str(), GetLastError());
			}
			return loaded;
		}();
		return module;
	}

	template <typename T>
	T real(const char* name)
	{
		const auto module = system_dinput();
		return module ? reinterpret_cast<T>(GetProcAddress(module, name)) : nullptr;
	}

	HRESULT hooked(const HRESULT result, void** out)
	{
		if (SUCCEEDED(result) && out && *out)
		{
			gamepad_fix::hook_direct_input(*out);
		}
		return result;
	}

	// The game's own DirectInput 8.
	decltype(&DirectInput8Create) real_direct_input8_create = nullptr;

	HRESULT WINAPI game_direct_input8_create(HINSTANCE instance, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer)
	{
		const HRESULT result = hooked(real_direct_input8_create(instance, version, riid, out, outer), out);
		if (SUCCEEDED(result) && out && *out)
		{
			test_input::hook_direct_input8(*out); // the game's keyboard device comes from here
		}
		return result;
	}

	using get_proc_address_t = FARPROC(WINAPI*)(HMODULE, LPCSTR);
	get_proc_address_t real_get_proc_address = nullptr;

	FARPROC WINAPI game_get_proc_address(HMODULE module, LPCSTR name)
	{
		const auto result = real_get_proc_address(module, name);
		if (result && !IS_INTRESOURCE(name) && std::strcmp(name, "DirectInput8Create") == 0)
		{
			real_direct_input8_create = reinterpret_cast<decltype(&DirectInput8Create)>(result);
			return reinterpret_cast<FARPROC>(&game_direct_input8_create);
		}
		return result;
	}

	std::string online_domain()
	{
		const auto ini = (logger::module_dir() / L"xml2-fix.ini").wstring();
		wchar_t value[256]{};
		GetPrivateProfileStringW(L"Online", L"Domain", L"openspy.net", value, static_cast<DWORD>(std::size(value)), ini.c_str());

		std::string domain;
		for (const wchar_t c : std::wstring(value))
		{
			domain += static_cast<char>(c);
		}
		return domain;
	}

	void install()
	{
		logger::write("XML2 Fix " FIX_VERSION);
		gamepad_fix::use_profile(pad_profile::logitech_dual_action);
		xml2_pad_bindings::install();

		const HMODULE game = GetModuleHandleW(nullptr);
		// The button prompts: the pad's names for a player on a pad, the power wheel's buttons of play. Code and
		// data the game runs from its first menu on; none of the sites is another module's.
		pad_prompts::install(game);
		real_get_proc_address = reinterpret_cast<get_proc_address_t>(
			iat_hook::hook(game, "KERNEL32.dll", "GetProcAddress", 0, reinterpret_cast<void*>(&game_get_proc_address)));
		if (!real_get_proc_address)
		{
			logger::write("the game doesn't import GetProcAddress - its own DirectInput 8 stays unfixed");
		}

		if (const auto domain = online_domain(); !domain.empty() && domain != "off")
		{
			openspy_redirect::install(game, domain.c_str());
		}
		else
		{
			logger::write("online: redirect turned off in xml2-fix.ini");
		}

		const auto ini = (logger::module_dir() / L"xml2-fix.ini").wstring();
		if (GetPrivateProfileIntW(L"Debug", L"LogNetwork", 0, ini.c_str()))
		{
			net_trace::install(game);
		}

		new_game::install(game);
		// What the end credits run: one push operand in CREDITS_MENU, used only when a campaign ends.
		postgame::install(game);
		// The main menu's item names: the push operands of MAIN_MENU's own code (mouse, Quit), used from the
		// first main menu on.
		main_menu::install(game);
		// X-Men Legends 1's level table, cap and kill XP. Here, in DllMain, before the exe's entry point: the game
		// first asks for a level's XP (0x448a90, which builds XML2's table on that call) when it loads the herostat at
		// start-up (0x4ba1d9, every hero's starting level), and its first kill, level check or XP bar come later still,
		// so every read of the table and the cap is the patched one. Independent of the other patches here: none of
		// its sites or guards is anyone else's.
		xp_curve::install(game);
		// The forced parties' script functions (seatParty and the rest). The game registers its script
		// functions exactly once, from its own init: 0x40197b -> game vt+0x13c (0x46b750) -> the
		// script interface's vt+0 (0x49fe30), which pushes its table and count and calls 0x4d75a0. That
		// runs long after this DllMain (below: before the exe's entry point), so the two push operands
		// are re-pointed at the DLL's longer table here, and every script - they compile from the first
		// menu on, which is when names are looked up - sees the new functions. After new_game: both
		// patch push operands of script-facing code; nothing here depends on limits or test_input (the
		// pipe's "script" command compiles its statement when it runs, with the functions in place).
		forced_teams::install(game);
		// Before any of XMen2.exe's own code runs, as the engine limit adjuster must be: this DLL is a
		// static import of libIGDisplay.dll, which XMen2.exe imports statically, so Windows runs this
		// DllMain while it loads the process - before the exe's entry point (0x6725f4, the CRT start-up
		// that runs its static initialisers and then WinMain; the exe has no TLS callbacks). The game
		// first asks for the actor table (getter 0x56b8e0) and the resource name table (0x55af80) from
		// the CPrecacheMgr constructor, during its start-up.
		limits::install(game);

		test_input::install(); // first: its screenshots need the display fix's device hook
		display::install(game);

		// The game and the engine DLLs that read game data.
		mod_loader::install({nullptr, "libIGCore.dll", "libIGGfx.dll", "libIGLua.dll", "libIGOpt.dll", "libCriMovie.dll"},
		                    GetPrivateProfileIntW(L"Debug", L"LogFiles", 0, ini.c_str()) != 0);
	}
}

BOOL WINAPI DllMain(HINSTANCE instance, const DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(instance);
		install();
	}
	return TRUE;
}

extern "C"
{
	HRESULT WINAPI proxy_DirectInputCreateA(HINSTANCE instance, DWORD version, LPVOID* out, LPUNKNOWN outer)
	{
		static const auto target = real<HRESULT(WINAPI*)(HINSTANCE, DWORD, LPVOID*, LPUNKNOWN)>("DirectInputCreateA");
		return target ? hooked(target(instance, version, out, outer), out) : DIERR_GENERIC;
	}

	HRESULT WINAPI proxy_DirectInputCreateW(HINSTANCE instance, DWORD version, LPVOID* out, LPUNKNOWN outer)
	{
		static const auto target = real<HRESULT(WINAPI*)(HINSTANCE, DWORD, LPVOID*, LPUNKNOWN)>("DirectInputCreateW");
		return target ? hooked(target(instance, version, out, outer), out) : DIERR_GENERIC;
	}

	HRESULT WINAPI proxy_DirectInputCreateEx(HINSTANCE instance, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer)
	{
		static const auto target = real<HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN)>("DirectInputCreateEx");
		return target ? hooked(target(instance, version, riid, out, outer), out) : DIERR_GENERIC;
	}

	HRESULT WINAPI proxy_DllCanUnloadNow()
	{
		static const auto target = real<HRESULT(WINAPI*)()>("DllCanUnloadNow");
		return target ? target() : S_FALSE;
	}

	HRESULT WINAPI proxy_DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* out)
	{
		static const auto target = real<HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*)>("DllGetClassObject");
		return target ? target(clsid, riid, out) : CLASS_E_CLASSNOTAVAILABLE;
	}

	HRESULT WINAPI proxy_DllRegisterServer()
	{
		static const auto target = real<HRESULT(WINAPI*)()>("DllRegisterServer");
		return target ? target() : E_FAIL;
	}

	HRESULT WINAPI proxy_DllUnregisterServer()
	{
		static const auto target = real<HRESULT(WINAPI*)()>("DllUnregisterServer");
		return target ? target() : E_FAIL;
	}
}
