#pragma once
#include "limits_rules.hpp"
#include "placement_crush_rules.hpp"
#include <array>

namespace world_impact_rules
{
	using placement_crush_rules::parse_rule;
	using placement_crush_rules::rule;
	using limits_rules::guard;
	constexpr std::uint32_t site = 0x497977, resume = 0x497983;
	inline constexpr std::array<unsigned char, 12> original{
		0x85, 0xf6, 0x74, 0x22, 0x66, 0x0f, 0xb6, 0x86, 0x1d, 0x03, 0x00, 0x00};
	// The native resolver/type test leaves ESI null for a non-physical target.
	// XML1's corresponding impact calculation uses multiplier zero there.
	// Check the classification, FPU input, displaced instructions and epilogue.
	inline constexpr std::array<guard, 4> guards{{
		{0x497900, "518b44240c5657660fb6b91d0300008d4c2414", "impact formula frame and source heaviness"},
		{0x497924, "8bf085f674268b16538bceff128b15683f750083c2248bca83e11fbb01000000d3e3c1fa05855c90145b7502", "physical-target classification"},
		{0x497950, "33f66685ff7c566683ff037d548a44241833c984c0740cd9442410d80dac9c6800eb04d944240885f67422660fb6861d030000", "null target and native FPU input"},
		{resume, "6685c07d0433c0eb06663d03007d0b2bf88bcf6685c97d0233c90fbfc15f5ed80c859c9c680059c20c00", "physical-target continuation and native return"},
	}};
	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
			if (!limits_rules::matches(image + g.va - limits_rules::image_base, g.hex)) return &g;
		return nullptr;
	}
}
