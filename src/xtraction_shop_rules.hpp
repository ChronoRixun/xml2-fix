#pragma once

#include "limits_rules.hpp"
#include <array>

// Issue #95: append the source equipment-shop label/action before the native dialog's finalize call.
// The builder passes permission per point; this never changes Save, Change Team, or the world map.
namespace xtraction_shop_rules
{
	constexpr DWORD finalize_call = 0x4a6d33;
	constexpr DWORD dialog_getter = 0x5eb300;
	constexpr DWORD text_getter = 0x4bdb30;
	constexpr DWORD text_vtable = 0x68e990;
	constexpr int label_id = 2005;
	inline constexpr std::array<limits_rules::guard, 3> guards{{
		{0x4a6d04, "e8276e01008bf0e8f04514006a006a006a006a008d5424288bf88b1f8b065268d20700008bceff5008508bcfff531ce8c84514008b108bc8ff5268",
		 "native Save addOption(text, script, script2, closes, resumes, disabled), then dialog finalize"},
		{text_getter, "8a0d98337700b80100000084c80f858f", "text singleton getter"},
		{dialog_getter, "a1ec138b0085c0750ae882ffffffa1ec", "dialog singleton getter"},
	}};
	constexpr bool should_add(bool enabled, bool permission) { return enabled && permission; }
}
