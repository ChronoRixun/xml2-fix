#pragma once
#include <Windows.h>
#include <string>

namespace test_mode
{
	void install(HMODULE game);
	bool hotkeys_enabled();
	// Called on the game thread after pipe/physical keys have been merged.
	void keyboard(const unsigned char* state);
	// Window messages are used when InputPipe is off; neither path executes gameplay here.
	bool window_key(UINT message, WPARAM key, LPARAM flags);
	void watch_window(HWND window);
	bool console_frame_hook_matches();
	void input_lost();
	std::string status();
}
