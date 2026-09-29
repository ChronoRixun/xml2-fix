#pragma once

#include <Windows.h>

// The game window's title, for a mod that is another game (the X-Men Legends 1 port):
//
//   [Game]
//   WindowTitle=X-Men Legends   ; the window's, the taskbar's and alt-tab's name; not set: "X-Men Legends 2"
//
// libIGDisplay.dll's CreateWindowExA and SetWindowTextA are hooked in its import table and, for the
// engine's window only, pass this title on; the game's string (also its registry root and save
// folder) is left alone. The rules and what the game does are in window_title_rules.hpp. Without the
// key nothing is hooked.
namespace window_title
{
	void install();
}
