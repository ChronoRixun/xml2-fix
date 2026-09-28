#pragma once

// X-Men Legends II's Advanced Options panel (Options > Controls > Advanced) has a left pane
// with two rows, Resolution and FSAA, and two hundred empty pixels under them. The display
// settings the fix adds - [Display] Mode, FrameRate, VSync, RunInBackground in xml2-fix.ini -
// could only be edited in that file (or by the launcher). This puts them in the panel:
//
//   Display mode        Fullscreen / Borderless / Windowed     (restart)
//   Frame rate          30 / 60 / 120 / 144 / 165 / 180 / 240 / Refresh / Unlimited   (live)
//   VSync               Off / On           (live in a window, restart in fullscreen)
//   Run in background   Off / On           (live)
//
// The panel is hand-coded Direct3D UI (Beenox "BXIG" widgets), not a menu file, so the rows are
// the game's own BXIGCycle objects, built like its FSAA row by a function the fix puts in place
// of the builder's final call (0x61f356, the "start the enter animation" call): four cycles, a
// status label, their navigation records, and the FSAA/Accept records relinked so the keyboard
// walks Resolution > FSAA > our rows > Accept. The engine draws, animates, hit-tests and
// navigates them; the fix supplies the row callback (value changed: dirty flag, click sound,
// status line; focus: the highlight bar, moved by an animation function of ours in the bar's
// free slot 5) and three more call-site replacements in the panel's close function: Accept
// (0x61f8a4, after the game's own registry save) writes the changed keys with
// WritePrivateProfileStringW and applies what can apply live; Cancel (0x61f8be) discards;
// Revert to default (0x61f667) puts the rows back to the stock values, persisted only by Accept.
//
// Every address is the retail build's; the bytes at each call site and the first bytes of every
// game function used are compared first, and on any difference the panel is left as it is (logged).
// With the rows never touched the game behaves as before: no key is written and nothing but the
// four call sites is patched (the display fix's Direct3D create/reset hooks, there so a frame rate
// picked here can start the limiter at once, pass every call through unchanged; Present is hooked
// only when the limiter starts). [Display] InGameOptions=0 turns the rows off.

#include <Windows.h>

namespace options_menu
{
	// Checks the game's code and, when it is the retail build, patches the four call sites. Run
	// after display::install has read [Display]; needs the display fix's device hooks for the live
	// frame-rate changes (display.hpp: set_frame_rate and friends).
	void install(HMODULE game);

	// Whether the rows are in place (the call sites were patched).
	bool installed();
}
