#pragma once

// [Game] PackAmount=xml1: a health or energy pack restores a quarter of the maximum, as in X-Men Legends 1, instead of
// XMen2.exe's third. Kept apart from the patch so xml2_test can check every retail byte it relies on.
//
// What XMen2.exe does. A pack's use runs its item's script: XML2's own Data/items gives HEALTH_ITEM
// `restoreHealth('_ACTIVATOR_',-1)` and ENERGY_ITEM `restoreEnergy('_ACTIVATOR_',-1)` (its descriptions say "Recovers
// 25% max health / energy"). The script functions (0x4a2dd0 / 0x4a2c20, script table 0x68aa68 / 0x68aa58) take a
// negative amount as "a fraction of the maximum": they store the fraction 0.33 (0x3ea8f5c3) in a local, at 0x4a2e4e
// (health) / 0x4a2c96 (energy) - 0.66 on a double heal, 0x4a2e96 / 0x4a2cde - and multiply it by the maximum
// (health +0x284, a float; energy +0x314, then truncated, 0x4a2da7). The use functions (0x42ca80 / 0x42cb70) add
// the amount to the pending heal (+0x6a8 / +0x6ac: max(current, pending) + amount, capped at the maximum).
//
// What X-Men Legends 1 does. Its Data/items (generated/items, loaded by default.xbe 0x7f4d0) has the same two
// items with the same scripts, `restoreHealth('_ACTIVATOR_',-1)` / `restoreEnergy('_ACTIVATOR_',-1)`, described as
// "Recovers 25% of maximum health / energy". Its script functions (default.xbe 0x9bd50 / 0x9bcb0) replace a negative
// amount with (int)(max * 0.25) (the float 0.25 at 0x3c83b0; max health +0x246, max energy +0x2bc, both 16-bit) and
// pass it to the use (0x3a7c0 / 0x3a870: max(current, pending) + amount, capped at the maximum, into the 16-bit
// pending +0x48c). No random draw, no multiplier, no double amount.
//
// PackAmount=xml1 writes 0.25 (0x3e800000) into the two single-amount stores. The double-heal stores (0.66) are not
// touched: X-Men Legends 1 has no double heal ([Game] DoubleHeal=0 removes it), and their bytes lie inside the
// DoubleHeal patch's guards. Every peer computes its own heals, so the value is part of the gameplay agreement.

#include "ini_rules.hpp"
#include "limits_rules.hpp" // guard, matches, image_base

#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace pack_amount_rules
{
	using limits_rules::guard;
	using address = std::uint32_t;

	// ---- The ini value ----------------------------------------------------------------------------------

	enum class rule
	{
		xml2,
		xml1,
	};

	// The rule asked for, or why the value isn't one. `set` false and `error` empty: the key isn't set.
	struct rule_choice
	{
		rule value = rule::xml2;
		bool set = false;
		std::string error;
	};

	// "xml1" or "xml2", any case; a ';' starts a comment.
	inline rule_choice parse_rule(const std::string_view raw)
	{
		const auto value = ini_rules::value_text(raw);
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
			return {rule::xml1, true, {}};
		}
		if (lower == "xml2")
		{
			return {rule::xml2, true, {}};
		}
		rule_choice refused;
		refused.error = "isn't xml1 (X-Men Legends 1's quarter) or xml2 (the game's own third)";
		return refused;
	}

	// ---- The numbers ------------------------------------------------------------------------------------------

	constexpr float xml2_fraction = 0.33f;  // 0x3ea8f5c3
	constexpr float xml1_fraction = 0.25f;  // default.xbe 0x3c83b0
	constexpr std::array<std::uint8_t, 4> retail_fraction{0xc3, 0xf5, 0xa8, 0x3e};
	constexpr std::array<std::uint8_t, 4> xml1_fraction_bytes{0x00, 0x00, 0x80, 0x3e};

	// The imm32 of `mov dword [esp+0xc], 0.33` (c7 44 24 0c imm32) in each script function.
	constexpr address health_fraction = 0x4a2e52; // instruction 0x4a2e4e
	constexpr address energy_fraction = 0x4a2c9a; // instruction 0x4a2c96

	// Each guard ends before 0x4a2e64 / 0x4a2cac, where the DoubleHeal patch's guards begin.
	inline constexpr std::array<guard, 4> guards{{
		{0x4a2e4c, "85ffc744240cc3f5a83ec744241001000000",
		 "restoreHealth: the target test and the single amount 0.33, multiplier 1 (0x4a2e4c-0x4a2e5d)"},
		{0x4a2c94, "85ffc744240cc3f5a83ec744241003000000",
		 "restoreEnergy: the target test and the single amount 0.33, multiplier 3 (0x4a2c94-0x4a2ca5)"},
		{0x68aa68, "d02d4a00", "the script table's restoreHealth (0x68aa68)"},
		{0x68aa58, "202c4a00", "the script table's restoreEnergy (0x68aa58)"},
	}};

	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
		{
			if (!limits_rules::matches(image + (g.va - limits_rules::image_base), g.hex))
			{
				return &g;
			}
		}
		return nullptr;
	}

	// What a pack restores on the port's numbers: the script function's own arithmetic (fraction * maximum).
	constexpr float restored(const float fraction, const float maximum)
	{
		return fraction * maximum;
	}
}
