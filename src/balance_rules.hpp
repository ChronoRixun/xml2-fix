#pragma once

// [Balance]: X-Men Legends 1's combat numbers in XMen2.exe, for a mod that carries XML1's characters (the X-Men
// Legends 1 port: its npcstat is XML1's, its heroes' stats XML1's times a scale), kept apart from the patching so
// xml2_test can check it without the game: the ini values' rules, the formulas, every retail byte the change relies
// on, every byte it writes, the code of its two stubs, and the functions the patched game calls.
//
// The research is in the xml1-port repository (the balance design, issue 14 of the port); every XMen2.exe address
// below was read again from the retail build for this code (image base 0x400000, no relocations) and is in `guards`,
// every XML1 one from XML1's default.xbe (Xbox, 2004; its addresses as loaded at 0x10000).
//
// What the two games do, and what each key changes. Every key is on its own: a key that isn't set leaves its part
// of the game exactly as it is, and a key whose retail bytes aren't there is refused with a line in the log.
//
//   Max health (0x4b8850; XML1 0xafa90). Both: 25 + 5 level + P body, then x npchealthscale (0.8 unless the entry
//     says otherwise) and the player count for a character that isn't on the hero team, rounded up. P is 3 here
//     (the float at 0x6d9770, read at 0x4b89f7) and 10 in XML1.
//       EnemyHealthPerBody = P for every character whose stats the mod didn't scale; HeroHealthPerBody = P for the
//       ones it did (see "scaled heroes" below). The `fld dword [0x6d9770]` at 0x4b89f7 becomes a call to a stub
//       that loads the one or the other and leaves every register and flag alone (the `je` at 0x4b8a09 still
//       uses the flags of the `test edi, edi` at 0x4b89e0).
//     The other reader of that float, 0x4b8912, is Hard's formula for enemies (25 + 5 (level - 50) + 3 (level - 45),
//     no body in it): a level term, left alone. Easy's halving and an npcstat specific_health stay the game's too.
//   Stat bonus on a hit (0x453a60; XML1 0x5cca0). Here a hit whose DamageScale is `normal` does R + ceil((m - 1) R),
//     m = 1 + 0.05 strength (0x4b7940), or 1 + 0.01 mind (0x4b7970) for a mental attack (the attack's type bits
//     0xf00); XML1: R + trunc(0.1 stat R) for both, unless the attack is a power attack.
//       StrengthBonus / MindBonus = the bonus per point for an unscaled character, and that divided by HeroStatScale
//       per stored point for a scaled hero; HeroStrengthBonus / HeroMindBonus = the scaled heroes' per stored point,
//       whatever the other says. The two calls (0x453b6e, 0x453b67) go to functions of the same shape that call the
//       game's own - so its stat affecters stay - and answer 1 + points x the new bonus. MindBonus also redirects
//       the stats screen's call (0x5e0afa), so the screen shows what the hits do.
//   Rounding. This game builds a hit in two steps, each rounded up: hit = roll + ceil(stat bonus x roll), then
//     + ceil(hit x (M - 1) + A) for the attacker's damage affecters (their multiplier M and what they add, A;
//     0x453b81, 0x453be2). XML1 works both out on the roll and rounds both down: roll + trunc(0.1 stat x roll) +
//     trunc(roll x (M - 1) + A), M with might's factor in it (0x5cdb6, 0x5ce02); its roll is a whole number
//     (0x1233c0), this game's a float. On a 4-5 point punch with strength 3 and might 2 that is 7-8 here against
//     XML1's 5-6.
//       Rounding = xml1: the two multiplier functions above read the roll (the damage function's [esp+0x20]) and
//       answer the multiplier whose rounded-up bonus is XML1's rounded-down one; the damage affecter function below
//       does the same for XML1's affecter bonus (it also reads the hit, [esp+0x24], and A, [esp+0x10]). Both
//       bonuses are worked out on the roll rounded to a whole number, as XML1's rolls are, in XML1's own order of
//       operations (so a strength of 4 on a roll of 5 adds 2, as there, and not 1 for want of a bit). Sets up
//       all three even without StrengthBonus / MindBonus / MightBonus (then with the game's own 0.05 / 0.01 / no
//       might).
//   Might (XML1 0x5cd5c: the talent `might` at rank 1 / 2 / 3 multiplies every attack by 1.05 / 1.10 / 1.15; nothing
//     here: this game's `might` only lifts).
//       MightBonus = the bonus per rank, ranks above 3 as 3. The second of the damage function's two reads of the
//       attacker's damage affecters (0x453bc7: kind 0x39, mode 1 = the multiplier) goes to a function that calls
//       the game's and multiplies its answer. The talent's rank is read as the game reads one (0x4b9090, 0x4bbda9):
//       CStats+0x20 holds 8-byte entries, the talent's id in the first word, its rank in the low 6 bits of the
//       third byte, the count at CStats+0xbc. The id's talent comes from the talent registry (0x4c05b0, its vt+0x14
//       = 0x4bdbe0 with a pointer to the id); the talent's name is the `name` attribute of the data node it keeps at
//       +8, read with the game's own attribute lookup (0x5649c0), as the talent's description getters read theirs
//       (0x4be400). The string the talent's own vt+0x14 returns (+0xc, what 0x4b9090 compares) is not its name: it
//       is empty for every talent of the port's.
//   Power attacks (XML1: an attack flagged PowerAttack gets no stat bonus and is never rolled to hit). This game has
//     the same switch under another name: DamageScale `none` (0) - the stat bonus is for `normal` (1) only
//     (0x453b54), and a miss counts only for `normal` (0x45333f). Its parser (0x44ec00, table 0x6d6dec: none 0,
//     normal 1, difficulty 2, "" 3) reads any other name as `normal`.
//       PowerAttacks = xml1: the parser's one call (0x4dc10f) goes to a function that reads the name `x1power` as
//       `none` and hands every other name to the game's. A mod writes DamageScale="x1power" where XML1 had
//       PowerAttack; without the key that is `normal`, as before.
//   Melee hit chance (0x44f450; XML1 0x5c240). Here: 3 A / (A + D) x attacker level / (both levels), A = 5 speed -
//     35, D = 3 speed (mind for a mental attack), clamped to 5 %..95 %; XML1: (30 + (speed + level of the attacker)
//     - (speed + level of the defender)) x 0.025, at least 10 %.
//       HitChance = xml1: the two pushes at 0x44f684, where the game draws its random number against the chance at
//       [esp+0x20], become a jump to a stub that stores XML1's chance there first (HitChanceBase, HitChancePerPoint:
//       the 30 and the 0.025; a scaled hero's speed or mind divided by HeroStatScale). Everything before it stays
//       the game's: which attacks are rolled at all, the always-hit cases, and its own chance's overflow above 95 %
//       that feeds its critical hits (0x44f655), which is why the stub sits after that code and not in its place.
//
// Scaled heroes. HeroStatScale is what the mod multiplied its heroes' four stats by (the port: 4), and
// NativeStatHeroes the herostat entries it didn't. A character is a scaled hero when its stats came from the
// herostat - the object at CStats+0xc0 has the vtable 0x685294 (written at 0x44a436; an npcstat character's is
// 0x68511c, 0x44983a) - and its name (CStats+0x150) isn't in the list. An allied NPC and an enemy copy of a hero are
// npcstat characters: unscaled.
//
// Nothing here is saved: stats, levels and XP stay as they are in the data and in a save.

#include "limits_rules.hpp" // guard, matches, hex_byte, hex_size, image_base

#include <intrin.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace balance_rules
{
	using limits_rules::guard;
	using limits_rules::image_base;

	// ---- The ini values -------------------------------------------------------------------------------

	// The game's own numbers.
	constexpr float stock_health_per_body = 3.0f; // 0x6d9770
	constexpr float stock_strength_bonus = 0.05f; // 0x682f98
	constexpr float stock_mind_bonus = 0.01f;     // 0x682a2c
	// XML1's (default.xbe 0x44f1fc; 0x3c8240; the caller's table at 0x5cd82; 0x3c8368, 0x3cbda4, 0x3c8240).
	constexpr float xml1_health_per_body = 10.0f;
	constexpr float xml1_stat_bonus = 0.1f;
	constexpr float xml1_might_bonus = 0.05f;
	constexpr int might_max_rank = 3;
	constexpr float xml1_hit_base = 30.0f;
	constexpr float xml1_hit_per_point = 0.025f;
	constexpr float xml1_hit_floor = 0.1f;

	// A number asked for, or why the value isn't one. `set` false and `error` empty: the key isn't set.
	struct number_choice
	{
		float value = 0.0f;
		bool set = false;
		std::string error;
	};

	// A plain decimal number from `low` to `high` (digits, at most one '.'); a ';' starts a comment.
	inline number_choice parse_number(const std::string_view raw, const float low, const float high)
	{
		const auto value = ini_rules::value_text(raw);
		if (value.empty())
		{
			return {};
		}
		bool plain = value.size() <= 12;
		int dots = 0;
		int digits = 0;
		for (const char c : value)
		{
			if (c == '.')
			{
				++dots;
			}
			else if (c >= '0' && c <= '9')
			{
				++digits;
			}
			else
			{
				plain = false;
			}
		}
		const std::string text(value);
		const float number = plain && dots <= 1 && digits > 0 ? std::strtof(text.c_str(), nullptr) : -1.0f;
		if (!(number >= low && number <= high))
		{
			char range[96];
			std::snprintf(range, sizeof range, "isn't a number from %g to %g", low, high);
			number_choice refused;
			refused.error = range;
			return refused;
		}
		return {number, true, {}};
	}

	enum class rules
	{
		xml2,
		xml1,
	};

	struct rules_choice
	{
		rules value = rules::xml2;
		bool set = false;
		std::string error;
	};

	inline std::string lowered(const std::string_view text)
	{
		std::string lower(text);
		for (auto& c : lower)
		{
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return lower;
	}

	// "xml1" or "xml2", any case; a ';' starts a comment.
	inline rules_choice parse_rules(const std::string_view raw)
	{
		const auto value = ini_rules::value_text(raw);
		if (value.empty())
		{
			return {};
		}
		const auto lower = lowered(value);
		if (lower == "xml1")
		{
			return {rules::xml1, true, {}};
		}
		if (lower == "xml2")
		{
			return {rules::xml2, true, {}};
		}
		rules_choice refused;
		refused.error = "isn't xml1 or xml2";
		return refused;
	}

	// NativeStatHeroes: names apart by commas, lower case, the empty ones dropped.
	inline std::vector<std::string> parse_names(const std::string_view raw)
	{
		std::vector<std::string> names;
		auto rest = ini_rules::value_text(raw);
		while (!rest.empty())
		{
			const auto comma = rest.find(',');
			const auto name = ini_rules::trimmed(rest.substr(0, comma));
			if (!name.empty())
			{
				names.push_back(lowered(name));
			}
			if (comma == std::string_view::npos)
			{
				break;
			}
			rest.remove_prefix(comma + 1);
		}
		return names;
	}

	struct melee_weapon
	{
		std::string name;
		int attack_type = 0;
		int bonus = 0;
		DWORD damage_type = 0;
	};

	// A mod generates these records from its own source data; no enemy-name table.
	inline std::optional<std::vector<melee_weapon>> parse_melee_weapons(const std::string_view raw)
	{
		const std::string text(ini_rules::value_text(raw));
		std::vector<melee_weapon> out;
		std::size_t start = 0;
		while (start < text.size())
		{
			const auto end = text.find('|', start);
			const std::string row = text.substr(start, end == std::string::npos ? end : end - start);
			std::array<std::string, 4> fields;
			std::size_t at = 0;
			for (int i = 0; i < 4; ++i)
			{
				const auto colon = row.find(':', at);
				if ((i < 3) != (colon != std::string::npos)) return std::nullopt;
				fields[i] = row.substr(at, colon == std::string::npos ? colon : colon - at);
				at = colon == std::string::npos ? row.size() : colon + 1;
			}
			if (fields[0].empty() || fields[0].size() > 63 || out.size() >= 16 ||
			    !std::all_of(fields[0].begin(), fields[0].end(), [](const char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; })) return std::nullopt;
			std::array<unsigned long, 3> numbers{};
			for (int i = 0; i < 3; ++i)
			{
				const auto& value = fields[i + 1];
				if (value.empty() || value.size() > 6 || !std::all_of(value.begin(), value.end(), [](const char c) { return c >= '0' && c <= '9'; })) return std::nullopt;
				numbers[i] = std::strtoul(value.c_str(), nullptr, 10);
			}
			if (numbers[0] < 1 || numbers[0] > 3 || numbers[1] > 32000 ||
			    (numbers[2] != 128 && numbers[2] != 32768 && numbers[2] != 131072) ||
			    std::any_of(out.begin(), out.end(), [&](const melee_weapon& w) { return w.name == fields[0]; })) return std::nullopt;
			out.push_back({fields[0], static_cast<int>(numbers[0]), static_cast<int>(numbers[1]), static_cast<DWORD>(numbers[2])});
			if (end == std::string::npos) break;
			start = end + 1;
			if (start == text.size()) return std::nullopt;
		}
		return out;
	}

	// The section as text, a key's value as the profile API hands it over ("" when the key is missing).
	struct ini_values
	{
		std::string enemy_health_per_body, hero_health_per_body, hero_stat_scale, native_stat_heroes, strength_bonus, mind_bonus, hero_strength_bonus, hero_mind_bonus,
			might_bonus, power_attacks, hit_chance, hit_chance_base, hit_chance_per_point, rounding, damage_rolls, melee_weapons;
	};

	// What [Balance] asks for.
	struct settings
	{
		std::optional<float> enemy_health_per_body; // per body point, unscaled characters
		std::optional<float> hero_health_per_body;  // per stored body point, scaled heroes
		float hero_stat_scale = 1.0f;
		std::vector<std::string> native_stat_heroes; // lower case
		std::optional<float> strength_bonus, mind_bonus;           // per point, unscaled characters (and scaled heroes / the scale)
		std::optional<float> hero_strength_bonus, hero_mind_bonus; // per stored point, scaled heroes
		std::optional<float> might_bonus;                          // per rank of `might`
		bool power_attacks_xml1 = false;
		bool hit_chance_xml1 = false;
		bool rounding_xml1 = false; // the stat bonus and might round down
		bool damage_rolls_xml1 = false; // uniform integer damage draws, independent of bonus rounding
		std::vector<melee_weapon> melee_weapons;
		float hit_chance_base = xml1_hit_base;
		float hit_chance_per_point = xml1_hit_per_point;
		// One line for each value that was refused: "[Balance] Key=value isn't ... - what stays".
		std::vector<std::string> refused;

		bool health() const { return enemy_health_per_body || hero_health_per_body; }
		bool strength() const { return strength_bonus || hero_strength_bonus || rounding_xml1; }
		bool mind() const { return mind_bonus || hero_mind_bonus || rounding_xml1; }
		bool might() const { return might_bonus.has_value() || rounding_xml1; }
		bool any() const { return health() || strength() || mind() || might() || power_attacks_xml1 || hit_chance_xml1 || damage_rolls_xml1 || !melee_weapons.empty(); }

		float health_per_body(const bool scaled_hero) const
		{
			return scaled_hero ? hero_health_per_body.value_or(stock_health_per_body) : enemy_health_per_body.value_or(stock_health_per_body);
		}
		float strength_per_point(const bool scaled_hero) const
		{
			if (!scaled_hero)
			{
				return strength_bonus.value_or(stock_strength_bonus);
			}
			return hero_strength_bonus ? *hero_strength_bonus : strength_bonus ? *strength_bonus / hero_stat_scale : stock_strength_bonus;
		}
		float mind_per_point(const bool scaled_hero) const
		{
			if (!scaled_hero)
			{
				return mind_bonus.value_or(stock_mind_bonus);
			}
			return hero_mind_bonus ? *hero_mind_bonus : mind_bonus ? *mind_bonus / hero_stat_scale : stock_mind_bonus;
		}
	};

	inline settings parse(const ini_values& ini)
	{
		settings s;
		const auto number = [&](const char* key, const std::string& raw, const float low, const float high, const char* stays) -> std::optional<float> {
			const auto chosen = parse_number(raw, low, high);
			if (!chosen.error.empty())
			{
				s.refused.push_back(std::string("[Balance] ") + key + "=" + std::string(ini_rules::value_text(raw)) + " " + chosen.error + " - " + stays);
			}
			return chosen.set ? std::optional<float>(chosen.value) : std::nullopt;
		};
		const auto choice = [&](const char* key, const std::string& raw, const char* stays) {
			const auto chosen = parse_rules(raw);
			if (!chosen.error.empty())
			{
				s.refused.push_back(std::string("[Balance] ") + key + "=" + std::string(ini_rules::value_text(raw)) + " " + chosen.error + " - " + stays);
			}
			return chosen.set && chosen.value == rules::xml1;
		};
		s.enemy_health_per_body = number("EnemyHealthPerBody", ini.enemy_health_per_body, 1.0f, 50.0f, "the game's own health stays");
		s.hero_health_per_body = number("HeroHealthPerBody", ini.hero_health_per_body, 1.0f, 50.0f, "the heroes' own health stays");
		s.hero_stat_scale = number("HeroStatScale", ini.hero_stat_scale, 1.0f, 8.0f, "heroes count as unscaled (1)").value_or(1.0f);
		s.native_stat_heroes = parse_names(ini.native_stat_heroes);
		s.strength_bonus = number("StrengthBonus", ini.strength_bonus, 0.0f, 1.0f, "the game's own strength bonus stays");
		s.mind_bonus = number("MindBonus", ini.mind_bonus, 0.0f, 1.0f, "the game's own mind bonus stays");
		s.hero_strength_bonus = number("HeroStrengthBonus", ini.hero_strength_bonus, 0.0f, 1.0f, "the heroes follow StrengthBonus");
		s.hero_mind_bonus = number("HeroMindBonus", ini.hero_mind_bonus, 0.0f, 1.0f, "the heroes follow MindBonus");
		s.might_bonus = number("MightBonus", ini.might_bonus, 0.0f, 0.5f, "no damage from the talent might, as in the game");
		s.power_attacks_xml1 = choice("PowerAttacks", ini.power_attacks, "x1power stays normal, as the game reads it");
		s.hit_chance_xml1 = choice("HitChance", ini.hit_chance, "the game's own roll stays");
		s.rounding_xml1 = choice("Rounding", ini.rounding, "the game's own rounding up stays");
		s.damage_rolls_xml1 = choice("DamageRolls", ini.damage_rolls, "the game's own float damage draws stay");
		if (auto weapons = parse_melee_weapons(ini.melee_weapons))
		{
			s.melee_weapons = std::move(*weapons);
		}
		else
		{
			s.refused.push_back("[Balance] MeleeWeapons isn't a bounded name:type:bonus:damage-bits list - no melee weapon bonus");
		}
		s.hit_chance_base = number("HitChanceBase", ini.hit_chance_base, 0.0f, 60.0f, "X-Men Legends 1's 30 is used").value_or(xml1_hit_base);
		s.hit_chance_per_point = number("HitChancePerPoint", ini.hit_chance_per_point, 0.001f, 0.1f, "X-Men Legends 1's 0.025 is used").value_or(xml1_hit_per_point);
		return s;
	}

	// ---- The formulas ---------------------------------------------------------------------------------

	// 0x4b8850 at Normal for a character with no health affecter: 25 + 5 level + per_body x body as a float, x the
	// non-hero scale as a float (1 for the hero team), rounded up (0x672348), at least 1, at most 1,000,000.
	inline int max_health(const int level, const int body, const float per_body, const float scale)
	{
		volatile float health = 25.0f + 5.0f * static_cast<float>(level) + per_body * static_cast<float>(body);
		health = health * scale;
		const double rounded = std::ceil(static_cast<double>(health));
		return rounded < 1.0 ? 1 : rounded > 1000000.0 ? 1000000 : static_cast<int>(rounded);
	}

	// The stat points in the game's multiplier m = 1 + points x stock: the whole number they were (a stat with its
	// affecters, 0x4b7780's int).
	inline float points_in(const float game_multiplier, const float stock)
	{
		return std::round((game_multiplier - 1.0f) / stock);
	}

	// The game's multiplier turned into 1 + points x wanted.
	inline float rescaled(const float game_multiplier, const float stock, const float wanted)
	{
		if (wanted == stock)
		{
			return game_multiplier;
		}
		return 1.0f + points_in(game_multiplier, stock) * wanted;
	}

	// The multiplier for a roll whose bonus the game will round up - roll + ceil((m - 1) roll) - so that the result
	// is the bonus rounded down: roll + floor(bonus). Half a point under the whole number, so the game's float
	// arithmetic can't land on the wrong side of it.
	inline float rounded_down(const float roll, const float bonus)
	{
		const float whole = std::floor(bonus);
		return roll > 0.0f && whole >= 1.0f ? 1.0f + (whole - 0.5f) / roll : 1.0f;
	}

	// What the game makes of a roll and a multiplier: roll + ceil((m - 1) roll), as 0x453b73-0x453b86 and
	// 0x453bcc-0x453bf7 do it (for xml2_test).
	inline float game_bonus(const float roll, const float multiplier)
	{
		volatile float product = multiplier * roll;
		return roll + static_cast<float>(std::ceil(static_cast<double>(product) - static_cast<double>(roll)));
	}

	// XML1's stat bonus for a roll (default.xbe 0x5cdea-0x5ce02): trunc((1 + per_point x points) x roll - roll) on
	// its whole-number roll, in more than float precision as its x87 does it.
	inline float xml1_bonus_for(const float roll, const float points, const float per_point)
	{
		const double whole_roll = std::round(static_cast<double>(roll));
		return static_cast<float>(std::trunc((1.0 + static_cast<double>(per_point) * points) * whole_roll - whole_roll));
	}

	// XML1's affecter bonus (0x5cda2-0x5cdb6), trunc(roll x M - roll + A) on its whole-number roll, as the multiplier
	// that makes the game's own second step - hit + ceil(hit x (m - 1) + A) - add exactly that.
	inline float affecters_on_roll(const float roll, const float hit, const float multiplier, const float added)
	{
		if (!(hit > 0.0f))
		{
			return multiplier;
		}
		const double whole_roll = std::round(static_cast<double>(roll));
		const auto whole = static_cast<float>(std::trunc(whole_roll * multiplier - whole_roll + added));
		return 1.0f + (whole - 0.5f - added) / hit;
	}

	// The game's second step, for xml2_test: what hit + ceil(hit x (m - 1) + A) comes to.
	inline float game_affected(const float hit, const float multiplier, const float added)
	{
		volatile float product = multiplier * hit;
		return hit + static_cast<float>(std::ceil(static_cast<double>(product) - static_cast<double>(hit) + static_cast<double>(added)));
	}

	inline float might_factor(const float per_rank, const int rank)
	{
		return 1.0f + per_rank * static_cast<float>(rank < 0 ? 0 : rank > might_max_rank ? might_max_rank : rank);
	}

	// XML1's chance (default.xbe 0x5c40c): (base + attacker - defender) x per_point, at least 10 %; each side is its
	// speed (or mind) + its level. No cap: above 100 % always hits.
	inline float xml1_hit_chance(const float attacker, const float defender, const float base, const float per_point)
	{
		const float chance = (attacker + base - defender) * per_point;
		return chance < xml1_hit_floor ? xml1_hit_floor : chance;
	}

	// The game's own (0x44f5d3-0x44f683), for comparison: A = 5 speed - 35, D = 3 speed, each at least 1.
	inline float xml2_hit_chance(const int attacker_speed, const int attacker_level, const int defender_speed, const int defender_level)
	{
		const float a = std::max(1.0f, 5.0f * static_cast<float>(attacker_speed) - 35.0f);
		const float d = std::max(1.0f, 3.0f * static_cast<float>(defender_speed));
		const float chance = 3.0f * a / (a + d) * static_cast<float>(attacker_level) / static_cast<float>(attacker_level + defender_level);
		return chance < 0.05f ? 0.05f : chance > 0.95f ? 0.95f : chance;
	}

	// ---- The game's objects ---------------------------------------------------------------------------

	constexpr DWORD hero_extension_vtable = 0x685294; // the object at CStats+0xc0 of a herostat character (0x44a436)
	constexpr DWORD npc_extension_vtable = 0x68511c;  // of an npcstat character (0x44983a)
	constexpr std::size_t stats_extension = 0xc0;
	constexpr std::size_t stats_name = 0x150;
	constexpr std::size_t stats_talents = 0x20;       // 8-byte entries: the id (a word), then the rank in 6 bits
	constexpr std::size_t stats_talent_count = 0xbc;
	constexpr int stats_talent_capacity = 19;          // (0xbc - 0x20) / 8
	constexpr std::size_t actor_stats = 0x35c;
	constexpr std::size_t attack_type_bits = 0x10;     // 0xf00: a mental attack (0x453a6c, 0x44f484)
	constexpr int stat_speed = 1;
	constexpr int stat_mind = 3;
	constexpr int damage_scale_none = 0;
	constexpr const char* power_attack_name = "x1power";
	constexpr const char* might_name = "might";

	template <typename T>
	T read_at(const void* object, const std::size_t offset)
	{
		T value{};
		std::memcpy(&value, static_cast<const std::uint8_t*>(object) + offset, sizeof value);
		return value;
	}

	// Whether `stats` (a CStats) is a herostat character the mod scaled: `hero_vtable` at its extension object, and
	// a name that isn't in the list.
	inline bool scaled_hero(const void* stats, const settings& s, const DWORD hero_vtable = hero_extension_vtable)
	{
		if (!stats)
		{
			return false;
		}
		const auto* extension = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(read_at<DWORD>(stats, stats_extension)));
		if (!extension || read_at<DWORD>(extension, 0) != hero_vtable)
		{
			return false;
		}
		const char* name = static_cast<const char*>(stats) + stats_name;
		for (const auto& native : s.native_stat_heroes)
		{
			if (_strnicmp(name, native.c_str(), native.size() + 1) == 0)
			{
				return false;
			}
		}
		return true;
	}

	// ---- What the patched game calls ---------------------------------------------------------------------

	// The game's own functions the change calls, as the fix sees them (xml2_test puts its own here).
	using multiplier_t = float(__fastcall*)(void* stats, void* edx, int zero, void* actor, int out); // 0x4b7940, 0x4b7970: __thiscall, ret 0xc
	using affecter_t = unsigned char(__fastcall*)(void* actor, void* edx, int kind, float* out, void* attack, int mode); // 0x550410: __thiscall, ret 0x10
	using parser_t = int(__cdecl*)(const char* name);                    // 0x44ec00
	using stat_t = int(__cdecl*)(void* stats, int index, void* actor);   // 0x4b7780 through the fix: a stat with the actor's affecters
	using level_t = int(__cdecl*)(void* stats);                          // 0x4b87c0 through the fix
	using talent_name_t = const char*(__cdecl*)(int id);                 // the registry's talent and its node's `name` through the fix; null without one
	using integer_roll_t = int(__cdecl*)(int high, int low, int stream); // 0x5601e0: inclusive, accepts either bound order
	using float_roll_t = float(__cdecl*)(float high, float low, int stream); // 0x5601b0

	struct runtime
	{
		settings chosen;
		DWORD hero_vtable = hero_extension_vtable;
		multiplier_t strength = nullptr;
		multiplier_t mind = nullptr;
		affecter_t affecter = nullptr;
		parser_t parser = nullptr;
		stat_t stat = nullptr;
		level_t level = nullptr;
		talent_name_t talent_name = nullptr;
		integer_roll_t integer_roll = nullptr;
		float_roll_t float_roll = nullptr;
		float health[2] = {stock_health_per_body, stock_health_per_body}; // [0] unscaled, [1] scaled heroes: what the health stub loads
	};

	inline runtime live;

	// At attack construction, after its base draw and before any contact/to-hit
	// roll. Change the newly built attack, never a shared definition or missed hit.
	inline void __cdecl melee_weapon_for(void* actor, void* attack)
	{
		if (!actor || !attack) return;
		void* stats = reinterpret_cast<void*>(static_cast<std::uintptr_t>(read_at<DWORD>(actor, actor_stats)));
		if (!stats) return;
		const char* name = static_cast<const char*>(stats) + stats_name;
		for (const auto& weapon : live.chosen.melee_weapons)
		{
			if (_strnicmp(name, weapon.name.c_str(), weapon.name.size() + 1) != 0 || read_at<int>(attack, 0xc) != weapon.attack_type) continue;
			const float damage = read_at<float>(attack, 4) + static_cast<float>(weapon.bonus);
			std::memcpy(static_cast<std::uint8_t*>(attack) + 4, &damage, sizeof damage);
			std::memcpy(static_cast<std::uint8_t*>(attack) + attack_type_bits, &weapon.damage_type, sizeof weapon.damage_type);
			return;
		}
	}

	// Only damage draws at 0x453a51 and 0x453add call this; hit/chance draws keep
	// their float distribution. Do not round a continuous draw: wider ranges would
	// underweight their endpoints. Reuse the game's inclusive integer RNG and stream.
	inline float __cdecl damage_roll(const float high, const float low, const int stream)
	{
		if (!std::isfinite(high) || !std::isfinite(low) || std::abs(high) > 32000.0f || std::abs(low) > 32000.0f)
		{
			return live.float_roll(high, low, stream);
		}
		return static_cast<float>(live.integer_roll(static_cast<int>(high), static_cast<int>(low), stream));
	}

	// The health stub's question: the float to load for this CStats.
	inline const float* __cdecl health_per_body_for(const void* stats)
	{
		return &live.health[scaled_hero(stats, live.chosen, live.hero_vtable) ? 1 : 0];
	}

	// The damage function's frame (0x453a60, after its three pushes): the roll at [esp+0x20], the hit after its stat
	// bonus at [esp+0x24], the damage affecters' multiplier at [esp+0xc] and what they add at [esp+0x10].
	constexpr std::size_t frame_roll = 0x20;
	constexpr std::size_t frame_hit = 0x24;
	constexpr std::size_t frame_multiplier = 0xc;
	constexpr std::size_t frame_added = 0x10;

	// The multiplier a hit gets from the game's own `game` (1 + points x stock): 1 + points x per_point, or with
	// Rounding=xml1 the one that adds XML1's bonus for this roll.
	inline float hit_multiplier(const float game, const float stock, const float per_point, const float roll)
	{
		return live.chosen.rounding_xml1 ? rounded_down(roll, xml1_bonus_for(roll, points_in(game, stock), per_point)) : rescaled(game, stock, per_point);
	}

	// In place of 0x4b7940 at 0x453b6e: called with three arguments pushed on the damage function's frame, so the
	// roll is 4 (the return address) + 12 + 0x20 bytes above where this function's return address sits.
	__declspec(noinline) inline float __fastcall strength_multiplier(void* stats, void* edx, const int zero, void* actor, const int out)
	{
		const float roll = read_at<float>(_AddressOfReturnAddress(), 4 + 12 + frame_roll);
		const float game = live.strength(stats, edx, zero, actor, out);
		return hit_multiplier(game, stock_strength_bonus, live.chosen.strength_per_point(scaled_hero(stats, live.chosen, live.hero_vtable)), roll);
	}

	// In place of 0x4b7970 at 0x453b67: as the strength one.
	__declspec(noinline) inline float __fastcall mind_multiplier(void* stats, void* edx, const int zero, void* actor, const int out)
	{
		const float roll = read_at<float>(_AddressOfReturnAddress(), 4 + 12 + frame_roll);
		const float game = live.mind(stats, edx, zero, actor, out);
		return hit_multiplier(game, stock_mind_bonus, live.chosen.mind_per_point(scaled_hero(stats, live.chosen, live.hero_vtable)), roll);
	}

	// In place of 0x4b7970 at 0x5e0afa, the stats screen: the plain multiplier (no roll there).
	inline float __fastcall mind_multiplier_shown(void* stats, void* edx, const int zero, void* actor, const int out)
	{
		const float game = live.mind(stats, edx, zero, actor, out);
		return rescaled(game, stock_mind_bonus, live.chosen.mind_per_point(scaled_hero(stats, live.chosen, live.hero_vtable)));
	}

	// The rank of the talent `might` in a CStats, 0 without one.
	inline int might_rank(const void* stats)
	{
		if (!stats || !live.talent_name)
		{
			return 0;
		}
		const int count = std::min(read_at<int>(stats, stats_talent_count), stats_talent_capacity);
		for (int i = 0; i < count; ++i)
		{
			const std::size_t entry = stats_talents + 8u * static_cast<std::size_t>(i);
			const char* name = live.talent_name(read_at<std::uint16_t>(stats, entry));
			if (name && _stricmp(name, might_name) == 0)
			{
				return read_at<std::uint8_t>(stats, entry + 2) & 0x3f;
			}
		}
		return 0;
	}

	// In place of 0x550410 at 0x453bc7 (kind 0x39, mode 1: the attacker's damage multiplier, 1.0 when it has none):
	// x might's factor. `out` is the damage function's [esp+0xc], so the roll, the hit and what the affecters add are
	// 0x14, 0x18 and 4 bytes above it; with Rounding=xml1 the answer is the multiplier that adds XML1's bonus.
	inline unsigned char __fastcall damage_multiplier(void* actor, void* edx, const int kind, float* out, void* attack, const int mode)
	{
		const unsigned char found = live.affecter(actor, edx, kind, out, attack, mode);
		if (!actor || !out)
		{
			return found;
		}
		const auto* stats = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(read_at<DWORD>(actor, actor_stats)));
		const int rank = live.chosen.might_bonus ? might_rank(stats) : 0;
		const float multiplier = rank > 0 ? *out * might_factor(*live.chosen.might_bonus, rank) : *out;
		if (live.chosen.rounding_xml1)
		{
			*out = affecters_on_roll(read_at<float>(out, frame_roll - frame_multiplier), read_at<float>(out, frame_hit - frame_multiplier), multiplier,
			                         read_at<float>(out, frame_added - frame_multiplier));
		}
		else
		{
			*out = multiplier;
		}
		return found;
	}

	// In place of 0x44ec00 at 0x4dc10f.
	inline int __cdecl damage_scale(const char* name)
	{
		if (name && _stricmp(name, power_attack_name) == 0)
		{
			return damage_scale_none;
		}
		return live.parser(name);
	}

	// The hit stub's question: XML1's chance for this attacker, defender and attack. As XML1 reads the two stats
	// (0x5c270, 0x5c2ec): speed with the defender's affecters, or for a mental attack mind with the attacker's.
	inline float __cdecl hit_chance_for(void* attacker, void* defender, void* attack)
	{
		const bool mental = attack && (read_at<DWORD>(attack, attack_type_bits) & 0xf00) != 0;
		const int index = mental ? stat_mind : stat_speed;
		void* context = mental ? attacker : defender;
		const auto side = [&](void* actor) {
			auto* stats = actor ? reinterpret_cast<void*>(static_cast<std::uintptr_t>(read_at<DWORD>(actor, actor_stats))) : nullptr;
			if (!stats)
			{
				return 0.0f;
			}
			const float scale = scaled_hero(stats, live.chosen, live.hero_vtable) ? live.chosen.hero_stat_scale : 1.0f;
			return static_cast<float>(live.stat(stats, index, context)) / scale + static_cast<float>(live.level(stats));
		};
		return xml1_hit_chance(side(attacker), side(defender), live.chosen.hit_chance_base, live.chosen.hit_chance_per_point);
	}

	// ---- The two stubs --------------------------------------------------------------------------------

	inline void put_dword(std::vector<std::uint8_t>& code, const std::uint32_t value)
	{
		for (int i = 0; i < 4; ++i)
		{
			code.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
		}
	}

	inline void put_bytes(std::vector<std::uint8_t>& code, const std::initializer_list<std::uint8_t> bytes)
	{
		code.insert(code.end(), bytes);
	}

	// Called from 0x4b89f7 in place of `fld dword [0x6d9770]`: esi = the CStats, one value on the x87 stack. Loads
	// the float `question(esi)` points at; every register and flag as it was.
	inline std::vector<std::uint8_t> health_stub(const DWORD question)
	{
		std::vector<std::uint8_t> code{0x9c, 0x60, 0x56, 0xb8}; // pushfd; pushad; push esi; mov eax, question
		put_dword(code, question);
		put_bytes(code, {0xff, 0xd0, 0x83, 0xc4, 0x04, 0xd9, 0x00, 0x61, 0x9d, 0xc3}); // call eax; add esp, 4; fld dword [eax]; popad; popfd; ret
		return code;
	}

	// Jumped to from 0x44f684: ebx = the attacker, edi = the defender, esi = the attack, the chance at [esp+0x20],
	// nothing on the x87 stack, eax, ecx and edx free. Stores `question(ebx, edi, esi)` as the chance, does the two
	// pushes the jump replaced (push 1; push 1.0f) and goes on at `resume` (0x44f68b).
	inline std::vector<std::uint8_t> hit_stub(const DWORD question, const DWORD resume)
	{
		std::vector<std::uint8_t> code{0x56, 0x57, 0x53, 0xb8}; // push esi; push edi; push ebx; mov eax, question
		put_dword(code, question);
		// call eax; add esp, 12; fstp dword [esp+0x20]; push 1; push 1.0f; push resume
		put_bytes(code, {0xff, 0xd0, 0x83, 0xc4, 0x0c, 0xd9, 0x5c, 0x24, 0x20, 0x6a, 0x01, 0x68, 0x00, 0x00, 0x80, 0x3f, 0x68});
		put_dword(code, resume);
		code.push_back(0xc3); // ret
		return code;
	}

	constexpr DWORD hit_resume = 0x44f68b;
	// Called from 0x4dc37b: st0 the base draw, ESI the newly built attack, EBP
	// the attacker. Store the draw as before, apply the profile preserving all
	// registers/flags, then replay mov eax,[ebp+0x35c]. No extra x87 values remain.
	inline std::vector<std::uint8_t> melee_stub(const DWORD question)
	{
		std::vector<std::uint8_t> code{0xd9, 0x5e, 0x04, 0x9c, 0x60, 0x56, 0x55, 0xb8};
		put_dword(code, question);
		put_bytes(code, {0xff, 0xd0, 0x83, 0xc4, 0x08, 0x61, 0x9d, 0x8b, 0x85, 0x5c, 0x03, 0x00, 0x00, 0xc3});
		return code;
	}

	// ---- The patch ------------------------------------------------------------------------------------

	enum feature : unsigned
	{
		health = 1,
		strength = 2,
		mind = 4,
		might = 8,
		power_attacks = 16,
		hit_chance = 32,
		damage_rolls = 64,
		melee_weapons = 128,
	};

	inline unsigned features_of(const settings& s)
	{
		return (s.health() ? health : 0u) | (s.strength() ? strength : 0u) | (s.mind() ? mind : 0u) | (s.might() ? might : 0u) | (s.power_attacks_xml1 ? power_attacks : 0u) |
		       (s.hit_chance_xml1 ? hit_chance : 0u) | (s.damage_rolls_xml1 ? damage_rolls : 0u) | (!s.melee_weapons.empty() ? melee_weapons : 0u);
	}

	inline const char* feature_name(const unsigned f)
	{
		switch (f)
		{
		case health: return "health";
		case strength: return "strength bonus";
		case mind: return "mind bonus";
		case might: return "might";
		case power_attacks: return "power attacks";
		case hit_chance: return "hit chance";
		case damage_rolls: return "damage rolls";
		case melee_weapons: return "melee weapons";
		}
		return "?";
	}

	// What each feature leaves as it is when it is refused.
	inline const char* feature_stays(const unsigned f)
	{
		switch (f)
		{
		case health: return "the game's own health stays";
		case strength: return "the game's own strength bonus stays";
		case mind: return "the game's own mind bonus stays";
		case might: return "no damage from the talent might, as in the game";
		case power_attacks: return "x1power stays normal, as the game reads it";
		case hit_chance: return "the game's own roll stays";
		case damage_rolls: return "the game's own float damage draws stay";
		case melee_weapons: return "no melee weapon bonus";
		}
		return "nothing changes";
	}

	// What a site's new bytes are: a call or a jump to one of the fix's.
	enum class target
	{
		health_stub,        // call + nop
		strength_multiplier, // call
		mind_multiplier,
		mind_multiplier_shown,
		damage_multiplier,
		damage_scale,
		hit_stub,           // jmp + 2-byte nop
		damage_roll,
		damage_roll_tail,
		melee_stub,
	};

	struct site
	{
		DWORD va;
		std::string_view retail;
		unsigned feature;
		target to;
		const char* what;
	};

	inline constexpr std::array<site, 10> sites{{
		{0x4b89f7, "d90570976d00", health, target::health_stub, "max health: the health per body point, by whose stats they are (was fld dword [0x6d9770], 3.0)"},
		{0x453b6e, "e8cd3d0600", strength, target::strength_multiplier, "a hit: the strength multiplier (was call 0x4b7940)"},
		{0x453b67, "e8043e0600", mind, target::mind_multiplier, "a hit: the mind multiplier (was call 0x4b7970)"},
		{0x5e0afa, "e8716eedff", mind, target::mind_multiplier_shown, "the stats screen: the mind multiplier (was call 0x4b7970)"},
		{0x453bc7, "e844c80f00", might, target::damage_multiplier, "a hit: the attacker's damage multiplier, x the talent might (was call 0x550410)"},
		{0x4dc10f, "e8ec2af7ff", power_attacks, target::damage_scale, "DamageScale: x1power reads as none (was call 0x44ec00)"},
		{0x44f684, "6a01680000803f", hit_chance, target::hit_stub, "the melee roll: X-Men Legends 1's chance, then the game's draw (was push 1; push 1.0f)"},
		{0x453add, "e8cec61000", damage_rolls, target::damage_roll, "random damage: inclusive integer draw returned as float (was call 0x5601b0)"},
		{0x453a51, "e95ac71000", damage_rolls, target::damage_roll_tail, "attack construction: inclusive integer base damage (was tail jump to 0x5601b0)"},
		{0x4dc37b, "d95e048b855c030000", melee_weapons, target::melee_stub, "melee weapon: store the base draw and add its bonus/type before the contact roll"},
	}};

	struct feature_guard
	{
		unsigned features; // every feature that relies on these bytes
		guard bytes;
	};

	// Every byte of XMen2.exe the change relies on, read from the retail build; every site lies inside one. A
	// feature is applied only when all of its guards match; xml2_test compares them with a copy of the exe.
	inline constexpr std::array<feature_guard, 26> guards{{
		{health,
		 {0x4b8850,
		  "51a168976d00558b6c241456578b7c2418558bf18b4c241857516a028bce8944241ce809efffff0fbfd08954241cdb44241cd95c2418e85554fbff8b108bc8ff926802000083f801"
		  "0f8e3b0100008b86a002000083f81e740983f81f0f85270100008b8ec00000008b01ff900c01000085c07e188b8ec00000008b11ff920c0100008944241cdb44241ceb3e8bcee8dd"
		  "feffff0fbfc083e8328944241cdb44241cd9056c976d00d8c9d844240cd9c9d80534006800e872981b008944241cdb44241cd80d70976d00dec18bced95c240ce89bfeffff85ff0f"
		  "bfc8894c241cdb44241cd83d5c2c6800d84c240cd80dc02c6800d8052ce56800d95c240c0f84fe0100008b875c03000085c00f847f0100008bcfe8b918f6ff83f81d0f846f010000"
		  "8b8ec00000008b11ff9224010000d84c240cd95c240ce8b554fdff8b108bc8ff520483f8010f8e44010000e8a054fdff8b108bc8ff525c84c00f8530010000e88c54fdff8b108bc8"
		  "ff5204488944241cdb44241cd80da0196800d8052c016800d84c240ce9020100008bcee8e0fdffff85ff0fbfc08944241cdb44241cd80d6c976d00d844240cd90570976d00d84c24"
		  "18d8c1d95c240cddd874768b875c03000085c0746c8bcfe80418f6ff83f81d74608b8ec00000008b11ff9224010000d84c240cd95c240ce80454fdff8b108bc8ff520483f8017e39"
		  "e8f353fdff8b108bc8ff525c84c07529e8e353fdff8b108bc8ff5204488944241cdb44241cd80da0196800d8052c016800d84c240cd95c240c8b8ec00000008b01ff900c01000085"
		  "c07e1a8b8ec00000008b11ff920c0100008944241cdb44241cd95c240ce82e52fbff8b108bc8ff926802000083f8017d1e8b86a002000083f81e740583f81f750ed944240cd80d88"
		  "046800d95c240c85ff746d8b44240c81c77004000089442418f6870a0100000174078bcfe81f340800f6870a010000018b4f64894c241c74078bcfe808340800d9476883ec08d944"
		  "2424d8442414d8c9dd1c24ddd8e816981b0083c40885edd95c240c7413d944240cd8642418da4500e82f961b008945008b8ec00000008b11ff523c0fb6c08944241c8d4c241451db"
		  "442420d8442410d95c2410e8e82901008b7c241883c40485ff7445e8b895140081e7ffffff008b54b8048d8402088000005081c65001000056e8bc991b0083c40885c0751be80e2a"
		  "0100d84c240c83ec08dd1c24e887971b00d95c241483c408e87352fdff8b108bc8ff525cd944240c84c05f5e5d7406d80ddce46800d9052c016800d8d9dfe0f6c441750cddd8d905"
		  "2c01680059c20c00d815b8e46800dfe0f6c4057b08ddd8d905b8e4680059c20c00",
		  "max health (0x4b8850, __thiscall, ret 0xc): 25 + 5 level + 3 body at Normal and Easy, esi = the CStats; Hard's enemies 25 + 5 (level - 50) + 3 (level - 45)"}},
		{health, {0x6d9768, "0000c8410000a04000004040", "max health's floats 25, 5, 3 (0x6d9768)"}},
		{health | strength | mind | hit_chance, {0x44a436, "c70694526800", "a herostat character's extension object: the vtable 0x685294 (0x44a436)"}},
		{health | strength | mind | hit_chance, {0x44983a, "c7011c516800", "an npcstat character's extension object: the vtable 0x68511c (0x44983a)"}},
		{strength | mind | might | damage_rolls,
		 {0x453a60,
		  "83ec0853558be98b451032dbf6c40f56c744241000000000c744240c0000803f7402b3018a44242884c08b74241875298a867a050000a801578dbe7004000074178bcfe878840e00"
		  "f6870a0100000174078bcfe868840e005f8a44242484c07410d944241cd8442420d80d88046800eb148b4424208b4c241c6a015051e8cec6100083c40c8a4535d9542420c0e803d9"
		  "5c2424240f3c02755be8e2a101008b108bc8ff926802000083f8017e278bcee8ac94fcff89442424db442424d83df85b6800d84c2420d80de4196800d805a8016800eb59e8a7a101"
		  "008b108bc8ff926802000083f8017d49d9442420d80d88046800eb393c01753984db8b8e5c0300006a00566a007407e8043e0600eb05e8cd3d0600d84c242083ec08d8642428dd1c"
		  "24e8c2e72100d844242883c408d95c24248a44242884c07533d9442424d81d30006800dfe0f6c44175226a00558d442418506a398bcee855c80f006a01558d4c2414516a398bcee8"
		  "44c80f00d944240c83ec08d84c242cd864242cd8442418dd1c24e861e7210083c408e88de5210089442428db442428d8442424d815f45b6800dfe0f6c4417508ddd8d905f45b6800"
		  "5ed955045d5b83c408c21400",
		  "a hit's damage (0x453a60): the stat multiplier for DamageScale normal only, the attacker's damage affecters (kind 0x39) after it"}},
		{strength | mind,
		 {0x4b7940, "8b4424088b5424046a0050526a00e82dfeffff0fbfc089442408db442408d80d982f6800d8052c016800c20c00cccccc8b4424088b5424046a0050526a03e8fdfdffff0fbfc089442408db442408d80d2c2a6800d8052c016800c20c00",
		  "the strength and mind multipliers (0x4b7940, 0x4b7970; __thiscall, ret 0xc): 1 + 0.05 strength, 1 + 0.01 mind, the stat through 0x4b7780"}},
		{strength, {0x682f98, "cdcc4c3d", "the float 0.05 of the strength multiplier (0x682f98)"}},
		{mind, {0x682a2c, "0ad7233c", "the float 0.01 of the mind multiplier (0x682a2c)"}},
		{mind, {0x5e0aec, "8d54241452576a018bcb89442424e8716eedffd8252c01680051d80d14056800d91c24e80c7ee6ff83c404e860160900", "the stats screen's mind line (0x5e0aec): (the mind multiplier - 1) x 100"}},
		{might, {0x550410, "558b6c240c56578b7c241c83ff018bf17509c745000000803feb07c74500000000008b862002000085c074118b961c0200008bc88b0152ff1084c075085f5e32c05dc21000", "an actor's affecter total (0x550410, __thiscall, ret 0x10): *out starts at 1.0 for mode 1"}},
		{might,
		 {0x4b9090, "538b5c2408565733ff8d71208d6424003bbe9c00000075043bf6744233c0668b04fe89442410e8f57400008b108d4c2410518bc8ff521485c074158b10538bc8ff521450e889941b0083c40885c0740347ebbd668b4424105f5e5bc204005f5e660dffff5bc20400",
		  "a character's talent list (0x4b9090): CStats+0x20's 8-byte entries, the id in the first word, the count at +0xbc; the registry's vt+0x14 by id"}},
		{might, {0x4bbda9, "8a44c702243f", "a talent's rank: the low 6 bits of its entry's third byte (0x4bbda9)"}},
		{might, {0x4c05b0, "a19c33770085c0750ae8c2ffffffa19c337700c3", "the talent registry's getter (0x4c05b0)"}},
		{might, {0x68ec38, "e0db4b00", "the talent registry's vt+0x14: 0x4bdbe0 (0x68ec38)"}},
		{might, {0x4bdbe0, "8b44240456578bf18b3e50ff5724508bceff57185f5ec20400", "a talent by a pointer to its id (0x4bdbe0, __thiscall, ret 4)"}},
		{might,
		 {0x4be400, "53568b74240c32db85f6742b8b490885c974246878926800e8a3650a0085c074168b4c2410515056e833200a0083c40c5eb0015bc20800",
		  "a talent's descname (0x4be400): the data node at +8, its attribute through 0x5649c0"}},
		{might,
		 {0x5649c0,
		  "5355568be9578b7d0c8d470183cbff83f8017e318b4c24148d041f992bc28bf0d1fe8b44f5105051e875db100083c40885c0741a7d048bfeeb028bde8bd72bd383fa017fcf5f5e5d33c05b"
		  "c204008b44f5145f5e5d5bc204",
		  "a data node's attribute by name (0x5649c0, __thiscall, ret 4): the value, or null"}},
		{power_attacks, {0x44ec00, "518b4c24088d0424505168ec6d6d00c744240c01000000e8d41611008b44240c83c410c3", "the DamageScale parser (0x44ec00, __cdecl): the table 0x6d6dec, normal (1) for any other name"}},
		{power_attacks, {0x4dc0f8, "5768200a6900538bcee8fafdffff85c075188b4c241451e8ec2af7ff83c4045f8946145eb0015bc20c00", "the style reader's DamageScale attribute (0x4dc0f8): its one call of the parser"}},
		{hit_chance,
		 {0x44f450,
		  "83ec0c5355568b7424248b460c8be98b4e10575189442418e833c9feff8b5424308b5c242483c404c702000000008b46106a0056f6c40f8b835c030000c644243000c644241b018b"
		  "cd6a007417536a0350e87af8ffff8b7c24246a00566a00536a03eb158b7c2430576a0150e85ff8ffff6a00566a00576a018b875c030000d95c24348bcd50e8b5fbffffd95c2424e8"
		  "dc92070084c0740ad9442420dcc0d95c24208a837a050000a8018d8b700400007405e819ca0e00f6837905000004740dc644242801c74424240000803f8b44241483f808771f0fb6"
		  "8058f74400ff24854cf74400c644241301eb05c644241300c644242801f7461400000008752df687640700004075248b87bc00000085c089442414740d8d4c2414e8625f010084c0"
		  "750ef687d8030000047505c644242801f74614000000207405c644241300f646350474208a44241384c0c6442428017409f6876507000002750abf01000000e9090100008bcfe865"
		  "74040084c00f85f5000000568bcfe8559dfdff84c00f84e50000008b8b5c030000e8e29106000fbfc8894c24148b8f5c030000db442414d95c2414e8c89106000fbfd089542418db"
		  "442418d8442414d87c2414d9442420d80d285b6800d9442424d8442420def9dec9dcc0d80d2c2a6800d9542420d81d84566800dfe0f6c4057a0ac7442420cdcc4c3deb40d9442420"
		  "d81d88566800dfe0f6c441752fd944242051d82588566800d80dac016800d91c2468cdcc4c3de8eda7fcff8b442434d91883c408c74424203333733f6a01680000803f6a00e81e0b"
		  "11008a44243483c40c84c07512d85c2420dfe0f6c4057b09bf02000000eb0eddd8bf01000000eb05bf030000008b55008bcdff128b10578bc8ff522483ff03744c83ff027447d905"
		  "30006800d94604dae9dfe0f6c4447b17d94604d81d30006800dfe0f6c4417a07c746040000803f8b45008bcdff108b4e048b10518bc8ff52088bc75f5e5d5b83c40cc210000fbf56"
		  "0889542420c7460400000000db442420d80dfc2f6800e8412a2200668946088bc75f5e5d5b83c40cc21000903bf5440034f5440045f54400000202020001010000",
		  "the melee roll (0x44f450): ebx the attacker, edi the defender, esi the attack; its chance at [esp+0x20], the draw from 0x44f684 on"}},
		{hit_chance,
		 {0x4b7780, "83ec10535556578b7c242485ff7c0b83ff047d068a5c0f08eb0232db8d7104578bcee8692cf8ff0fb6c00fb6cb03c189442424578bcedb442428d95c2428e83d2cf8ff8b6c243085ed0fb6d089542410db442410d8442424d95c24247407c74500000000008b74242c85f60f848c0000008a867a05000081c670040000b30184c374078bcee8164708008a860a010000d944be1084c3d95c241074078bcee8fd4608008a860a010000d944be2084c3d95c241874078bcee8e44608008b463089442414849e0a01000074078bcee8ce46080085edd94634d9442414d8442410d8442424dec9d84c2418740ed9442424d8e9e806a91b00894500d95c2424d9442424e8f6a81b005f5e5d5b83c410c21000",
		  "a stat with its affecters (0x4b7780, __thiscall, ret 0x10): the index, a flag, the actor whose affecters count, an out pointer"}},
		{hit_chance, {0x4b87c0, "5153578bf9660fb65f1ce8d1ff000084c0744f8d4424085650e8822d01008b74241083c40485f67431e85299140081e6ffffff008b4cb0048d8401088000005081c75001000057e8569d1b0083c40885c07507e8782d010003d85e5f668bc35b59c35f668bc35b59c3", "a character's level (0x4b87c0, __thiscall)"}},
		{damage_rolls, {0x5601e0, "8a44240c568b742408578b7c24103bf77e44", "inclusive integer RNG: bounds as integers and the same random stream (__cdecl)"}},
		{melee_weapons, {0x4dc265, "8b470889460c8b4f0c894e10", "attack construction: attack kind at +0xc and damage-type bits at +0x10"}},
		{melee_weapons, {0x4dc37b, "d95e048b855c030000", "attack construction: base draw stored at attack +4, attacker stats at actor +0x35c"}},
		{damage_rolls, {0x453a39, "8b5424088b442404c744240c010000008954240889442404e95ac71000", "construction damage draw: float bounds and a tail jump with stream 1"}},
	}};

	// The fix's addresses the change points the game at.
	struct addresses
	{
		DWORD health_stub = 0;
		DWORD strength_multiplier = 0;
		DWORD mind_multiplier = 0;
		DWORD mind_multiplier_shown = 0;
		DWORD damage_multiplier = 0;
		DWORD damage_scale = 0;
		DWORD hit_stub = 0;
		DWORD damage_roll = 0;
		DWORD melee_stub = 0;
	};

	struct byte_write
	{
		DWORD va;
		std::vector<std::uint8_t> bytes;
	};

	inline DWORD address_for(const target to, const addresses& a)
	{
		switch (to)
		{
		case target::health_stub: return a.health_stub;
		case target::strength_multiplier: return a.strength_multiplier;
		case target::mind_multiplier: return a.mind_multiplier;
		case target::mind_multiplier_shown: return a.mind_multiplier_shown;
		case target::damage_multiplier: return a.damage_multiplier;
		case target::damage_scale: return a.damage_scale;
		case target::hit_stub: return a.hit_stub;
		case target::damage_roll: return a.damage_roll;
		case target::damage_roll_tail: return a.damage_roll;
		case target::melee_stub: return a.melee_stub;
		}
		return 0;
	}

	// The new bytes of one site: a call (or, for the hit stub, a jump) rel32, counted from the next instruction,
	// then nops up to the length of what it replaces.
	inline std::vector<std::uint8_t> bytes_for(const site& s, const addresses& a)
	{
		std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(s.to == target::hit_stub || s.to == target::damage_roll_tail ? 0xe9 : 0xe8)};
		put_dword(bytes, address_for(s.to, a) - (s.va + 5));
		const std::size_t size = limits_rules::hex_size(s.retail);
		if (size == 6)
		{
			bytes.push_back(0x90);
		}
		else if (size == 7)
		{
			bytes.push_back(0x66);
			bytes.push_back(0x90);
		}
		while (bytes.size() < size) bytes.push_back(0x90);
		return bytes;
	}

	// The writes of the features in `wanted`.
	inline std::vector<byte_write> writes_for(const unsigned wanted, const addresses& a)
	{
		std::vector<byte_write> writes;
		for (const auto& s : sites)
		{
			if (s.feature & wanted)
			{
				writes.push_back({s.va, bytes_for(s, a)});
			}
		}
		return writes;
	}

	// The first guard of `f` that `image` (XMen2.exe's image base: 0x400000 in the game) doesn't match, or null.
	inline const guard* first_mismatch(const std::uint8_t* image, const unsigned f)
	{
		for (const auto& g : guards)
		{
			if ((g.features & f) && !limits_rules::matches(image + (g.bytes.va - image_base), g.bytes.hex))
			{
				return &g.bytes;
			}
		}
		return nullptr;
	}

	inline void apply(std::uint8_t* image, const std::vector<byte_write>& writes)
	{
		for (const auto& w : writes)
		{
			std::memcpy(image + (w.va - image_base), w.bytes.data(), w.bytes.size());
		}
	}

	// ---- What the log and the test pipe say -------------------------------------------------------------

	inline std::string number_text(const float value)
	{
		char text[32];
		std::snprintf(text, sizeof text, "%g", value);
		return text;
	}

	// "balance health=10/3 str=0.1/0.025 mind=0.1/0.025 might=0.05 power=xml1 hit=xml1" for the features in
	// `applied` (unscaled / scaled heroes); "-" for one that isn't; "balance off" for none.
	inline std::string status_text(const settings& s, const unsigned applied)
	{
		if (!applied)
		{
			return "balance off";
		}
		const auto pair = [](const float unscaled, const float scaled) { return number_text(unscaled) + "/" + number_text(scaled); };
		std::string text = "balance health=";
		text += applied & health ? pair(s.health_per_body(false), s.health_per_body(true)) : "-";
		text += " str=";
		text += applied & strength ? pair(s.strength_per_point(false), s.strength_per_point(true)) : "-";
		text += " mind=";
		text += applied & mind ? pair(s.mind_per_point(false), s.mind_per_point(true)) : "-";
		text += " might=";
		text += (applied & might) && s.might_bonus ? number_text(*s.might_bonus) : "-";
		text += applied & power_attacks ? " power=xml1" : " power=xml2";
		text += applied & hit_chance ? " hit=xml1(" + number_text(s.hit_chance_base) + "," + number_text(s.hit_chance_per_point) + ")" : " hit=xml2";
		text += s.rounding_xml1 && (applied & (strength | mind | might)) ? " rounding=xml1" : " rounding=xml2";
		text += applied & damage_rolls ? " rolls=xml1" : " rolls=xml2";
		text += " weapons=" + std::to_string(applied & melee_weapons ? s.melee_weapons.size() : 0);
		return text;
	}
}
