#pragma once

// X-Men Legends II builds its Video options list into a table of 20 twelve-byte strings in
// XMen2.exe's data (0x6e9800) with no bounds check: a modern adapter lists more sizes than that
// (24 on the development PC) and the overflow overwrites the default key bindings that follow
// the table. The fix used to keep the list at 20 by trimming what Direct3D reports. This gives
// the game a 64-slot table of the DLL's own instead:
//
//   [Display]
//   ResolutionList = all     ; the default: 64 slots, the list below; game = the game's own 20
//                            ; slots, the list trimmed to 20 (never past the table)
//
// The seven instructions that carry the table's address (an imm32 each; the count next to it is
// a plain global every reader already uses) get the new table's address, after the 16 bytes at
// each of them have been compared with the retail build's - on any difference nothing is written,
// the game keeps its table and the list is trimmed to 20 as before. The list itself is built by
// the display fix's IDirect3D8::GetAdapterModeCount / EnumAdapterModes hooks in every mode
// (resolution_rules::build_list): the adapter's sizes, the desktop's, the forced Width x Height
// and, in a window, the common sizes of the desktop's aspect ratio and two render-scale presets,
// up to the table's slots. The game's own flow around a change (the restart warning, the registry
// save) is untouched: a resolution still applies at the next start.

#include "resolution_rules.hpp"

#include <Windows.h>

#include <cstddef>

namespace resolution_list
{
	// Relocates the game's table when `setting` is all and the code is the retail build's; returns
	// how many sizes the Video options list may hold (64, or the game's 20). Run before the game's
	// first frame, from display::install.
	std::size_t install(HMODULE game, display_rules::resolution_list setting);

	// The table in use: 64 slots when relocated, 20 otherwise.
	std::size_t capacity();
	bool relocated();
}
