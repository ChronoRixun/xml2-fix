#pragma once

// The Video options list's decisions, kept apart from the hooks so xml2_test can check them: the
// seven places in XMen2.exe that address the game's 20-slot resolution table (which the fix
// relocates to a 64-slot table of its own with [Display] ResolutionList=all), and which sizes the
// list offers in each display mode.
// Every address is the retail build's, read from its disassembly (docs/in-game-options-plan.md,
// 1.4 and 3.1).

#include "d3d8_min.hpp"
#include "display_rules.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace resolution_rules
{
	// ---- The game's table ------------------------------------------------------------------------
	// FUN_00619ac0 lists the adapter's modes (IDirect3D8::GetAdapterModeCount / EnumAdapterModes),
	// keeps one entry per size of at least 640x480, sorts them and sprintf's "%dx%d" into 12-byte
	// slots at 0x6e9800 - 20 of them, with no bounds check: the 21st and on overwrite the default
	// key bindings that follow (24 sizes on a modern PC). The count goes to 0xa68da8 and every
	// reader uses it, so a bigger table needs nothing but its seven references moved. The registry
	// setting an entry is compared with is read into a 10-byte buffer, so an entry's text must fit
	// nine characters ("3840x2160" does; five-digit widths don't).

	constexpr std::size_t stock_slots = 20;
	constexpr std::size_t slots = 64;
	constexpr std::size_t slot_bytes = 12;
	constexpr std::size_t max_text = 9;
	constexpr DWORD stock_table_va = 0x6e9800; // the retail build's, at its image base 0x400000
	constexpr DWORD stock_table_rva = 0x2e9800;

	// One instruction that carries the table's address as an imm32, with the 16 bytes from its
	// start as the retail build has them and where in them the address sits.
	struct table_site
	{
		const char* what;
		DWORD rva;
		std::array<std::uint8_t, 16> expected;
		std::size_t imm_offset; // 1 for mov reg, imm32; 3 for lea eax, [reg*4 + imm32]
	};

	// The seven, in address order; nothing else in the image (code or data) refers to the table or
	// to any address inside it.
	inline constexpr std::array<table_site, 7> table_sites{{
		// FUN_006180e0, the slider's callback: the entry for the knob's index becomes the value label's text.
		{"the resolution slider's entry read (0x6181bf)", 0x2181bf, {0x8D, 0x04, 0x95, 0x00, 0x98, 0x6E, 0x00, 0x50, 0x8D, 0x8C, 0x24, 0xE0, 0x00, 0x00, 0x00, 0x68}, 3},
		// FUN_00619ac0, the list builder: sprintf's every size into the table.
		{"the resolution table writer (0x619b95)", 0x219b95, {0xBB, 0x00, 0x98, 0x6E, 0x00, 0x8B, 0xFD, 0x8D, 0x64, 0x24, 0x00, 0x8B, 0x47, 0x04, 0x8B, 0x0F}, 1},
		// FUN_0061d550, Accept: the chosen entry is compared with the current setting (the restart warning).
		{"Accept's resolution entry read (0x61d61f)", 0x21d61f, {0x8D, 0x04, 0x85, 0x00, 0x98, 0x6E, 0x00, 0x8D, 0x54, 0x24, 0x0C, 0x2B, 0xD0, 0x8D, 0x64, 0x24}, 3},
		// FUN_0061dc10, the panel builder: finds the current setting's index for the slider.
		{"the panel builder's resolution search (0x61e636)", 0x21e636, {0xB8, 0x00, 0x98, 0x6E, 0x00, 0x89, 0x44, 0x24, 0x14, 0x90, 0xBE, 0x9C, 0x8D, 0xA6, 0x00, 0x8A}, 1},
		// FUN_0061f380, the close function: the accepted entry, compared before the registry save.
		{"the panel's close-time resolution read (0x61f57b)", 0x21f57b, {0x8D, 0x04, 0x85, 0x00, 0x98, 0x6E, 0x00, 0x8D, 0x54, 0x24, 0x18, 0x2B, 0xD0, 0x8A, 0x08, 0x88}, 3},
		// FUN_0061f380, revert to default: finds the default setting's index for the slider.
		{"the revert's resolution search (0x61f6a1)", 0x21f6a1, {0xBD, 0x00, 0x98, 0x6E, 0x00, 0xBE, 0x9C, 0x8D, 0xA6, 0x00, 0x8B, 0xD5, 0x8D, 0x49, 0x00, 0x8A}, 1},
		// FUN_0061f380, the close function: the accepted entry becomes the setting that is saved.
		{"the panel's resolution setting copy (0x61f843)", 0x21f843, {0x8D, 0x04, 0x85, 0x00, 0x98, 0x6E, 0x00, 0xBA, 0x9C, 0x8D, 0xA6, 0x00, 0x2B, 0xD0, 0x8A, 0x08}, 3},
	}};

	inline bool matches(const table_site& site, const std::uint8_t* bytes)
	{
		return std::memcmp(bytes, site.expected.data(), site.expected.size()) == 0;
	}

	// The address the expected bytes carry (an offline check that the table is consistent).
	inline DWORD decoded_address(const table_site& site)
	{
		DWORD address = 0;
		std::memcpy(&address, site.expected.data() + site.imm_offset, sizeof(address));
		return address;
	}

	// ---- The list -------------------------------------------------------------------------------

	inline std::string text_of(const UINT width, const UINT height)
	{
		return std::to_string(width) + "x" + std::to_string(height);
	}

	// Whether the entry's text fits the game's registry read (nine characters).
	inline bool fits(const UINT width, const UINT height)
	{
		return text_of(width, height).size() <= max_text;
	}

	// Within one per cent of the same width-to-height ratio (1366x768 counts as 16:9, 3440x1440
	// and 2560x1080 as the same 21:9; 1280x1024 is not 1280x960's).
	inline bool same_aspect(const UINT width_a, const UINT height_a, const UINT width_b, const UINT height_b)
	{
		if (!width_a || !height_a || !width_b || !height_b)
		{
			return false;
		}
		const auto cross_a = static_cast<long long>(width_a) * height_b;
		const auto cross_b = static_cast<long long>(width_b) * height_a;
		return std::llabs(cross_a - cross_b) * 100 <= cross_a;
	}

	// Sizes people expect to find, by aspect ratio; the adapter lists most of them for its own
	// monitor, the rest matter in a window. All fit nine characters.
	inline constexpr std::array<display_rules::size, 30> common_sizes{{
		{640, 480}, {800, 600}, {1024, 768}, {1152, 864}, {1280, 960}, {1400, 1050}, {1600, 1200}, {2048, 1536}, // 4:3
		{1280, 1024},                                                                                            // 5:4
		{1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3200, 1800}, {3840, 2160},           // 16:9
		{1280, 800}, {1440, 900}, {1680, 1050}, {1920, 1200}, {2560, 1600}, {3840, 2400},                        // 16:10
		{2560, 1080}, {3440, 1440}, {3840, 1600}, {5120, 2160},                                                  // 21:9
		{3840, 1080}, {5120, 1440},                                                                              // 32:9
		{1920, 1440}, {2560, 1920},                                                                              // 4:3, large
	}};

	// The sizes a display mode can offer beyond the adapter's modes. Exclusive fullscreen (the
	// game's own mode and Mode=fullscreen) can only use modes the adapter has, so nothing. In a
	// window (borderless, windowed) the back buffer can be any size: the common sizes of the
	// desktop's aspect ratio up to the desktop, and half and three quarters of the desktop
	// (render-scale presets; borderless stretches them to the screen), rounded down to even numbers.
	inline std::vector<display_rules::size> extra_sizes(const display_rules::size& desktop, const display_rules::mode mode)
	{
		std::vector<display_rules::size> extras;
		if (!display_rules::manages_window(mode) || !desktop.width || !desktop.height)
		{
			return extras;
		}
		for (const auto& common : common_sizes)
		{
			if (common.width <= desktop.width && common.height <= desktop.height && same_aspect(common.width, common.height, desktop.width, desktop.height))
			{
				extras.push_back(common);
			}
		}
		for (const unsigned quarters : {2u, 3u})
		{
			extras.push_back({(desktop.width * quarters / 4) & ~1u, (desktop.height * quarters / 4) & ~1u});
		}
		return extras;
	}

	// The Video options list: the adapter's sizes of at least 640x480, the desktop's, the forced
	// Width x Height and, in a window, extra_sizes; one entry per size, ascending, every text within
	// nine characters, and at most `cap` (the table's slots) - the smallest go when there are more.
	inline std::vector<d3d8::display_mode> build_list(const std::vector<d3d8::display_mode>& adapter_modes, const display_rules::size& desktop,
	                                                  const UINT refresh_rate, const std::optional<display_rules::size>& forced, const display_rules::mode mode,
	                                                  const std::size_t cap)
	{
		std::vector<d3d8::display_mode> candidates;
		for (const auto& entry : adapter_modes)
		{
			if (fits(entry.width, entry.height))
			{
				candidates.push_back(entry);
			}
		}
		for (const auto& extra : extra_sizes(desktop, mode))
		{
			if (fits(extra.width, extra.height))
			{
				candidates.push_back({extra.width, extra.height, refresh_rate, d3d8::format_x8r8g8b8});
			}
		}
		const auto listed_desktop = fits(desktop.width, desktop.height) ? desktop : display_rules::size{};
		const auto listed_forced = forced && fits(forced->width, forced->height) ? forced : std::nullopt;
		return display_rules::curate_modes(candidates, listed_desktop, refresh_rate, listed_forced, cap);
	}

	// The list the fix's mode-list hooks answer with, by [Display] ResolutionList. Without the key
	// it is exactly what the fix listed before the 64-slot table existed (only reached in its own
	// modes: the adapter's sizes, the desktop's and the forced one, in the game's 20 slots); game
	// and all get build_list within `cap`, the slots of the table in use.
	inline std::vector<d3d8::display_mode> video_list(const std::vector<d3d8::display_mode>& adapter_modes, const display_rules::size& desktop,
	                                                  const UINT refresh_rate, const std::optional<display_rules::size>& forced, const display_rules::mode mode,
	                                                  const display_rules::resolution_list setting, const std::size_t cap)
	{
		if (setting == display_rules::resolution_list::stock)
		{
			return display_rules::curate_modes(adapter_modes, desktop, refresh_rate, forced, stock_slots);
		}
		return build_list(adapter_modes, desktop, refresh_rate, forced, mode, std::min(cap, setting == display_rules::resolution_list::all ? slots : stock_slots));
	}
}
