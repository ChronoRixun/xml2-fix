#pragma once

// The pads the test pipe presses (pad_input_rules.hpp has the commands): what it holds on each
// pad, merged into every read of that pad, and how often the game has read each one, so a tap
// can wait until the game has seen it - as test_input does for the keyboard.
//
// Pad N is the game's pad N: virtual pad N with [Test] VirtualPads (virtual_pad.hpp), else the
// N-th real pad the fix presents (gamepad_fix.cpp).

#include "pad_input_rules.hpp"
#include "xinput_pad.hpp"

#include <string>

namespace pad_input
{
	// The test pipe is on (test_input::install): reads are merged and counted from now on. Until
	// then on_read does nothing, so a game without the pipe never logs a word of it.
	void enable();

	// From a device read of pad `index` (0-based): merges what the pipe holds on it into `state`
	// and counts the read. `game`: a read through the game's own DirectInput 8 (a tap waits for
	// one of those when that pad has such a device, the engine's DirectInput 7 reads aside).
	void on_read(int index, xinput_pad::raw_state& state, bool game);

	// A virtual pad's device of the game's own DirectInput 8 came or went (see on_read).
	void game_device(int index, bool created);

	// The pipe's side, on its thread: one parsed pad command -> "ok ..." or "error ...". Taps, holds
	// and timed sticks/triggers block until they are over and the game has read the pad meanwhile.
	std::string handle(const pad_input_rules::pad_command& command);

	void release_all();

	// "virtual pads 2; pad reads 120/118/0/0" for the pipe's status line.
	std::string status();
}
