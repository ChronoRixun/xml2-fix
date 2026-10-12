// dinput.dll replacement for X-Men Legends II (2005 PC). Every export forwards to Windows'
// own dinput.dll in the system folder.
//
// The game reads pads twice: the engine (libIGDisplay.dll) through this DLL's DirectInput 7,
// and the game's own binding code through DirectInput 8, which it loads from the system
// folder by full path. The DirectInput 8 one is caught by hooking the game's GetProcAddress.
// Both are shown a Logitech Dual Action, and the game's bindings get a console-style layout
// for it (the PC version ships keyboard-only defaults).
//
// Loading also redirects the game's GameSpy lookups to OpenSpy (or to one server of your own,
// [Online] Server) and puts the address this PC reaches the internet from first among the game's
// own ([Online] LocalIP), for online play, and, when
// xml2-fix.ini asks for it, runs the game windowed or borderless at the desktop's resolution,
// raises engine caps (the actor and resource name tables, limits.hpp), gives a mod's campaign what
// it needs (its New Game, saves, main menu, ending, forced parties, X-Men Legends 1's XP curve and its rule
// for breaking objects) and
// opens a named pipe through which tests press keys and pad buttons (on virtual pads, [Test]
// VirtualPads, when nothing is plugged in) and take screenshots without the focus. It also
// shows what the game is doing in Discord (Rich Presence, [Discord]; on unless switched off).

#include "balance.hpp"
#include "break_rule.hpp"
#include "conversations.hpp"
#include "confirmation_release.hpp"
#include "explosive_first_hit.hpp"
#include "gameplay_agreement.hpp"
#include "gameplay_capabilities.hpp"
#include "objective_text.hpp"
#include "discord_presence.hpp"
#include "display.hpp"
#include "forced_teams.hpp"
#include "mold_controller.hpp"
#include "xml1_turrets.hpp"
#include "game_version.hpp"
#include "gamepad_fix.hpp"
#include "iat_hook.hpp"
#include "ini.hpp"
#include "limits.hpp"
#include "geometry_sharing.hpp"
#include "ladder_paths.hpp"
#include "log.hpp"
#include "main_menu.hpp"
#include "classic_main_backdrop.hpp"
#include "classic_main_panels.hpp"
#include "mod_loader.hpp"
#include "new_game.hpp"
#include "net_trace.hpp"
#include "openspy_redirect.hpp"
#include "pad_profile.hpp"
#include "pad_bindings.hpp"
#include "pad_prompts.hpp"
#include "patch_transaction.hpp"
#include "entity_capacity.hpp"
#include "postgame.hpp"
#include "review_menu.hpp"
#include "personal_item_text.hpp"
#include "save_format.hpp"
#include "save_content_guards.hpp"
#include "save_trace.hpp"
#include "stat_help.hpp"
#include "test_input.hpp"
#include "virtual_pad.hpp"
#include "window_title.hpp"
#include "xinput_pad.hpp"
#include "xtract.hpp"
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

	// An [Online] value by the fix's one rule (ini_rules.hpp): "" when it isn't set.
	std::string online_value(const wchar_t* key)
	{
		return ini::text(L"Online", key).value_or("");
	}

	void install()
	{
		logger::write("XML2 Fix " FIX_VERSION);
		gamepad_fix::use_profile(pad_profile::logitech_dual_action);
		// [Test] VirtualPads: pads with nothing plugged in, in both DirectInput paths below - read before either exists.
		virtual_pad::install();
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

		// [Online] LocalIP: which of this PC's addresses the game takes for its own (its LocalIP, its game
		// socket's and its heartbeats' first address) - it resolves its own host name, from the Play Online
		// screen on, so the same gethostbyname hook arranges that answer.
		openspy_redirect::install(game, online_rules::choose(online_value(L"Domain"), online_value(L"Server")),
		                          local_ip_rules::choose(online_value(L"LocalIP")));

		if (ini::flag(L"Debug", L"LogNetwork", false))
		{
			net_trace::install(game);
		}
		// [Online] GameVersion: the string the game copies into its network code at start-up and when online
		// opens - here, before any of its code runs.
		game_version::install(game);
		if (!game_version::startup_allowed()) return;

		// The one startup transaction (docs/entity-capacity-phase-a-plan.md section 7): the shared gameplay
		// agreement's refusal and transport sites, the LC save seam, [Limits] MoveTriggers and the entity-capacity
		// install contribute their guards and writes; every expectation is unioned and checked once, every touched
		// page acquired once, and nothing is published unless the whole set verified. A refusal leaves stock bytes.
		patch_transaction::transaction startup;
		if (game_version::needs_gameplay_agreement()) gameplay_agreement::contribute(startup, game);

		new_game::install(game);
		// [Save] Format=lc: the LC save format's disk seam (the card's nine file methods, the stream helpers'
		// skip branches, the zone placement file's bytes), used from the first save menu on. After new_game, whose
		// [Game] SaveFolder re-points the folder the card builds at start-up and this reads at its first file
		// operation. The entity-capacity install chooses the native save layout before the seam is contributed.
		entity_capacity::prepare(startup, game);
		save_format::contribute(startup, game);
		limits::contribute(startup, game, entity_capacity::owns_arena_teardown());
		entity_capacity::contribute(startup, game);
		const auto committed = startup.commit();
		logger::write("startup transaction: %s (%s)", patch_transaction::name(committed), startup.report().c_str());
		entity_capacity::after_commit(committed == patch_transaction::outcome::committed, committed == patch_transaction::outcome::rollback_failed);
		if (!save_format::startup_allowed() || !entity_capacity::startup_allowed()) return;
		// Start background work only after required startup hooks have verified.
		// XInput's first call can take seconds (xinput_pad.hpp): made on a thread of its own, which runs once
		// the loader is done, so the game's thread never waits for it; pads read as idle until then.
		xinput_pad::start();
		save_content_guards::install(game);
		save_trace::install(game);
		// What the end credits run: one push operand in CREDITS_MENU, used only when a campaign ends.
		postgame::install(game);
		// The main menu's item names: the push operands of MAIN_MENU's own code (mouse, Quit), used from the
		// first main menu on.
		main_menu::install(game);
		classic_main_backdrop::install(game);
		classic_main_panels::install(game);
		// The Review menu's tabs: five imm8s in REVIEW_PATHS_MENU's own code, used from the first Review on.
		review_menu::install(game);
		personal_item_text::install(game);
		// An Xtraction Point's Xtract choice ([Game] Xtract=0): two bytes in extractionPoint's own
		// code, used from the first Xtraction Point on.
		xtract::install(game);
		// X-Men Legends 1's level table, cap and kill XP. Here, in DllMain, before the exe's entry point: the game
		// first asks for a level's XP (0x448a90, which builds XML2's table on that call) when it loads the herostat at
		// start-up (0x4ba1d9, every hero's starting level), and its first kill, level check or XP bar come later still,
		// so every read of the table and the cap is the patched one. Independent of the other patches here: none of
		// its sites or guards is anyone else's.
		xp_curve::install(game);
		// [Balance]: X-Men Legends 1's combat numbers. Calls and one jump in the health, damage and roll functions and the
		// style reader, first run when a character or a style loads; none of its sites or guards is anyone else's.
		balance::install(game);
		// The stats screen's help text: the two literals that name [Balance] numbers (health per body, the
		// strike bonus) follow the same applied values, through the string manager's vt+8; every other
		// string passes to the game's getter. After balance: it reads what balance applied.
		stat_help::install(game);
		// The forced parties' script functions (seatParty and the rest). The game registers its script
		// functions exactly once, from its own init: 0x40197b -> game vt+0x13c (0x46b750) -> the
		// script interface's vt+0 (0x49fe30), which pushes its table and count and calls 0x4d75a0. That
		// runs long after this DllMain (below: before the exe's entry point), so the two push operands
		// are re-pointed at the DLL's longer table here, and every script - they compile from the first
		// menu on, which is when names are looked up - sees the new functions. After new_game: both
		// patch push operands of script-facing code; nothing here depends on limits or test_input (the
		// pipe's "script" command compiles its statement when it runs, with the functions in place).
		forced_teams::install(game);
		mold_controller::install(game);
		// XML1 scan turrets ([Game] Xml1Turrets, builder [Xml1Turrets]): one pointer of the native event
		// scheduler's vtable (its per-frame dispatch); after mold_controller, whose query guards it shares.
		xml1_turrets::install(game);
		if (!game_version::startup_allowed()) return;
		// XML1 controllers in the shared gameplay record (docs/gameplay-agreement.md): online play is lockstep, so
		// every peer runs Master Mold's native AI update and must have the same effective controller (rule 3).
		// The scan turrets run from the same lockstep update on every peer (rule 4): a process that does not request
		// them advertises revision 1 verified off, as 1.4.0 did.
		// Registering a requested controller enlists the agreement even in a non-LC installation.
		if (game_version::needs_gameplay_agreement() || mold_controller::requested() || xml1_turrets::requested())
		{
			const auto mold = mold_controller::capability();
			gameplay_agreement::register_rule(gameplay_capabilities::master_mold_id, xml1_capability_rules::master_mold_revision, mold.values, mold.verified);
			const auto turrets = xml1_turrets::capability();
			gameplay_agreement::register_rule(gameplay_capabilities::turret_controller_id, xml1_capability_rules::turret_revision_for(xml1_turrets::requested()), turrets.values, turrets.verified);
		}
		// Conversations written for XML1's engine ([Game] AutoAdvance, ReplyVoices, ReplyCursor; on by default):
		// three code sites of the conversation system, used from the first conversation on; independent of the
		// patches above (none of its sites or guards is anyone else's).
		conversations::install(game);
		confirmation_release::install(game);
		explosive_first_hit::install(game);
		if (game_version::needs_gameplay_agreement())
			gameplay_agreement::register_rule(gameplay_capabilities::explosive_first_hit_id,1,{explosive_first_hit::installed() ? 1u : 0u,0,0,0},explosive_first_hit::verified());
		objective_text::install(game);
		// Before any of XMen2.exe's own code runs, as the engine limit adjuster must be: this DLL is a
		// static import of libIGDisplay.dll, which XMen2.exe imports statically, so Windows runs this
		// DllMain while it loads the process - before the exe's entry point (0x6725f4, the CRT start-up
		// that runs its static initialisers and then WinMain; the exe has no TLS callbacks). The game
		// first asks for the actor table (getter 0x56b8e0) and the resource name table (0x55af80) from
		// the CPrecacheMgr constructor, during its start-up.
		limits::install(game);
		geometry_sharing::install(game);
		ladder_paths::install(game);
		// X-Men Legends 1's rule for breaking objects ([Game] BreakRule=xml1): one vtable slot of the combat object and
		// two code sites of the objects' own (their structure read and their damage gate), used from the first zone on;
		// none of its sites or guards is anyone else's.
		break_rule::install(game);

		test_input::install(); // first: its screenshots need the display fix's device hook
		display::install(game);
		// [Game] WindowTitle: after the display fix, whose CreateWindowExA hook (borderless / windowed) it then
		// calls with the new title; either order chains.
		window_title::install();

		// The game and the engine DLLs that read game data.
		mod_loader::install({nullptr, "libIGCore.dll", "libIGGfx.dll", "libIGLua.dll", "libIGOpt.dll", "libCriMovie.dll"},
		                    ini::flag(L"Debug", L"LogFiles", false));

		// Discord Rich Presence ([Discord], on by default): a thread of its own that reads the game's state and
		// talks to Discord's local pipe; it hooks only ExitProcess (the game's import and msvcr71.dll's), to
		// clear the presence on quitting. Its X-Men Legends I port detection reads mods\load-order.txt as the
		// mod loader above does (mod_order.hpp).
		discord_presence::install(game);

		// Freeze only after every participating gameplay installer has registered its effective values.
		if (game_version::needs_gameplay_agreement())
			game_version::bind_gameplay_agreement(gameplay_agreement::commit_verified());
	}
}

BOOL WINAPI DllMain(HINSTANCE instance, const DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(instance);
		install();
		if (!game_version::startup_allowed() || !save_format::startup_allowed() || !entity_capacity::startup_allowed())
		{
			const std::string startup_reason = !game_version::startup_allowed()
				? std::string(game_version::startup_error())
				: !save_format::startup_allowed() ? "The required LC save/trivia hooks could not be installed.\n" + save_format::startup_error()
				: "The entity-capacity install could not be certified.\n" + entity_capacity::startup_error();
			logger::write("startup: refused: %s", startup_reason.c_str());
			const auto message = startup_reason + "\n\nThe game was not started. Check xml2-fix.log and use a supported executable.";
			MessageBoxA(nullptr, message.c_str(), "XML2 Fix cannot start", MB_OK | MB_ICONERROR);
			logger::write("startup: refusal notice closed; DLL initialization rejected");
			return FALSE;
		}
		// A capacity-only fallback starts the game (stock capacities, online closed): the player is told once, here.
		if (const auto notice = entity_capacity::fallback_notice(); !notice.empty())
		{
			logger::write("startup: entity capacity notice: requested raise not installed; continuing with stock capacities");
			MessageBoxA(nullptr, notice.c_str(), "XML2 Fix: entity capacity unavailable", MB_OK | MB_ICONWARNING);
			logger::write("startup: entity capacity notice closed");
		}
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
