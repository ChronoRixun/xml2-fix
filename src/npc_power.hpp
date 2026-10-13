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
// power only with at least half of the character's energy. Nothing a save reads changes.
namespace npc_power
{
	// The shared gameplay record's rule ID. Unassigned (0): the 0.2.2 integrator picks it (impl-022 rule: agents do
	// not pick IDs). Until then the rule is not registered and a peer with a different value isn't refused.
	constexpr std::uint16_t rule_id = 0;

	// Reads [Game] NpcPowerRule and patches XMen2.exe for it, all three sites or none. From DllMain (see exports.cpp).
	void install(HMODULE game);

	bool requested(); // the key says xml1, or is set to a value that is not xml1/xml2 (then unverified)
	bool installed(); // and every site carries the rule
	bool verified();  // nothing is half-installed: not requested, or installed
	std::array<std::uint32_t, 4> values(); // {installed ? 1 : 0, 0, 0, 0}
}
