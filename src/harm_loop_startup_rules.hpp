#pragma once
#include "limits_rules.hpp"
#include "placement_crush_rules.hpp"
#include <array>

namespace harm_loop_startup_rules
{
	using placement_crush_rules::parse_rule;
	using placement_crush_rules::rule;
	using limits_rules::guard;
	constexpr std::uint32_t site = 0x4399c5, resume = 0x4399cf, skip = 0x439a2f;
	inline constexpr std::array<unsigned char, 10> original{0x8b,0x06,0x8b,0xce,0xff,0x90,0xe8,0,0,0};
	// Entered only for positive firstact. The base action parser already owns
	// that timer. Ordinary start-on loops do not need PC's on/off timer path.
	inline constexpr std::array<guard, 6> guards{{
		{0x41bf24,"686f12833ad91dd4526d0068d0526d008bcde8f5e7ffff","base parser schedules authored first activation"},
		{0x4398c5,"8b178dae640300005568784468008bcfff521c8b178d86680300005068704468008bcfff521c","on and off timer field layout"},
		{0x439999,"8b176a0068b82068008bcfff521085c0743a50e85f882300d954242cd81d3000680083c404dfe0f6c4417520","positive-firstact branch"},
		{0x4399c5,"8b068bceff90e80000008a9ec50000008b4c242880e3fb889ec500000051eb3e","positive-firstact disable and loop clear"},
		{0x439a23,"8bce6820606d00e8010dfeff","PC on/off event scheduling"},
		{0x439a2f,"8dae6c03000055c74500000000008b17","resume at smart pulse parsing"},
	}};
	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
			if (!limits_rules::matches(image + g.va - limits_rules::image_base,g.hex)) return &g;
		return nullptr;
	}
}
