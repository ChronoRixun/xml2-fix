#include <WinSock2.h>
#include "game_version.hpp"
#include "iat_hook.hpp"
#include "save_guard_rules.hpp"

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

		bool lc_required = false;
		bool version_verified = false;
		bool gameplay_required = false, gameplay_verified = false;
		void (*show_refusal)() = nullptr;
		// These experimental rules have no shared gameplay-capability entry yet.
		// Capture the request once: later INI edits cannot reopen a patched process.
		bool experimental_offline = false;
		using socket_t = SOCKET(WSAAPI*)(int,int,int);
		socket_t real_socket = nullptr;
		SOCKET WSAAPI guarded_socket(int family, int type, int protocol)
		{
			if (experimental_offline)
			{
				WSASetLastError(WSAEACCES);
				logger::write_once("xml1-controller-online-refused", "online: refused: experimental XML1 controllers are not covered by shared gameplay agreement");
				return INVALID_SOCKET;
			}
			if (!save_guard_rules::online_allowed(lc_required,version_verified,gameplay_required,gameplay_verified))
			{
				if (show_refusal) show_refusal();
				WSASetLastError(WSAEACCES);
				logger::write_once("lc-online-refused", "online: host/join refused: %s",
				                  gameplay_required && !gameplay_verified ? "effective gameplay capability record was not installed/verified" : "the four-character release version patch did not verify");
				return INVALID_SOCKET;
			}
			return real_socket(family,type,protocol);
		}

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

	bool startup_allowed()
	{
		return save_guard_rules::startup_allowed(lc_required || gameplay_required || experimental_offline,real_socket != nullptr);
	}

	bool offline_gameplay_ready() { return experimental_offline && real_socket != nullptr; }

	bool require_gameplay_agreement(HMODULE game)
	{
		gameplay_required = true;
		gameplay_verified = false;
		if (!real_socket)
			real_socket = reinterpret_cast<socket_t>(iat_hook::hook(game,"WS2_32.dll","socket",23,reinterpret_cast<void*>(&guarded_socket)));
		return real_socket != nullptr;
	}
	bool gameplay_online_allowed() { return !experimental_offline && save_guard_rules::online_allowed(lc_required,version_verified,gameplay_required,gameplay_verified); }
	bool needs_gameplay_agreement() { return gameplay_required; }
	void bind_gameplay_refusal(void (*show)()) { show_refusal = show; }
	void bind_gameplay_agreement(bool verified) { gameplay_verified = verified; }
	void install(const HMODULE game)
	{
		version_verified = false;
		real_socket = nullptr;
		experimental_offline = ini::flag(L"Game",L"Xml1MasterMold",false) ||
			ini::flag(L"Game",L"Xml1Turrets",false) || ini::number(L"Test",L"NativeProjectileProbe",0) == 1;
		lc_required = save_format_rules::parse_mode(ini::text(L"Save",L"Format").value_or("")).value == save_format_rules::mode::lc;
		gameplay_required = lc_required || ini::text(L"Game", L"ExplosiveFirstHit").has_value();
		gameplay_verified = false;
		if (lc_required || gameplay_required || experimental_offline)
		{
			real_socket = reinterpret_cast<socket_t>(iat_hook::hook(game,"WS2_32.dll","socket",23,reinterpret_cast<void*>(&guarded_socket)));
			if (!real_socket)
			{
				logger::write("online: ERROR: host/join socket exclusion hook could not be installed");
				return;
			}
		}
		const auto chosen = parse_version(ini::text(L"Online", L"GameVersion"));
		if (!chosen.error.empty())
		{
			logger::write("online: [Online] GameVersion %s - %s", chosen.error.c_str(), (lc_required ? "LC host/join sockets refused" : as_before));
			return;
		}
		if (lc_required && (chosen.version.size() != version_max || chosen.version == retail_version))
		{
			logger::write("online: LC requires an assigned four-character release token; host/join sockets refused");
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
			logger::write("online: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", (lc_required ? "LC host/join sockets refused" : as_before));
			return;
		}
		if (const guard* g = first_mismatch_guarded())
		{
			logger::write("online: 0x%08lX isn't the retail code (%s) - %s", g->va, g->what, (lc_required ? "LC host/join sockets refused" : as_before));
			return;
		}

		void* target = game_image() + (version_va - image_base);
		DWORD protection = 0;
		if (!VirtualProtect(target, patched_bytes, PAGE_READWRITE, &protection))
		{
			logger::write("online: ERROR: can't unprotect XMen2.exe's data at 0x%08lX (error %lu) - %s", version_va, GetLastError(), (lc_required ? "LC host/join sockets refused" : as_before));
			return;
		}
		game_version_rules::apply(game_image(), chosen.version);
		DWORD ignored = 0;
		VirtualProtect(target, patched_bytes, protection, &ignored);
		const auto expected = bytes_for(chosen.version);
		version_verified = std::memcmp(target,expected.data(),expected.size()) == 0;
		if (!version_verified)
		{
			logger::write("online: version patch readback failed; LC host/join sockets refused");
			return;
		}
		logger::write("online: GameSpy game version %s ([Online] GameVersion; the game's own is 1.30, at 0x%08lX) - games of another version don't list or join",
		              chosen.version.c_str(), version_va);
	}
}
