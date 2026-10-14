#pragma once
#include <Windows.h>
#include <string>

namespace test_mode
{
	void install(HMODULE game);
	bool hotkeys_enabled();
	// Called on the game thread after pipe/physical keys have been merged.
	void keyboard(const unsigned char* state);
	void input_lost();
	std::string status();
}
