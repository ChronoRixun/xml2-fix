#pragma once

#include <Windows.h>

#include <string>

// X-Men Legends 1's wall cling for the acrobat moveset (Beast, Nightcrawler, Psylocke): the FightMove handlers
// `ch_clingwalldecide` and `ch_clingwallbackflip`, which XMen2.exe never registers (both moves run as %default%):
//
//   [Game]
//   Xml1WallCling = xml1   ; jump, then jump again facing a wall within 24 units: turn to it and back-flip off it
//
// Without the key, empty, xml2 or off, nothing is patched and no agreement entry is registered (the released record).
// The research, every rule and every byte relied on are in xml1_cling_rules.hpp; docs/xml1-cling.md. A gameplay rule
// both co-op peers must share: rule 39 of the gameplay agreement.
namespace xml1_cling
{
	// Shared owner of the 0x4fd860 hook. Independent feature resolvers may install
	// it while cling is off; each is published only after the patch succeeds.
	using handler_resolver = void*(__cdecl*)(const char*);
	bool register_lookup(HMODULE game, handler_resolver resolver);
	void install(HMODULE game);
	std::string status(); // "wall cling xml1" / "wall cling game's own" / "wall cling unavailable"

	bool requested();
	bool installed();
	bool verified();
}
