#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>

// The team-combo popup never draws without both of its heads (owner's crash 2026-10-23, beta 1: XMen2.exe+0x1a6848,
// the popup's draw calling through a null second head when the game's model pools were full). On by default:
//
//   [Game]
//   ComboPopupGuard = 0   ; the game's own popup (crashes when a head could not be made)
//
// A popup that lacks a head is hidden the way its own timeout hides it; the combo's XP and count are unchanged.
// Display only (no gameplay-agreement record). The research and every byte relied on are in combo_heads_rules.hpp.
namespace combo_heads
{
	void install(HMODULE game);
	std::string status(); // "combo popup guard on (N hidden)" / "combo popup guard off" / "... unavailable"

	// The fix's own show (HUD vt+0xb4), exposed for the tests: runs `original` and hides a popup that lacks a head.
	using show_fn = void(__fastcall*)(void* hud, void* unused, const void* hero_a, const void* hero_b, int type, int amount, const char* text);
	void __fastcall show(void* hud, void* unused, const void* hero_a, const void* hero_b, int type, int amount, const char* text);
	void set_original_for_test(show_fn original);
	// Where the IGB and shader-effect pools' live counts are (model_pools moves them with [Engine] ModelPools), for the log.
	void set_pool_live_addresses(std::uint32_t igb, std::uint32_t shader_fx, int igb_capacity, int shader_fx_capacity);
	unsigned hidden_count();
}
