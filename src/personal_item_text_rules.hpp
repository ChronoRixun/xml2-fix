#pragma once
#include "limits_rules.hpp"
#include <array>
#include <cstdint>
#include <cstring>

// Only the PERSONAL_MENU factory call is replaced. See docs/personal-item-text.md.
namespace personal_item_text_rules
{
	using limits_rules::guard;
	constexpr DWORD image_base = 0x400000;
	constexpr DWORD constructor_call = 0x5b84e9;
	constexpr DWORD constructor = 0x5b8180;
	constexpr DWORD personal_vtable = 0x69f97c;
	constexpr std::size_t depth_offset = 0x1928;
	constexpr std::int16_t retail_depth = -10;
	constexpr std::int16_t text_background_depth = 10;
	inline constexpr std::array<guard, 7> guards{{
		{0x5b84b6, "68f4fb690056e8a1a00b0083c40885c0752b8bcfe8d1ecffff894424288944241085c0c744241c040000000f849e0400008bc8e892fcffff", "personal factory branch and constructor call"},
		{0x69fbf4, "504552534f4e414c5f4d454e5500", "personal menu type"},
		{0x5b8180, "568bf1e848faffffc7067cf969008bc65ec3", "personal constructor and vtable"},
		{0x5b7c30, "66c78628190000f6ff", "inherited signed image depth default"},
		{0x69fa10, "807c5b00", "personal image position getter vtable slot"},
		{0x5b7c80, "510fbf8128190000568b74240c89442404680000c04351db44240c8bced91c246a00ff15c8f967008bc65e59c20400", "image position reads the signed depth field"},
		{0x5bbb0b, "50e82b680b0083c40466898628190000", "explicit menu depth assignment after construction"},
	}};
	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
			if (!limits_rules::matches(image + g.va - image_base, g.hex)) return &g;
		return nullptr;
	}
	// Called immediately after the personal constructor, before menu attributes are parsed.
	// Never touch other image menus or override an explicitly authored depth.
	inline bool configure_menu(void* object)
	{
		if (!object) return false;
		auto* menu = static_cast<std::uint8_t*>(object);
		DWORD vtable = 0;
		std::int16_t depth = 0;
		std::memcpy(&vtable, menu, sizeof vtable);
		std::memcpy(&depth, menu + depth_offset, sizeof depth);
		if (vtable != personal_vtable || depth != retail_depth) return false;
		std::memcpy(menu + depth_offset, &text_background_depth, sizeof text_background_depth);
		return true;
	}
	// Build one CALL rel32, without modifying the image or output on refusal.
	inline bool make_patch(const std::uint8_t* image, bool enabled, DWORD target, std::array<std::uint8_t, 5>& patch)
	{
		if (!enabled || first_mismatch(image)) return false;
		const DWORD displacement = target - (constructor_call + 5);
		patch[0] = 0xe8;
		std::memcpy(patch.data() + 1, &displacement, sizeof displacement);
		return true;
	}
}
