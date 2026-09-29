#pragma once

#include <Windows.h>

// The main menu's item names, for mods with a main menu of their own: XMen2.exe's main menu finds the
// items its mouse handler hit-tests and its Quit item by XML2's names (label_option04..09, debug_text,
// debug, debug_focus); the X-Men Legends 1 port uses XML1's menu, whose buttons are button1..button8.
//
//   [Game]
//   MainMenuItems = button1,button2,button3,button4,button5,button6,button7,button8
//
// The names go, in order, to label_option04, 05, 06, 07, 08, 09 (the mouse's slots), debug_text (Quit:
// its text, the quit flag on accept), debug and debug_focus (the Quit button's models); a slot left
// out keeps the game's name. An eighth name is a seventh mouse slot instead of a Quit model: the
// mouse focuses and accepts that item itself (the port's Play Online). The research and every byte
// relied on are in main_menu_rules.hpp. Without the key nothing is patched.
namespace main_menu
{
	void install(HMODULE game);
}
