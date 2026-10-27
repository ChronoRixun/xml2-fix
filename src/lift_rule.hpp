#pragma once

#include <Windows.h>

#include <string>

// X-Men Legends 1's pickup, throw impact, carried throw damage and telekinesis rules (revision 2),
// for the XML1 port's converted objects with XML1 heaviness 4/5 in `xml1heaviness`:
//
//   [Game]
//   LiftRule = xml1   ; objects that carry xml1heaviness are lifted by XML1's rule (rank + 1 >= heaviness, 5 never)
//
// Without the key, empty, xml2 or off, nothing is patched and no agreement entry is registered (the released record).
// The research and every byte relied on are in lift_rule_rules.hpp; docs/lift-rule.md. A gameplay rule both co-op
// peers must share: rule 38 of the gameplay agreement.
namespace lift_rule
{
	// The existing handle-keyed XML1 consumer value, shared with rule46.
	int heaviness(void* object);
	void install(HMODULE game);
	std::string status(); // "lift rule xml1" / "lift rule game's own" / "lift rule unavailable"

	bool requested();
	bool installed();
	bool verified();
}
