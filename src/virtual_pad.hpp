#pragma once

// [Test] VirtualPads=N (0-4, default 0): the game sees N Logitech Dual Action pads with nothing
// plugged in, in both of its DirectInput enumerations (the engine's DirectInput 7 and XMen2.exe's
// own DirectInput 8), so tests can play with pads and in local co-op through the test pipe's pad
// commands (pad_input.hpp). They are COM objects of the fix's own (virtual_pad_rules.hpp has the
// decisions): virtual pad N is always listed N-th, so it lands in the game's pad slot N-1 -
// player N's pad in the fix's default bindings.
//
// They REPLACE the real controllers: while VirtualPads is set, the game's lists show no real game
// controller, and XInput pad N's input (while the game has the focus) goes into virtual pad N.
// So "pad 1" is the game's pad 1 whatever is plugged in, wakes up or goes to sleep during a run
// (the game re-lists its pads every few seconds and moves pads between slots when the list
// changes), and a real pad still plays as the same player.

#include <Windows.h>

namespace virtual_pad
{
	// Reads [Test] VirtualPads from xml2-fix.ini. Before the game's first DirectInput.
	void install();

	int count();

	bool is_virtual(REFGUID instance);

	// Lists the virtual pads to an EnumDevices callback (LPDIENUMDEVICESCALLBACKA, or W when
	// `wide`) of a DirectInput 8 (`di8`) or an older one. False when the callback asked to stop.
	bool enumerate(bool di8, bool wide, void* callback, void* user);

	// CreateDevice / CreateDeviceEx of a virtual pad: `iid` null is CreateDevice's own interface.
	HRESULT create(REFGUID instance, const IID* iid, bool di8, bool wide, void** device);
}
