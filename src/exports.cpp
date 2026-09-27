// dinput.dll replacement for X-Men Legends II (2005 PC). Every export forwards to Windows'
// own dinput.dll in the system folder.
//
// The game reads pads twice: the engine (libIGDisplay.dll) through this DLL's DirectInput 7,
// and the game's own binding code through DirectInput 8, which it loads from the system
// folder by full path. The DirectInput 8 one is caught by hooking the game's GetProcAddress.
// Both are shown a Logitech Dual Action, and the game's bindings get a console-style layout
// for it (the PC version ships keyboard-only defaults).
//
// Loading also redirects the game's GameSpy lookups to OpenSpy, for online play.

#include "gamepad_fix.hpp"
#include "iat_hook.hpp"
#include "log.hpp"
#include "mod_loader.hpp"
#include "net_trace.hpp"
#include "openspy_redirect.hpp"
#include "pad_profile.hpp"
#include "pad_bindings.hpp"

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
		return hooked(real_direct_input8_create(instance, version, riid, out, outer), out);
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
