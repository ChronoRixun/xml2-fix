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
// visibility uses XML1's geometry and shared query reservation/budget. Revision 4 additionally schedules combo
// presses and completion using XML1's delays. Revision 10 (5-9 were unreleased candidates, see below): a hit that makes an NPC react (pain or block) spends the
// combo steps that come due before its brain runs again, and the power re-checks and attack decisions that came due then, as
// XML1's brain - which keeps running while the body reacts - does; XMen2.exe resumed the combo (or fired the power)
// from idle. Nothing a save reads changes.
namespace npc_power
{
	// Reads [Game] NpcPowerRule and patches XMen2.exe for it, every site in one transaction. From DllMain.
	void install(HMODULE game);

	bool requested(); // the key says xml1, or is set to a value that is not xml1/xml2 (then unverified)
	bool installed(); // and every site carries the rule
	bool verified();  // nothing is half-installed: not requested, or installed
	// Rule 16: word 0 enables the power/combo rules; word 1 enables XML1 visibility geometry and query scheduling;
	// words 2 and 3 are the opt-in officer weapon brain ([Game] NpcSemiAuto=xml1, npc_semi_auto.hpp: installed, source
	// fingerprint), zero without it. Revision 10 adds the reaction-spent combo steps, power re-checks and attack
	// decisions and carries the officer weapon brain; revision 4 per-press and final combo delays; revision 3
	// visibility. Revisions 5-9 were unreleased candidates and are never reused: 5 a withdrawn role experiment (and
	// the first reaction-spent build, beta6-interrupt), 6-9 the officer weapon candidates (9 = the held-fire brain
	// now carried by revision 10). A peer on any of them is refused by revision.
	constexpr std::uint16_t revision = 10;
	// Released LC peers already advertise rule 16 revision 3 when disabled.
	constexpr std::uint16_t revision_for(bool requested) { return requested ? revision : 3; }
	std::array<std::uint32_t, 4> values();
}
