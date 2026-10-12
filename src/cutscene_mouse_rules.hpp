#pragma once

// [Input] CutsceneMouse (on by default): the mouse leaves the heroes alone while a cinematic owns the
// screen, as the keyboard and the pads already do. Kept apart from the patching so xml2_test can check it
// without the game: every retail byte the change relies on and the twelve bytes it writes.
//
// Every address below was read from the retail XMen2.exe (image base 0x400000, no relocations) for this
// code and is in `guards`.
//
// What the game does with a hero's keys and pads: every frame 0x46f640 runs, per player, 0x425500, which
// moves and fights that player's hero from the bindings - unless the Danger Room's free play is on
// (0x4c87c0), a conversation is up (the conversation system 0x4583f0, vt+0x20), the HUD shows a cinematic
// (the HUD 0x59ee20, vt+0xec = 0x59ec50: bit 2 of HUD+0x889b1, set by cinematicStart at 0x59b3e4 and
// cleared by cinematicEnd at 0x599dae) or the controls are locked (the game 0x46dce0, vt+0x34 = 0x469180:
// lockControls' deadline, a menu or a popup) - the checks at 0x425507..0x425590.
//
// What it does with the mouse: the window's mouse messages, and the 0x210 / 0x211 / 0x214 the frame loop
// makes (0x401ec4..0x401f65, the 3D cursor's hover), go to 0x5f9eb0. It hands them to a popup (0x5eb300
// vt+0x78), a menu (0x5d8920 vt+0x204) or a conversation (0x4583f0 vt+0x20) when one is up; otherwise it
// finds the local player's hero and then asks only whether the controls are locked (0x5fa2d4..0x5fa2e2):
// locked, it clears the hover (0xa0a080 + player * 4, 0xa0a090, 0xa0a06c, 0x6e84d0, 0xa0a064) and lets go of
// the player's eight mouse buttons (0x61a5d0); not locked, the hover outline, the click-to-move and the
// mouse powers follow (0x5fa33e on, 0x5f7e60). The cinematic test is missing there, and X-Men Legends 1's
// mission cutscenes (the "mocap" briefings: cinematicStart, no lockControls) run with the controls
// unlocked: the keyboard and pads are still and the mouse outlines the cast and clicks to move
// (legends-classic issue #85).
//
// The change: the call to the game's getter and its vt+0x34 at 0x5fa2d4 (12 bytes: call, mov edx,[eax],
// mov ecx,eax, call [edx+0x34]) becomes a call to the fix's own test - "the controls are locked, or the
// HUD shows a cinematic" - and a 7-byte nop; the `test al, al / je` after it is the game's. So in a
// cinematic the mouse takes the game's own "locked" path. Menus, popups and conversations get the mouse
// before this point and are not touched; mouse events a co-op peer sends (0x558dc4 / 0x558dfb) keep the
// game's own rule, so both PCs see the same events.

#include "limits_rules.hpp" // guard, matches, image_base

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>

namespace cutscene_mouse_rules
{
	using limits_rules::guard;
	using limits_rules::image_base;

	constexpr DWORD site = 0x5fa2d4;   // call 0x46dce0 / mov edx,[eax] / mov ecx,eax / call [edx+0x34]
	constexpr DWORD site_size = 12;
	constexpr DWORD game_getter = 0x46dce0;  // __cdecl: the game, 0x729960
	constexpr DWORD game_vtable = 0x686e1c;
	constexpr DWORD controls_locked_slot = 0x34;
	constexpr DWORD controls_locked = 0x469180;
	constexpr DWORD hud_getter = 0x59ee20;   // __cdecl: the HUD, 0x81d7e0
	constexpr DWORD hud_vtable = 0x69dca4;
	constexpr DWORD cinematic_slot = 0xec;
	constexpr DWORD cinematic = 0x59ec50;

	inline constexpr std::array<guard, 9> guards{{
		{site, "e8073ae7ff8b108bc8ff523484c0745a", "the mouse handler's lock test: the game, vt+0x34, test al / je 0x5fa33e (0x5fa2d4)"},
		{0x5fa2e4, "a120396f0033f6c60590a0a00000893485", "the locked path: the hover cleared (0x5fa2e4)"},
		{game_getter,
		 "64a1000000008a0d8ca072006aff681e38670050b80100000084c864892500000000752509058ca07200b960997200c744240800000000e834b1ffff68b0db6700e8f843200083c4048b0c24b86099720064890d0000000083c40cc3",
		 "the game's getter (0x46dce0)"},
		{game_vtable + controls_locked_slot, "80914600", "the game's vt+0x34: controls locked (0x469180)"},
		{controls_locked, "568bf1e8782118008b106a028bc8ff527484c07539e886f716008b108bc8ff920402000084c0752ad986f4030000",
		 "controls locked: popup, menu, lockControls' deadline (0x469180)"},
		{hud_getter,
		 "64a1000000008a0d94618a006aff685e84670050b80100000084c8648925000000007525090594618a00b9e0d78100c744240800000000e8e4f8ffff68a0e36700e8b8320d0083c4048b0c24b8e0d7810064890d0000000083c40cc3",
		 "the HUD's getter (0x59ee20)"},
		{hud_vtable + cinematic_slot, "50ec5900", "the HUD's vt+0xec: a cinematic is on (0x59ec50)"},
		{cinematic, "8a81b1890800c0e8022401c3", "a cinematic is on: bit 2 of HUD+0x889b1 (0x59ec50)"},
		{0x59b3e4, "8a96b189080080ca04", "cinematicStart sets the bit (0x59b3e4)"},
	}};

	// The first guard `image` (XMen2.exe's image base: 0x400000 in the game) doesn't match, or null.
	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
		{
			if (!limits_rules::matches(image + (g.va - image_base), g.hex))
			{
				return &g;
			}
		}
		return nullptr;
	}

	// What the twelve bytes at `site` become: a call to `test` (at its address in the game's process),
	// then a 7-byte nop.
	inline std::array<std::uint8_t, site_size> patch_bytes(const DWORD test)
	{
		std::array<std::uint8_t, site_size> bytes{0xe8, 0, 0, 0, 0, 0x0f, 0x1f, 0x80, 0, 0, 0, 0};
		const DWORD relative = test - (site + 5);
		std::memcpy(bytes.data() + 1, &relative, 4);
		return bytes;
	}

	// The decision the fix's test makes, as the keyboard's (0x425500) for these two causes.
	constexpr bool mouse_ignored(const bool controls_locked_now, const bool cinematic_now)
	{
		return controls_locked_now || cinematic_now;
	}
}
