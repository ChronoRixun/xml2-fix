#pragma once

#include <Windows.h>

// The end of the game, for mods with their own campaign: XML2's credits end the game by loading XML2's
// last zone (act5/egypt/egypt6); the X-Men Legends 1 port plays XML1's closing movie and goes back to
// the main menu instead. And before that, once the credits have rolled, XML2's ending unlocks Deadpool
// with a popup ("Deadpool is now unlocked and available to play."), which a campaign without him leaves
// out.
//
//   [Game]
//   PostgameScript = x1/menus/postgame   ; Scripts\x1\menus\postgame.py runs in place of the egypt6 load
//   EndHeroUnlock = 0                    ; 0: the ending unlocks no hero and shows no popup; 1 or not set: Deadpool
//
// The win itself (the profile's "won" flag, Hard after a Normal win) and the end-of-game save stay. The
// research and every byte relied on are in postgame_rules.hpp. Without the keys nothing is patched.
namespace postgame
{
	void install(HMODULE game);
}
