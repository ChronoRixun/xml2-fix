#pragma once

#include <Windows.h>

#include <string>

// X-Men Legends 1's combat numbers, for a mod that carries XML1's characters (the X-Men Legends 1 port):
//
//   [Balance]
//   EnemyHealthPerBody = 10   ; health per body point of every character the mod didn't scale (the game's own: 3)
//   HeroStatScale = 4         ; what the mod multiplied its heroes' stats by
//   NativeStatHeroes = A,B    ; the herostat entries it didn't
//   StrengthBonus = 0.1       ; a hit's bonus per strength point (0.05); scaled heroes: that / HeroStatScale
//   MindBonus = 0.1           ; per mind point, mental attacks (0.01)
//   MightBonus = 0.05         ; per rank of the talent might (none)
//   PowerAttacks = xml1       ; DamageScale="x1power" means none: no stat bonus, never a miss
//   HitChance = xml1          ; XML1's melee hit chance
//   Rounding = xml1           ; a hit's stat and affecter bonuses worked out on the roll and rounded down, as XML1's
//   DamageRolls = xml1        ; inclusive integer damage draws (the game's own: continuous float)
//   MeleeWeapons = ...        ; mod-generated name:type:bonus:damage-bits records, separated by |
//
// and for tuning HitChanceBase, HitChancePerPoint, HeroStrengthBonus, HeroMindBonus, HeroHealthPerBody. Every key is
// on its own; without one, that part of the game is untouched. The research, every byte relied on and every byte
// written are in balance_rules.hpp.
namespace balance
{
	// Reads [Balance] and patches XMen2.exe for it. From DllMain (see exports.cpp): the style files, whose
	// DamageScale names the parser reads, load with the first character.
	void install(HMODULE game);

	// For the stats screen's help text (stat_help): the scaled heroes' live per-point numbers, or false when
	// that part of the game stays the retail one (the key unset, or the patch refused) - what the health
	// maximums and the hits actually use, so the text never names a number the game doesn't apply.
	bool hero_health_per_body(float* out);
	bool hero_strength_per_point(float* out);

	// For the test pipe's status: "balance health=10/3 str=0.1/0.025 mind=0.1/0.025 might=0.05 power=xml1
	// hit=xml1(30,0.025) rounding=xml1", or "balance off".
	std::string status();
}
