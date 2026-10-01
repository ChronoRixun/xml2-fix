#pragma once

#include <Windows.h>

#include <string>

// The engine limit adjuster: raises three fixed caps of XMen2.exe in memory at startup, without
// touching the exe on disk.
//
// - The actor table (CAnimMotionCache, 40 slots for actor skins and animation databases). A zone
//   whose characters need a 41st slot gets NULL back without a word, and the first animation of the
//   actor that got it crashes the game (at 0x5743bb). Zone changes free the zone's slots, but on-demand
//   loads of skins and databases no package lists stay until the main menu, so a long session creeps
//   towards 40 and then crashes in a zone that loaded fine before.
// - The global resource name table (450 names), which every record of the actor table and the other
//   resource caches (models, textures, playfields, motion paths) registers in; when it is full, the
//   same quiet NULL. A bigger actor table fills it sooner, so it grows too.
// - The item manager's enhancement record pool (375 records): Data/items stops loading at the item
//   whose enhancement would be the 376th, and every item after it never exists; XML2's own table uses
//   374. Saves store record numbers, so the pool can only grow (limits_rules.hpp).
//
//   [Limits]
//   ActorSlots = 127        ; 41..127 (40 is the game's own)
//   ResourceNames = 1024    ; 451..4096 (450 is the game's own); with ActorSlots above 40 and no
//                           ; ResourceNames, 1024
//   ItemEnhancements = 512  ; 376..1024 (375 is the game's own): the item manager's enhancement
//                           ; record pool, past which Data/items stops loading
//
// Without the keys nothing is patched. With them, every instruction that carries the old cap (its
// displacements into the structure, its immediates) is compared with the retail build's bytes first:
// on any difference the structure keeps its game's own size (all or nothing), and the actor table is
// only raised with the name table. The actor table moves into a block of the DLL that the game's own
// constructor builds and its own destructor tears down at exit; the name table is built by the DLL,
// with the game's own constructor, and put where the game's getter looks for it. limits_rules.hpp has
// the tables and the layouts.
namespace limits
{
	// Reads [Limits] from xml2-fix.ini and raises what it asks for. Must run before XMen2.exe's own
	// code does - from DllMain (see exports.cpp): the game first asks for both structures in its
	// start-up, from the CPrecacheMgr constructor.
	void install(HMODULE game);

	// For the test pipe's status: "actors 23/127; names 301/1024; motions 312/500; igb 150/200; items
	// 374/512" (live count/cap; "-" where this isn't the retail XMen2.exe or the structure doesn't exist yet).
	std::string status();
}
