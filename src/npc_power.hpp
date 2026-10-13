#pragma once

#include <Windows.h>

#include <array>
#include <cstdint>

// X-Men Legends 1's power use for computer-controlled characters, for a mod that carries the first game (the X-Men
// Legends 1 port):
//
//   [Game]
//   NpcPowerRule = xml1   ; enemies pay for their powers and decide on melee-range powers as in X-Men Legends 1
//
// Without the key, or with xml2, nothing is patched. With it (npc_power_rules.hpp, docs/npc-power-rule.md): a
// power's cost comes off an enemy's energy as in XML1 (the power cost's AI test at 0x4318e7), the melee-range power
// decision (0x518770) runs every 2.0 s (0.5 s for a party hero under the AI) instead of every 0.3 s, and it picks a
// power only with at least half of the character's energy; NPCs fight in the first game's melee combos (their combo
// level as XML1's AI init gives it, XML1's six combos and weights) and pause after starting one as in XML1. AI
// visibility uses XML1's geometry and shared query reservation/budget. Nothing a save reads changes.
namespace npc_power
{
	// Reads [Game] NpcPowerRule and patches XMen2.exe for it, every site in one transaction. From DllMain.
	void install(HMODULE game);

	bool requested(); // the key says xml1, or is set to a value that is not xml1/xml2 (then unverified)
	bool installed(); // and every site carries the rule
	bool verified();  // nothing is half-installed: not requested, or installed
	// Rule 16: word 0 enables the power/combo rules; word 1 enables XML1 visibility geometry and query scheduling.
	// Revision 3 advertises the added value; revision 2 had only the power/combo word.
	constexpr std::uint16_t revision = 3;
	std::array<std::uint32_t, 4> values();
}
