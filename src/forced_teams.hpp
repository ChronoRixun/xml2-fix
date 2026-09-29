#pragma once

#include <Windows.h>

// Forced parties for a mod's own campaign: eight script functions the game doesn't have, so a mod's
// scripts can seat the exact party a mission wants (the X-Men Legends 1 port: Magma alone in the
// mansion hubs, the flashbacks with their fixed heroes and costumes, Cyclops joining mid-zone):
//
//   xml2fixFeature("forcedteams")        1 when [Game] ForcedTeams=1 ("addhero": AddHero=1 too;
//                                        "joinhero": JoinHero isn't 0), else 0
//   seatParty("magma", "", "", "")       the party becomes exactly these heroes; the next statement loads
//   setSkinset("civilian", "magma")      XML1's mission costume for the listed heroes, default for the rest
//   pushParty("_ACTIVE_HERO_")           XML1's beginSideMission: zone, party and spot saved (2 at most)
//   popParty("mansion/man2/subbasement2") XML1's endSideMission: back to them, or the team menu at that zone;
//                                        a repeat before the first has run (or while its load runs) does nothing
//   addHero("cyclops")                   XML1's addHero: seated mid-zone by the game's own unused routine
//   getPartyMember(0)                    slot 0's hero, "" when empty
//   joinHero("cyclops")                  XML1's addHero as a reload: the spot saved, the hero added to the
//                                        saved party, the zone reloaded there with it (no team menu);
//                                        1 queued, 2 in the party already, 0 refused (the script falls back)
//
//   [Game]
//   ForcedTeams = 1   ; 1: the mod forces its campaign's parties; 0: the functions exist but report off
//                     ; (the mod's scripts open the team menu); absent: nothing patched
//   AddHero = 1       ; with ForcedTeams=1: addHero works (experimental); 0/absent: it returns 0 and the
//                     ; mod's scripts use their own fallback
//   JoinHero = 0      ; with ForcedTeams=1: joinHero returns 0 (the mod's fallback); absent or 1: it works
//
// A script checks xml2fixFeature first, into a variable declared with iadd(0, 0 ): without the fix (or
// without ForcedTeams) that line is dropped when the script compiles, the variable stays 0 and the
// script opens the team menu as before. Registration: the two push operands of the game's own
// registration (0x49fe30) point at a copy of its function table with the eight after its 289, all
// or nothing after every byte in forced_teams_rules::guards is compared with the retail build's.
// Every call is written to xml2-fix.log. forced_teams_rules.hpp has the details.
namespace forced_teams
{
	// Reads [Game] ForcedTeams, AddHero and JoinHero from xml2-fix.ini and registers the functions. Must run
	// before XMen2.exe's own code does - from DllMain (see exports.cpp).
	void install(HMODULE game);
}
