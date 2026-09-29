#pragma once

// Discord Rich Presence: while the game runs, a friend looking at your Discord profile sees what you
// are playing - "X-Men Legends II, Act 1 · Sanctuary, Wolverine Lv 12 · Storm Lv 11", or the menus, a
// cutscene, the Danger Room's course, an online game's player count - under the X-Men Legends
// application for the X-Men Legends I port and the X-Men Legends II one otherwise. Only the zone, the
// party's heroes and levels and the mode are shared: no player names, no addresses. On unless
// xml2-fix.ini says otherwise:
//
//   [Discord]
//   Enabled=1       ; 0 (or false/no/off): no presence at all
//   ShowZone=1      ; 0: where you are stays private ("Playing")
//   ShowParty=1     ; 0: your heroes stay private
//   LargeImage=     ; an art asset's key in the Discord application, when it has one (none by default)
//   SmallImage=
//   Game=           ; xml1 or xml2: which application, when the detection is wrong
//   ClientId=       ; another Discord application's id, for testing
//
// It talks to the Discord client on this PC through its local pipe (discord_ipc.hpp) from a thread of
// its own, and reads the game's state from its memory once a second, every read SEH-guarded, after
// checking every byte it relies on is the retail build's (discord_rules.hpp). Discord not running, or
// closed later, is looked for again every 20 seconds, quietly. The presence is cleared when the game
// quits (its ExitProcess), and by Discord itself when the pipe closes.

#include <Windows.h>

namespace discord_presence
{
	// Reads [Discord] and, in XMen2.exe, starts the presence's thread.
	void install(HMODULE game);
}
