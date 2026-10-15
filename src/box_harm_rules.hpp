#pragma once
#include "limits_rules.hpp"
#include "placement_crush_rules.hpp"
#include <array>

namespace box_harm_rules
{
	using placement_crush_rules::parse_rule;
	using placement_crush_rules::rule;
	using limits_rules::guard;
	constexpr std::uint32_t site = 0x451c6e, resume = 0x451c74;
	inline constexpr std::array<unsigned char, 6> original{0x8d,0x54,0x24,0x50,0x8b,0xcc};
	// XML1's box service passes a zero origin to the target dispatcher (0x5a9b9).
	// The PC service passes the box center, generating radial directions and delays.
	// Preserve the spatial query; change only that dispatcher's origin argument.
	inline constexpr std::array<guard, 4> guards{{
		{0x439b72,"f686600300000274288b16568d442420508d442418508bceff52288b165083ec0c548bceff5224e80280010083c4188bd8","harm box service caller"},
		{0x451c6a,"5083ec0c8d5424508bcc8964242452ff15e0f96700","origin copy stack frame"},
		{0x451c7f,"8b4424648b4c246050518d54242452e86df0ffff83c41c","dispatcher call and cleanup"},
		{0x450ef1,"ff1558fa6700d81d2c016800dfe0f6c4410f858b000000","dispatcher origin-length gate"},
	}};
	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
			if (!limits_rules::matches(image + g.va - limits_rules::image_base,g.hex)) return &g;
		return nullptr;
	}
}
