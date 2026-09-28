#pragma once

// X-Men Legends II builds its Video options list into a table of 20 twelve-byte strings in
// XMen2.exe's data (0x6e9800) with no bounds check: a modern adapter lists more sizes than that
// (24 on the development PC) and the overflow overwrites the default key bindings that follow
// the table. In its own modes (Mode set) the fix keeps the list at 20 by trimming what Direct3D
// reports; the game's own mode is left as it ships. [Display] ResolutionList goes further:
//
//   [Display]
//   ResolutionList = all     ; a 64-slot table of the DLL's in place of the game's, and the fix's
//                            ; list (below) in every mode, the game's own included
//   ResolutionList = game    ; the game's own 20 slots, the fix's list trimmed to 20 in every mode
//                            ; (the overflow fix alone)
//                            ; absent: as before - the fix's list only in its own modes, 20 slots
//
// With `all`, the seven instructions that carry the table's address (an imm32 each; the count next
// to it is a plain global every reader already uses) get the new table's address, after the 16
// bytes at each of them have been compared with the retail build's - on any difference nothing is
// written, the game keeps its table and the list is trimmed to 20. That happens only once the
// display fix has hooked the engine's Direct3DCreate8 (so the mode-list hooks that keep the list
// within 64 are sure to be there); they answer for whatever adapter the game asks about. The list
// (resolution_rules::build_list): the adapter's sizes, the desktop's, the forced Width x Height and,
// in a window, the common sizes of the desktop's aspect ratio and two render-scale presets, up to
// the table's slots. The game's own flow around a change (the restart warning, the registry save)
// is untouched: a resolution still applies at the next start.

#include "resolution_rules.hpp"

#include <Windows.h>

#include <cstddef>

namespace resolution_list
{
	// Relocates the game's table when `setting` is all and the code is the retail build's; returns
	// how many sizes the Video options list may hold (64, or the game's 20). Run before the game's
	// first frame, from display::install, and only once the mode-list hooks are certain.
	std::size_t install(HMODULE game, display_rules::resolution_list setting);

	// The table in use: 64 slots when relocated, 20 otherwise.
	std::size_t capacity();
	bool relocated();
}
