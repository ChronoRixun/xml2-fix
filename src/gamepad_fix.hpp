#pragma once
#include <Windows.h>

// The fix presents every XInput-capable pad as a Logitech Dual Action (see pad_profile.hpp)
// and builds its DirectInput state from XInput in that controller's exact layout, so every
// pad looks the same to the game however it describes itself over USB or Bluetooth.

namespace pad_profile
{
	struct profile;
}

namespace gamepad_fix
{
	// [Input] CacheDeviceScan, configured before any DirectInput object exists.
	void cache_device_scan(bool enabled);

	// The controller to present. Defaults to pad_profile::logitech_dual_action.
	void use_profile(const pad_profile::profile& profile);

	// The one presented (the virtual pads are one too).
	const pad_profile::profile& profile();

	// Hooks a freshly created DirectInput object (IDirectInput, 2, 7 or 8; A or W) and the devices it creates;
	// its device lists and CreateDevice also serve the virtual pads ([Test] VirtualPads, virtual_pad.hpp).
	void hook_direct_input(void* direct_input);

	// Actual post-scan gameplay GUIDs (GUID_NULL for empty slots). Retains a
	// physical pad's XInput slot while it is attached, across list compaction.
	void update_attached(const GUID* instances, unsigned count);
}
