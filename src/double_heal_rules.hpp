#pragma once

// [Game] DoubleHeal=0: X-Men Legends 1's packs never heal double (issue #82 of the X-Men Legends 1 port), kept apart
// from the patch so xml2_test can check every retail byte it relies on.
//
// What XMen2.exe does. A health or energy pack's use runs its item's script, restoreHealth / restoreEnergy
// (0x4a2dd0 / 0x4a2c20, script table 0x68aa68 / 0x68aa58). Each heals a third (0.33) and passes a multiplier of 1,
// but first draws a random number (0x5601b0) and compares it with the hero's double-heal chance (0x4b78c0 /
// 0x4b7900, the stats screen's "Double Heal Chance"): below it, two thirds and multiplier 2 (energy: 4). The use
// (0x42ca80 / 0x42cb70) then plays common/game/pickup_power_double instead of common/game/pickup_power: XML2's own
// recording, darker (spectral centroid about 1680 Hz against 2260 Hz). Port harness, 40 staged uses of each: 2/40
// health and 3/40 energy uses doubled and played it - the "sometimes slower / lower pitched" pack sound.
//
// What X-Men Legends 1 does. Its pack use (default.xbe 0x3a7c0) takes no multiplier and always plays
// common/game/pickup_power; its sound banks have no double event.
//
// DoubleHeal=0 makes both comparisons take the single branch: `jne` (75 10) at 0x4a2e94 / 0x4a2cdc becomes `jmp`
// (eb 10). The random number is still drawn, so the random stream every peer shares advances as before.

#include "limits_rules.hpp" // guard, matches, image_base

#include <array>
#include <cstdint>

namespace double_heal_rules
{
	using limits_rules::guard;
	using address = std::uint32_t;

	constexpr address health_branch = 0x4a2e94;
	constexpr address energy_branch = 0x4a2cdc;
	constexpr std::uint8_t retail_jne = 0x75;
	constexpr std::uint8_t patched_jmp = 0xeb;

	inline constexpr std::array<guard, 4> guards{{
		{0x4a2e64, "8bb75c0300006a01680000803f6a00e838d30b00d95c242083c40c6a00576a008bcee8354a0100d85c2414dfe0f6c4417510c744240cc3f5283fc744241002000000",
		 "restoreHealth: the random draw, the double-heal chance, the double branch (0x4a2e64-0x4a2ea5)"},
		{0x4a2cac, "8bb75c0300006a01680000803f6a00e8f0d40b00d95c242083c40c6a00576a008bcee82d4c0100d85c2414dfe0f6c4417510c744240cc3f5283f",
		 "restoreEnergy: the random draw, the double-heal chance, the double branch (0x4a2cac-0x4a2ce5)"},
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
}
