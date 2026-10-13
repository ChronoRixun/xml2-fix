#pragma once

#include <Windows.h>

// [Input] CutsceneMouse (on by default): while the HUD shows a cinematic, the mouse handler takes the game's
// own "controls locked" path - no hover outline, click-to-move or mouse powers - as the keyboard and pads
// already do there (cutscene_mouse_rules.hpp). Install before any game thread runs.
namespace cutscene_mouse
{
	void install(HMODULE game);
}
