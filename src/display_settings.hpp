#pragma once

#include <Windows.h>

namespace display_settings
{
	// The game's own display-settings persistence, fixed: the boot FSAA switch learns the
	// Advanced Options panel's 8x option, and a failed display device no longer resets the
	// player's Resolution/FSAA registry settings. Installed in every display mode; nothing
	// here hooks Direct3D or the window. See display_settings_rules.hpp for the sites.
	void install(HMODULE game);
}
