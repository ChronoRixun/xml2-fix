#pragma once

#include <Windows.h>

namespace display_settings
{
	// The game's own display-settings persistence, fixed: the boot FSAA switch learns the
	// Advanced Options panel's 8x option, and (`guard_wipe`) a failed display device no longer
	// resets the player's Resolution/FSAA registry settings. Installed only once the engine's
	// Direct3D device is hooked: 8 samples rely on the device hook's clamp, and the wipe guard on
	// its fallbacks. Nothing here hooks Direct3D or the window. See display_settings_rules.hpp.
	void install(HMODULE game, bool guard_wipe);
	// -1 unless the existing retail guards and FSAA transaction succeeded.
	int requested_fsaa_index();
}
