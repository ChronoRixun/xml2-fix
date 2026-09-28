#pragma once

// [Game] XPCurve: X-Men Legends 1's hero levels and kill XP in XMen2.exe, for a mod that carries XML1's XP amounts
// (the X-Men Legends 1 port: its objectives, scripts and npcstat are XML1's), kept apart from the patching so
// xml2_test can check it without the game: the ini value's rules, XML1's tables and formulas, every retail byte the
// change relies on and every byte it writes.
//
// The research is in the xml1-port repository, research/heroes/levels.md (sections 2, 4 and 6 A); every XMen2.exe
// address below was read again from the retail build for this code (image base 0x400000, no relocations) and is in
// `guards`, every XML1 one from XML1's default.xbe (Xbox, 2004).
//
// What XMen2.exe does. The hero registry (vtable 0x68544c; its getter 0x44b8f0) owns the XP curve: vt+0xd0
// (0x448a90) is the XP a hero needs for level n - a table at 0x717720 it builds on its first call, T(1) = 0,
// T(n) = T(n-1) + (730 + 65 (n - 2)) n + 1500 for n = 2..99, T(100) = 0x7fffffff, read for 0 < n < 101; vt+0xc8
// (0x44b690) is the level cap, 99; vt+0xc4 (0x44b6d0) the most XP a hero can have, T(99) + 1, where adding XP
// stops (0x4b79b0). A hero's level byte (CStats+0x1c) is raised while its XP reaches T(level + 1), at most to 99
// (0x4b7d10); level-up-by-N (vt+0xdc, 0x44be40: the level-up pickup, the reward menu, Hard's start, the cheat)
// clamps to 99 by hand, the shop's level-up item is refused at 99 (0x5a9db1), and the "every hero to 99" cheat
// (0x5db420) asks for 99 - level. Kills (0x436c50): the victim's level L gives vt+0xcc (0x44a9f0) = (20 L + 80)
// times max(30, 100 - 10 |party's average level - L|) %; x2 / x3 by the victim's flags (+0x3db 0x10 / 0x08: XML2's
// spawn variants, its mutated enemies among them), an npcstat xpaward replaces it; the bench gets min(trunc(0.4 xp), 1) through the roster award vt+0xd4 (0x449fe0)
// with flag 0 = the heroes not in the zone; each party hero xp times 1.0 (the killer, a human player) or, for an
// AI teammate, 0.4 + 0.6 (1 - d^2 / 90000) (d = its distance to the killer or the victim, whichever is nearer;
// 0.4 from 300 units on), then its XP boosts and a victim bonus.
//
// What XML1 does. default.xbe's registry has the same slots: 0x541e0 builds the table (T1(1) = 0,
// T1(n) = T1(n-1) + 5 (n + 8) f(n-1) for n = 2..45, T1(46) = 0x7fffffff, read for 0 < n < 47), 0x56c80 is the cap,
// 45, and 0x56cc0 the most XP, T1(45) + 1. f is its kill XP (0x54800): f(L) = trunc(2.5 x (4/3)^(L - 1)) (the double
// 4/3 at 0x3cb710, the float 2.5 at 0x3c8c98, the CRT pow 0x341990, ftol 0x3439c4) - no party multiplier. A kill
// (0x45f55-0x46253): an npcstat xpaward replaces f(L) (+0x380); half = xp / 2 goes to every hero through the
// roster award, the party included (0x55180 adds it whatever its flag says); then each party hero gets
// 3 (half + 1) + a victim bonus - an AI teammate that isn't the killer (1 - d^2 / 90000) (bonus + 2 (half + 1)) +
// (half + 1), nothing from 300 units on.
//
// The tables below are default.xbe's formulas worked out exactly, with the x87 at its default 53-bit precision
// (XML1's code never changes the precision; pow sets 0x27f itself, the multiply by 2.5 and ftol keep the caller's).
// At 24-bit precision f(40) would be 186445 instead of 186444 (2.5 x (4/3)^39 = 186444.998), and T1(41..45) 245 more;
// no other value depends on it (xml2_test checks the margins).
//
// XPCurve=xml1 changes, in memory, before any of the game's code runs:
//   - the table: 0x448a90 reads the DLL's copy of XML1's (47 entries, 0..46) instead of 0x717720, for 0 < n < 47
//     (0x2f, was 0x65 = 101); the builder still fills 0x717720 with XML2's on its first call, which nothing reads;
//   - the cap 45 (was 99) at every place that carries it: the cap (vt+0xc8), the most XP (vt+0xc4: T1(45) + 1), the
//     level recompute's clamp, level-up-by-N's four clamps, the shop's level-up item, the cheat (every hero to 45);
//   - kills: vt+0xcc is XML1's f(L) (a jump to the DLL; the enemy level is capped at 45 there, as no XML1 enemy is
//     above 40 and XML1's own int overflows from level 73 on); every hero gets half (the roster award's flag 1 = the
//     party too, as XML1's); each party hero 3 (half + 1) (the loop's float of the kill XP becomes that int); an
//     AI teammate 1/3 + 2/3 (1 - d^2 / 90000) of it (was 0.4 + 0.6 x), nothing from 300 units on (was 0.4).
// What stays XML2's (decided, and why):
//   - the Danger Room's fixed level 30 (1 in one versus mode; 0x483ec0, 0x4d0a9b, 0x4d0b0c) - within XML1's cap, and
//     like XML2's 30 of 99 a late-campaign level on XML1's curve (XML1's objectives alone end the campaign at 34);
//   - Hard's start at 45 (0x46a15f): XML1's cap, so Hard starts heroes at the top. Hard is unlocked only by XML2's
//     apoc_death.py; its NPC +50 levels (0x44bd4e, clamp 99) stay, as the NPC stat formulas at Hard use level - 50
//     (0x4b88e6), and the kill XP caps the enemy level at 45;
//   - the victim bonus (+0x6b0), the XP boosts, the victim flags' x2 / x3 (XML1's npcstat has no leaderskin or
//     mutantskin) and the Danger Room's halving; a downed party hero still gets nothing (XML1 gave it the full share);
//   - the loot level range max level + 20 (0x5d3800: 65) and the talent data's 0..99 range (0x4c05d9, 0x5efdb2).

#include "limits_rules.hpp" // guard, matches, hex_byte, hex_size, image_base

#include <Windows.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace xp_curve_rules
{
	using limits_rules::guard;
	using limits_rules::image_base;

	// ---- The ini value --------------------------------------------------------------------------------

	enum class curve
	{
		xml2,
		xml1,
	};

	// The curve asked for, or why the value isn't one. `set` false and `error` empty: the key isn't set.
	struct curve_choice
	{
		curve value = curve::xml2;
		bool set = false;
		std::string error;
	};

	// "xml1" or "xml2", any case; a ';' starts a comment.
	inline curve_choice parse_curve(std::string_view raw)
	{
		if (const auto comment = raw.find(';'); comment != std::string_view::npos)
		{
			raw = raw.substr(0, comment);
		}
		const auto value = limits_rules::trimmed(raw);
		if (value.empty())
		{
			return {};
		}
		std::string lower(value);
		for (auto& c : lower)
		{
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		if (lower == "xml1")
		{
			return {curve::xml1, true, {}};
		}
		if (lower == "xml2")
		{
			return {curve::xml2, true, {}};
		}
		curve_choice refused;
		refused.error = "isn't xml1 (X-Men Legends 1's levels and kill XP) or xml2 (the game's own)";
		return refused;
	}

	// ---- XML1's numbers ---------------------------------------------------------------------------------

	constexpr int xml1_max_level = 45; // default.xbe 0x56c80
	constexpr int xml2_max_level = 99; // XMen2.exe 0x44b690
	constexpr std::uint32_t out_of_table = 0x7fffffff;

	// T1(n), the XP a hero needs for level n: default.xbe 0x541e0 (T1(1) = 0, T1(n) = T1(n-1) + 5 (n + 8) f(n-1)),
	// index 0 = -1 and 46 = 0x7fffffff as there. The game reads 1..46 (0 < n < 47).
	inline constexpr std::array<std::uint32_t, 47> xml1_level_xp{{
		0xffffffff, 0, 100, 265, 505, 830, 1320, 2070,
		3190, 4720, 6880, 10015, 14415, 20610, 29190, 41265,
		58065, 81440, 113810, 158630, 220650, 306345, 424545, 587450,
		811610, 1119830, 1543300, 2124650, 2921870, 4014295, 5510355, 7557660,
		10357460, 14183785, 19410055, 26544400, 36278080, 49551280, 67642160, 92287785,
		125847705, 171526485, 233674735, 318196270, 433101450, 589254820, out_of_table,
	}};

	// f(L), XML1's kill XP for an enemy of level L = 0..45: default.xbe 0x54800, trunc(2.5 x (4/3)^(L - 1)).
	inline constexpr std::array<std::int32_t, 46> xml1_kill_xp_table{{
		1, 2, 3, 4, 5, 7, 10, 14, 18, 24,
		33, 44, 59, 78, 105, 140, 187, 249, 332, 443,
		591, 788, 1051, 1401, 1868, 2491, 3322, 4429, 5905, 7874,
		10499, 13999, 18665, 24887, 33183, 44244, 58992, 78656, 104875, 139833,
		186444, 248593, 331457, 441943, 589258, 785677,
	}};

	// What the DLL's vt+0xcc answers for an enemy of `level`: f(level), the level capped at 45 (XML1's enemies are
	// 0..40; f overflows XML1's own int from level 73 on). Below 0, as XML1's formula: 1 for -2 and -1, then 0.
	constexpr std::int32_t kill_xp(const int level)
	{
		if (level < -2)
		{
			return 0;
		}
		if (level < 0)
		{
			return 1;
		}
		return xml1_kill_xp_table[static_cast<std::size_t>(level > xml1_max_level ? xml1_max_level : level)];
	}

	// The patched 0x448a90 (vt+0xd0): T1(n) for 0 < n < 47, else 0x7fffffff.
	constexpr std::uint32_t xp_for_level(const int level)
	{
		return level > 0 && level < static_cast<int>(xml1_level_xp.size()) ? xml1_level_xp[static_cast<std::size_t>(level)] : out_of_table;
	}

	// The patched 0x44b6d0 (vt+0xc4): the most XP a hero can have, T1(45) + 1.
	constexpr std::uint32_t max_xp()
	{
		return xp_for_level(xml1_max_level) + 1;
	}

	// The level a hero with `xp` ends up at: 0x4b7d10's climb from level 1 (while xp >= T(level + 1), at most 45),
	// with the XP capped at max_xp() the way adding XP caps it (0x4b79b0).
	constexpr int level_for_xp(std::uint32_t xp)
	{
		if (xp > max_xp())
		{
			xp = max_xp();
		}
		int level = 1;
		while (xp >= xp_for_level(level + 1) && level < xml1_max_level)
		{
			++level;
		}
		return level;
	}

	// XMen2.exe's own table (0x448a90 before the change), for comparison.
	constexpr std::uint32_t xml2_xp_for_level(const int level)
	{
		if (level <= 0 || level >= 100)
		{
			return out_of_table; // T(100) = 0x7fffffff, and above the table
		}
		std::uint32_t total = 0;
		for (int n = 2; n <= level; ++n)
		{
			total += static_cast<std::uint32_t>((730 + 65 * (n - 2)) * n + 1500);
		}
		return total;
	}

	constexpr int xml2_level_for_xp(const std::uint32_t xp)
	{
		int level = 1;
		while (xp >= xml2_xp_for_level(level + 1) && level < xml2_max_level)
		{
			++level;
		}
		return level;
	}

	// What one kill worth `xp` gives (after the npcstat xpaward, XML2's leader / mutant multipliers and the Danger
	// Room's halving, which the change leaves as they are): every hero `every_hero` (XML1: half, the bench and the
	// party alike), each party hero `party` more before its XP boosts and the victim bonus (XML1: 3 (half + 1); an AI
	// teammate a share of it by distance, teammate_share).
	struct kill_shares
	{
		std::uint32_t every_hero = 0;
		std::uint32_t party = 0;
	};

	constexpr kill_shares xml1_shares(const std::uint32_t xp)
	{
		const std::uint32_t half = xp / 2;
		return {half, 3 * (half + 1)};
	}

	// XML2's (0x437199-0x4371ab): the bench min(trunc(0.4 xp), 1), the party xp (and not the bench's share).
	constexpr std::uint32_t xml2_bench_share(const std::uint32_t xp)
	{
		const auto share = static_cast<std::uint32_t>(static_cast<float>(xp) * 0.4f);
		return share > 1 ? 1 : share;
	}

	// The patched teammate factor (0x43739c-0x4373d0): d2 = the squared distance, the game's floats.
	inline constexpr float far_teammate = 0.0f;            // from 300 units on (was 0.4)
	inline constexpr float teammate_floor = 1.0f / 3.0f;   // near: 1/3 + 2/3 (1 - d2 / 90000) (was 0.4 + 0.6 x)
	inline constexpr float teammate_falloff = 2.0f / 3.0f;

	constexpr float teammate_factor(const float d2)
	{
		return d2 < 90000.0f ? (1.0f - d2 * (1.0f / 90000.0f)) * teammate_falloff + teammate_floor : far_teammate;
	}

	// ---- The patch ------------------------------------------------------------------------------------

	// What a site's new bytes are: fixed, or an address of the DLL's (the table, the kill XP function - a jmp rel32
	// from the site - or one of the three floats).
	enum class value_kind
	{
		fixed,
		level_table,
		kill_xp_jump,
		far_teammate,
		teammate_floor,
		teammate_falloff,
	};

	// One run of bytes written: at `va`, `retail` there before, the new bytes `patched` (fixed) or from `kind`.
	struct site
	{
		DWORD va;
		std::string_view retail;
		std::string_view patched; // hex, the same length as retail; empty for an address
		value_kind kind;
		const char* what;
	};

	inline constexpr std::array<site, 19> sites{{
		// The table (0x448a90, vt+0xd0).
		{0x448afa, "65", "2f", value_kind::fixed, "XP for level: read for 0 < n < 47 (was 101)"},
		{0x448b00, "20777100", {}, value_kind::level_table, "XP for level: the DLL's table of XML1's (was 0x717720)"},
		// The cap.
		{0x44b691, "63000000", "2d000000", value_kind::fixed, "the level cap (vt+0xc8): 45"},
		{0x44b6d1, "63", "2d", value_kind::fixed, "the most XP (vt+0xc4): T(45) + 1"},
		{0x4b7d6b, "63", "2d", value_kind::fixed, "the level recompute: at most 45 (the compare)"},
		{0x4b7d6f, "63000000", "2d000000", value_kind::fixed, "the level recompute: at most 45 (the clamp)"},
		{0x44bec4, "63000000", "2d000000", value_kind::fixed, "level-up-by-N: n <= 45 - level"},
		{0x44bed9, "6300", "2d00", value_kind::fixed, "level-up-by-N: below 45"},
		{0x44bee8, "63000000", "2d000000", value_kind::fixed, "level-up-by-N: n <= 45 - level (again)"},
		{0x44befd, "63000000", "2d000000", value_kind::fixed, "level-up-by-N: n = 45 - level"},
		{0x5a9db3, "6300", "2d00", value_kind::fixed, "the shop's level-up item: refused at 45"},
		{0x5db460, "63000000", "2d000000", value_kind::fixed, "the cheat: every hero to 45"},
		// Kills.
		{0x44a9f0, "83ec248b44", {}, value_kind::kill_xp_jump, "kill XP (vt+0xcc): XML1's f(L), in the DLL"},
		{0x437199, "d80d00416800e8d8af23008bf883ff017605bf01000000",
		 "ddd88bfbd1ef8d447f038944241c0f1f80000000006690", value_kind::fixed,
		 "the shares: half to every hero (edi), 3 (half + 1) as the party's base ([esp+0x1c], an int)"},
		{0x4371b8, "00", "01", value_kind::fixed, "the roster award's flag 1: the half to the party too"},
		{0x4373d1, "d9", "db", value_kind::fixed, "the party loop reads its base as an int (fild)"},
		{0x43739e, "00416800", {}, value_kind::far_teammate, "an AI teammate from 300 units on: 0 (was 0.4)"},
		{0x4373c7, "b82f6800", {}, value_kind::teammate_falloff, "an AI teammate nearer: x 2/3 (was 0.6)"},
		{0x4373cd, "00416800", {}, value_kind::teammate_floor, "an AI teammate nearer: + 1/3 (was 0.4)"},
	}};

	// Every byte of XMen2.exe the change relies on, read from the retail build; every site lies inside one. All must
	// match before anything is written; xml2_test compares them with a copy of the exe.
	inline constexpr std::array<guard, 14> guards{{
		{0x448a90,
		 "a0b478710084c07557c70520777100ffffffffc7052477710000000000c705b0787100ffffff7fb802000000b9da020000568b34851c7771008bd10fafd08d9416dc050000891485"
		 "2077710083c1414081f9bc1b00007cdac605b4787100015e8b44240485c07e0f83f8657d0a8b048520777100c20400b8ffffff7fc20400",
		 "XP for level (0x448a90, vt+0xd0): builds XML2's table at 0x717720 once, then reads it for 0 < n < 101"},
		{0x44b690, "b863000000c3", "the level cap (0x44b690, vt+0xc8): 99"},
		{0x44b6d0, "6a63e8b9d3ffff40c3", "the most XP (0x44b6d0, vt+0xc4): T(99) + 1"},
		{0x685510, "d0b6440090b64400f0a94400908a4400", "the registry's vtable: vt+0xc4, +0xc8, +0xcc, +0xd0 (0x685510)"},
		{0x4b7d10,
		 "51568bf1e8d73bf9ff8b1033c9668b8e8e020000518bc8ff527c84c00f84140100000fb6561c55578954240ce8af3bf9ff8b8ec00000008bf88b01ff500c8b178be80fb6461c4050"
		 "8bcfff92d00000003be8723a0fb6461c4083f8637e05b86300000088461ce8753bf9ff8b8ec00000008b118bf8ff520c0fb64e1c418be88b07518bcfff90d00000003be873c6e84d"
		 "3bf9ff8b8ec00000008b118bf8ff520c0fb64e1c8be88b07518bcfff90d00000003be8733e0fb64e1c49b8000000000f98c0c6461e004823c188461ce80f3bf9ff8b8ec00000008b"
		 "118bf8ff520c0fb64e1c8be88b07518bcfff90d00000003be872c283bea00200001d5f5d752df686ad020000207524e8c45efbff8b108bc8ff927c02000084c07411e8a180fdff0f"
		 "b64e1c8b10518bc8ff522c0fb6461c2b4424045e59c333c05e59c3",
		 "the level recompute (0x4b7d10): up while XP >= T(level + 1), at most 99; down while XP < T(level)"},
		{0x44be40,
		 "568b74240885f657746bd9442418d81d30006800dfe0f6c4057a5ae8801e02008b108d8e50010000518bc8ff921401000089442418db442418d80dac016800d82d88046800d81530"
		 "006800dfe0f6c4057a0addd8d90530006800eb15d815602c6800dfe0f6c4417508ddd8d905602c6800d95c24188bcee804c906008b7c24100fbfd0b8630000002bc23bf87c028bf8"
		 "8bcee8e9c80600663d63007d268bcee8dcc806000fbfc0b9630000002bc83bf97e118bcee8c7c806000fbfd0bf630000002bfa8bcee8b6c806000fbfc003c783f8010f8c13010000"
		 "5355e8d1f9ffff8bd88b2b8bcee896c806000fbfc803cf518bcbff95d00000008bd8e8b1f9ffff8b288bce89442414e874c806008b4c24140fbfd08d443a0150ff95d00000008b8e"
		 "c00000002bc385c089442414db4424147d06d805a81a680085dbd84c2420895c2420db4424207d06d805a81a68008b11dec1d95c2420ff520c85c089442414db4424147d06d805a8"
		 "1a6800d86c2420e8c86122006a01508bcee8f2b906008bcee84bbd06008bcee814fb06008a44241c84c05d5b7432e8d514ffff8b4c241c8b10516a016a01568bc8ff52088bcee8fd"
		 "d201008bc8e876f3feffe8e11c02008b106a008bc8ff526ce8d31c02008b108d8e50010000518bc8ff921401000085c07c096a008bcee8c5ff06005f5ec21400",
		 "level-up-by-N (0x44be40, vt+0xdc): n clamped to 99 - level, the XP for the new level added"},
		{0x5a9d96, "e8551beaff660fb64e108b10518bc8ff52748bf08bcee80feaf0ff663d63007d4d3beb", "the shop's level-up item: refused at 99 (0x5a9d96)"},
		{0x5db446, "e8a504e7ff8b10538bc8ff52748bf88bcfe864d3edff0fbfc0be630000002bf085f67e1ae88104e7ff8b106a0068000080bf6a0056578bc8ff92dc000000",
		 "the cheat (0x5db420): each hero up by 99 - level through level-up-by-N"},
		{0x44a9f0,
		 "83ec248b4424285355568d74801457c1e6028d4c241c8974241433db33ffe8dd6efdffe8c83202008b10576aff8d4c2424518bc8ff92200100008b4c243033c085c9894424107e6d"
		 "eb068d9b000000008b54841c8d4c241889542418e85faa01008bf085f674358b068bceff108b1540b8700083c2248bca83e11fbd01000000d3e5c1fa05856c901474118b8e5c0300"
		 "00e83add06000fbfc803d9478b4424108b4c2430403bc1894424107ca385ff75108b7424145f8bc65e5d5b83c424c204008bc399f7ff85d27407394424387e01402b442438998bc8"
		 "33ca2bcab80a0000002bc18d0c80d1e183f91e7d05b91e0000000faf4c2414b81f85eb51f7e15f5e8bc25dc1e8055b83c424c20400",
		 "kill XP (0x44a9f0, vt+0xcc, __thiscall, ret 4): (20 L + 80) x the party-average multiplier"},
		{0x4370e2,
		 "e8094801008b0e8b895c0300008bf88b1fe8c81608000fbfd0528bcfff93cc0000008bd88b068a88db030000f6c110740403dbeb08f6c10874038d1c5b8b805c0300008b88c00000"
		 "008b01ff90f400000085c074188b0e8b815c0300008b88c00000008b11ff92f40000008bd8e84c16090084c07421d1ebe8711609003cfd7416e8681609003cfe740de8ef2a0900f6"
		 "404f047402d1eb83fb017305bb0100000085db895c2424db4424247d06d805a81a6800d954241cd80d00416800e8d8af23008bf883ff017605bf01000000e83b4701008b106a0057"
		 "8bc8ff92d4000000",
		 "a kill (0x4370e2): the kill XP in ebx, its float at [esp+0x1c], the bench's share through the roster award"},
		{0x437344,
		 "8a87d8030000d9052c016800a801747d8b46083bf8747685c074728b0eddd88b1783c1208d6820518d44242c508bcfff52248bc8ff15ccf9670051d91c24558d4f20ff15ccf96700"
		 "51d91c24e8cb2afeffd95c241c83c408d90500416800d9442414d81d68416800dfe0f6c4057a1eddd8d9442414d80d64416800d82d2c016800d80db82f6800d80500416800d94424"
		 "1cd8c9e8a0ad2300ddd88daf700400008bd8f6850a0100000174078bcde82a4b100085db895c2414db4424147d06d805a81a6800d885c4000000e869ad23008bd8f6850a01000001"
		 "74078bcde8fb4a100085db895c2414db4424147d06d805a81a6800d88dc8000000e83aad2300394424187704894424188b4c241003c1508bcfe8f6aefeff8b5c2420",
		 "a kill, each party hero (0x437344): [esp+0x1c] x its factor, its XP boosts, + the victim bonus, added (0x422350)"},
		{0x449fe0,
		 "51558be98b85a8bb000085c0c7442404000000000f8e960000005356578d9d389b0000f6430801746a8b0b85c974648bb5209b00008d450423f169f6f804000003f0744f83bea002"
		 "00001d7546e8ae3c02008b108d8e50010000518bc8ff92080100008bf88a44241c84c0750485ff75228b5424186a00528bcee851d9060085ff74098bcfe8a675fdffeb078bcee89d"
		 "dc06008b4424108b8da8bb00004083c31c3bc1894424100f8c76ffffff5f5e5b5d59c20800",
		 "the roster award (0x449fe0, vt+0xd4): every hero, flag 0 skips the ones in the zone"},
		{0x68012c, "0000803f", "the float 1.0 the teammate factor starts from (0x68012c)"},
		{0x684164, "dc693a3700c8af47", "the floats 1/90000 and 90000 of the teammate factor (0x684164)"},
	}};

	// The DLL's addresses the change points the game at.
	struct addresses
	{
		DWORD level_table = 0;
		DWORD kill_xp_function = 0;
		DWORD far_teammate = 0;
		DWORD teammate_floor = 0;
		DWORD teammate_falloff = 0;
	};

	struct byte_write
	{
		DWORD va;
		std::vector<std::uint8_t> bytes;
	};

	inline std::vector<std::uint8_t> from_hex(const std::string_view hex)
	{
		std::vector<std::uint8_t> bytes(limits_rules::hex_size(hex));
		for (std::size_t i = 0; i < bytes.size(); ++i)
		{
			bytes[i] = limits_rules::hex_byte(hex, i);
		}
		return bytes;
	}

	inline std::vector<std::uint8_t> dword_bytes(const std::uint32_t value)
	{
		std::vector<std::uint8_t> bytes(4);
		std::memcpy(bytes.data(), &value, 4);
		return bytes;
	}

	// The new bytes of one site.
	inline std::vector<std::uint8_t> bytes_for(const site& s, const addresses& a)
	{
		switch (s.kind)
		{
		case value_kind::fixed:
			return from_hex(s.patched);
		case value_kind::level_table:
			return dword_bytes(a.level_table);
		case value_kind::kill_xp_jump:
		{
			auto bytes = dword_bytes(a.kill_xp_function - (s.va + 5)); // jmp rel32 counts from the next instruction
			bytes.insert(bytes.begin(), 0xe9);
			return bytes;
		}
		case value_kind::far_teammate:
			return dword_bytes(a.far_teammate);
		case value_kind::teammate_floor:
			return dword_bytes(a.teammate_floor);
		case value_kind::teammate_falloff:
			return dword_bytes(a.teammate_falloff);
		}
		return {};
	}

	inline std::vector<byte_write> writes_for(const addresses& a)
	{
		std::vector<byte_write> writes;
		for (const auto& s : sites)
		{
			writes.push_back({s.va, bytes_for(s, a)});
		}
		return writes;
	}

	// The first guard `image` (XMen2.exe's image base: 0x400000 in the game) doesn't match, or null.
	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
		{
			if (!limits_rules::matches(image + (g.va - image_base), g.hex))
			{
				return &g;
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
}
