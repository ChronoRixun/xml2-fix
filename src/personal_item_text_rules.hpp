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
	// [Game] PersonalItemPicture=window: the first game's picture window. XMen2.exe stretches a menu image over
	// the whole 512x384 virtual screen (size getter vtable +0x90 = 0x5b7c60, position getter +0x94 = 0x5b7c80,
	// z = 384 is the sprite's top edge); default.xbe's personal menu adds it at 256x128, x 128..384, z 202..330
	// (top 330) in front of the menu IGB's panels. Only PERSONAL_MENU's two vtable slots change.
	constexpr DWORD size_slot = 0x69fa0c;
	constexpr DWORD position_slot = 0x69fa10;
	constexpr DWORD retail_size_getter = 0x5b7c60;
	constexpr DWORD retail_position_getter = 0x5b7c80;
	constexpr DWORD vec2_constructor_import = 0x67f95c;
	constexpr DWORD vec3_constructor_import = 0x67f9c8;
	constexpr float window_width = 256.0f;
	constexpr float window_height = 128.0f;
	constexpr float window_x = 128.0f;
	constexpr float window_top = 330.0f;
	inline constexpr std::array<guard, 4> picture_guards{{
		{0x69fa0c, "607c5b00807c5b00", "personal image size and position getter vtable slots"},
		{0x5b7c60, "568b742408680000c04368000000448bceff155cf967008bc65ec20400", "image size getter (512x384)"},
		{0x5b7c80, "510fbf8128190000568b74240c89442404680000c04351db44240c8bced91c246a00ff15c8f967008bc65e59c20400", "image position getter reads the signed depth field"},
		{0x5b8180, "568bf1e848faffffc7067cf969008bc65ec3", "personal constructor and vtable"},
	}};
	inline const guard* first_picture_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : picture_guards)
			if (!limits_rules::matches(image + g.va - image_base, g.hex)) return &g;
		return nullptr;
	}
	// The two vtable pointers the patch writes (size, position), as little-endian bytes.
	inline std::array<std::uint8_t, 8> make_slots(DWORD size_thunk, DWORD position_thunk)
	{
		std::array<std::uint8_t, 8> out{};
		std::memcpy(out.data(), &size_thunk, 4);
		std::memcpy(out.data() + 4, &position_thunk, 4);
		return out;
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
