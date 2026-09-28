#pragma once

// X-Men Legends II runs at 60 fps at most, whatever the monitor does: CClient::frame busy-spins
// at the end of every frame until 1/60 s has passed since the last one (the 1/60 is rewritten
// into the client object every frame, so alchemy.ini's max_fps never matters). With the fix's
// borderless or windowed modes on a 144 or 180 Hz desktop that leaves most refreshes without a
// frame, and the spin burns a core. [Display] in xml2-fix.ini changes that:
//
//   [Display]
//   FrameRate = 120          ; a number of fps, "refresh" (the desktop's rate) or 0 (unlimited);
//                            ; absent = the game's own 60 fps cap, untouched
//   VSync = 1                ; 1 or 0; absent = the engine's own presentation interval
//
// With FrameRate set, the constant the game writes is patched to 0.0 (after checking the bytes,
// so an unknown build is left alone and logged), which ends the spin at once, and the frame is
// paced instead in the display fix's IDirect3DDevice8::Present hook: a high-resolution waitable
// timer (Windows 10 1803 or later; timeBeginPeriod(1) and an ordinary timer otherwise) for all
// but the last part of the wait, then a short spin on QueryPerformanceCounter. The game's
// simulation runs on wall-clock time, so the frame rate doesn't change its speed.
//
// VSync on a fullscreen device is the presentation interval, applied by the display fix when the
// device is created or reset (display_rules.hpp, rewrite_present). In a window Direct3D 8 has no
// usable vsync - xml2_test measures it: a present with the default interval never waits for the
// vertical blank, and COPY_VSYNC, its only windowed sync, runs at about 32 fps under the desktop
// compositor - so VSync=1 in the borderless and windowed modes means frames paced here at the
// desktop's refresh rate, which the compositor shows without tearing. It never raises the rate
// above what FrameRate allows.

#include "frame_rate_rules.hpp"

#include <Windows.h>

#include <string>

namespace frame_rate
{
	// Remembers the setting (read from [Display] FrameRate by the display fix), switches the game's
	// spin off when it calls for it, and prepares the pacer. `desktop_refresh` is for "refresh";
	// `window_vsync` is VSync=1 in a windowed mode (see above). Must run before the game's first
	// frame.
	void install(HMODULE game, const frame_rate_rules::cap& setting, unsigned desktop_refresh, bool window_vsync);

	// Whether frames need pacing, so Present must be hooked.
	bool paces();

	// On the game's render thread, after every successful Present: paces the frame and counts it.
	void on_present();

	// Frames per second over the last full second, in tenths (0 until a second has passed), and
	// the setting in words - what the test pipe's "status" reports.
	unsigned measured_fps_x10();
	std::string describe();
}
