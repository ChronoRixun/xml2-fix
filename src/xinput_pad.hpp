#pragma once

#include <cstdint>
#include <vector>

// The pads, read through XInput. XInput 1.4's first call finds the controllers through Windows'
// device access broker (deviceaccess.dll, COM), which can take several seconds - on the game's
// thread that froze its start-up long enough for Windows to call it "not responding". So XInput is
// loaded and asked once on a thread of its own from the DLL's start (start()); until that has
// returned, every read here answers "no pad" at once, and the pad path shows the game an idle pad.
namespace xinput_pad
{
	constexpr int max_pads = 4;

	struct raw_state
	{
		std::uint16_t buttons; // XINPUT_GAMEPAD_* bits
		std::uint8_t left_trigger, right_trigger;
		std::int16_t left_x, left_y, right_x, right_y;
	};

	// Loads XInput and makes its first call on a thread of its own (DllMain: it runs once the loader
	// is done). Without it, the first read does both, on the reader's thread.
	void start();

	// XInput is still loading on its own thread: reads answer "no pad" without waiting for it.
	bool starting();

	// Unprocessed XInput state (no deadzones); false if the pad is not connected, or XInput isn't ready.
	bool read_raw(int index, raw_state& out);

	// Indices of the currently connected XInput pads, in slot order (none until XInput is ready).
	std::vector<int> connected_indices();
}
