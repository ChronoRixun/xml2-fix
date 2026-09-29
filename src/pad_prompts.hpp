#pragma once

#include <Windows.h>

// Button prompts for the pad. X-Men Legends II (PC) names every button prompt after the keyboard -
// "[E] Talk to Jean", the skills screen's power wheel, the menus' hint bar, the tutorial hints - even for
// a player on a pad, and its power wheel shows the team menu's fixed keys ([Esc] for Smash, the menus'
// back key) rather than the ones that fire the powers. [Input] in xml2-fix.ini:
//
//   [Input]
//   Prompts = auto       ; auto: each player's prompts follow the device they last used; pad; keyboard;
//                        ; off: the game's own prompts, nothing patched. Unset: auto.
//   PromptColors = 1     ; A, B, X, Y as the letter in the Xbox colours; 0: "[A]" like the rest
//
// With auto, pad or keyboard, the label function's only call (CStrings::get, 0x4bd739) is replaced by
// the fix's, the per-frame input poll's call (0x61c479) samples the keyboard, mouse and pads the game just
// read, and the power wheel's four tokens are read from the bindings of play. pad_prompts_rules.hpp has
// the details and every retail byte this relies on; anything else and nothing is patched (logged).

namespace pad_prompts
{
	// Reads [Input]; patches XMen2.exe when its code is the retail build's. Before the game's code runs.
	void install(HMODULE game);
}
