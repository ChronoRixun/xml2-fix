#pragma once

#include <Windows.h>

// X-Men Legends II only runs in exclusive fullscreen, switches the display to the resolution in
// its registry settings (1920x1080 on a 2560x1440 desktop, say) and won't list the desktop's own
// resolution in its Video options. [Display] in xml2-fix.ini changes that:
//
//   [Display]
//   Mode = borderless        ; fullscreen | borderless | windowed; unset = the game's own behaviour
//   Width = 0                ; force a resolution; 0 = the desktop size (borderless) or the game's setting
//   Height = 0
//   Topmost = 0              ; borderless/windowed: keep the window above others
//   RunInBackground = 1      ; borderless/windowed: keep playing when another window has the focus
//   FrameRate = 120          ; fps, "refresh" or 0 (unlimited); unset = the game's own 60 fps cap (frame_rate.hpp)
//   VSync = 1                ; 1 / 0; unset = the engine's own presentation interval
//   InGameOptions = 1        ; the rows for these in the game's Advanced Options panel (options_menu.hpp)
//   ResolutionList = all     ; all = a 64-slot resolution table in place of the game's 20 (resolution_list.hpp);
//                            ; game = the game's own table, the list trimmed to 20
//
// The engine (Alchemy: libIGDisplay's igWin32Window, libIGGfx's igDx8VisualContext) creates a
// Direct3D 8 device with Windowed = FALSE at the registry resolution. Borderless and windowed
// modes make the device windowed instead (IDirect3D8::CreateDevice and IDirect3DDevice8::Reset
// are hooked in the D3D8 vtables) and place the window themselves (the engine's CreateWindowExA,
// SetWindowLongA, SetWindowPos and MoveWindow calls are hooked in libIGDisplay's import table),
// so the desktop mode is never changed. The game's idea of its resolution comes from the
// registry: that read is answered with the desktop size in borderless mode, so its HUD and
// aspect ratio match the back buffer. The Video options list comes from
// IDirect3D8::EnumAdapterModes, hooked in every mode: it is completed with the desktop
// resolution (and, in a window, the common sizes of its aspect ratio) and kept within the
// resolution table's slots, since the game writes it there without a bounds check. FrameRate and
// VSync work in any mode, the game's own included: VSync through the same CreateDevice/Reset
// rewrite, FrameRate through the frame limiter (frame_rate.hpp) run from the Present hook. The
// in-game rows (options_menu.hpp) change FrameRate, VSync (in a window) and RunInBackground while
// the game runs, through set_* below.

#include "display_rules.hpp"

namespace display
{
	// Reads [Display] from xml2-fix.ini and installs the hooks it calls for. Without a Mode,
	// FrameRate or VSync, nothing about the window or the frame timing changes; the engine's
	// Direct3D 8 is hooked in every case, for the Video options list (and Present, when a frame
	// hook was set or the in-game options are on).
	void install(HMODULE game);

	// [Display] as xml2-fix.ini has it now (the in-game rows show this when the panel opens; the
	// launcher may have edited the file since the game started). Unknown values are logged.
	display_rules::options read_ini_options();

	// What the fix runs with, and the desktop's refresh rate at start (0 if unknown).
	const display_rules::options& current_options();
	unsigned desktop_refresh_rate();

	// Live changes from the in-game rows (Accept). The frame rate retargets the limiter, switching
	// the game's own 60 fps spin off first if it still runs; VSync is a pacing change in a window
	// (returns true) and the presentation interval of the next device creation or reset in
	// fullscreen (false); RunInBackground takes effect at the next frame.
	void set_frame_rate(const frame_rate_rules::cap& setting);
	bool set_vsync(bool on);
	void set_run_in_background(bool on);

	// For the test pipe's screenshots: `hook` runs on the game's render thread with its
	// IDirect3DDevice8 just before every Present. Set before install().
	void set_frame_hook(void (*hook)(void* device));

	// Creates and resets the device without multisampling (a multisampled back buffer can't be
	// copied). Set before install(); `why` goes in the log.
	void disable_multisampling(const char* why);

	// Borderless/windowed: shows the game's window without taking the focus, so an automated run
	// starts behind whatever the owner is using. Set before install(); `why` goes in the log.
	// (Independently of this, in borderless/windowed modes the game never sees, moves or clips the
	// cursor while another window has the focus.)
	void show_without_focus(const char* why);
}
