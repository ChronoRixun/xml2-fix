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
//
// The engine (Alchemy: libIGDisplay's igWin32Window, libIGGfx's igDx8VisualContext) creates a
// Direct3D 8 device with Windowed = FALSE at the registry resolution. Borderless and windowed
// modes make the device windowed instead (IDirect3D8::CreateDevice and IDirect3DDevice8::Reset
// are hooked in the D3D8 vtables) and place the window themselves (the engine's CreateWindowExA,
// SetWindowLongA, SetWindowPos and MoveWindow calls are hooked in libIGDisplay's import table),
// so the desktop mode is never changed. The game's idea of its resolution comes from the
// registry: that read is answered with the desktop size in borderless mode, so its HUD and
// aspect ratio match the back buffer. The Video options list comes from
// IDirect3D8::EnumAdapterModes: it is completed with the desktop resolution and trimmed to the
// 20 entries the game has room for.

namespace display
{
	// Reads [Display] from xml2-fix.ini and installs the hooks it calls for. Without a Mode, nothing
	// changes - unless a frame hook was set, which needs the Direct3D device hooked whatever the mode.
	void install(HMODULE game);

	// For the test pipe's screenshots: `hook` runs on the game's render thread with its
	// IDirect3DDevice8 just before every Present. Set before install().
	void set_frame_hook(void (*hook)(void* device));

	// Creates and resets the device without multisampling (a multisampled back buffer can't be
	// copied). Set before install(); `why` goes in the log.
	void disable_multisampling(const char* why);
}
