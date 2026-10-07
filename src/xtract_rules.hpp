#pragma once

// [Game] Xtract=0: an Xtraction Point's menu never offers XML2's "Xtract" choice (the world map of
// its five town centres), for a mod whose campaign has no world map (the X-Men Legends 1 port:
// XML1's extractionPoint offers the title, Change Team, Save and Load, and under conditions the
// Danger Room, the Healer and the Forge - no world map; the port keeps XML2's town centres in its
// zone data because the engine's extraction needs them there, not to be offered). Kept apart from
// the patching so xml2_test can check it without the game: every retail byte the change relies on
// and the two bytes it writes.
//
// Every address below was read from the retail XMen2.exe for this code (image base 0x400000, no
// relocations) and is in `guards`.
//
// What the game does: the script function extractionPoint (0x4a6b50) builds its dialog
// unconditionally - the title (id 2000, 0x4a6c6a-0x4a6c77), then Xtract (id 2040,
// 0x4a6c7a-0x4a6ca8: the choice's line is openmenu('worldmap'), the string at 0x68d4e0), then
// Change Team (id 2001, extractionPointChange(%d,0)) and Save (id 2002, saveloadProcess(4)). There
// is no unlock test; XML1's strings have no id 2040.
//
// The change: the Xtract block's first two bytes (the start of its first call) become
// `jmp 0x4a6ca9` (eb 2d), so the choice is never added. The skip is safe:
//   - the block's calls take their arguments on the stack and pop them there (the same shape as
//     the title's two calls, which no `add esp` follows either), so esp is as it was;
//   - of the registers the block sets, esi and edi are reloaded before their next use (0x4a6cc6,
//     0x4a6cd9) and ebp is never read again (its only later appearance is the pop at 0x4a6d5f);
//   - the only value read right after the block is ebx (the point's entity, at 0x4a6ca9), set at
//     0x4a6b84, outside the block;
//   - no branch lands inside the block: the function's conditional jumps target 0x4a6c45, 0x4a6d60
//     and 0x4a6d61, all outside it.
// The menu's other choices, the world map itself and the town centres' zone data stay as they are.

#include "limits_rules.hpp" // guard, matches, image_base

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>

namespace xtract_rules
{
	using limits_rules::guard;
	using limits_rules::image_base;

	constexpr DWORD choice_block = 0x4a6c7a;    // the Xtract choice add: call 0x4bdb30, ...
	constexpr DWORD next_choice = 0x4a6ca9;     // the Change Team add that follows it
	constexpr std::array<std::uint8_t, 2> retail_op{0xe8, 0xb1}; // call 0x4bdb30 (rel32 b16e0100)
	constexpr std::array<std::uint8_t, 2> patched_jump{0xeb, 0x2d}; // jmp 0x4a6ca9

	// Every byte of XMen2.exe the change relies on, read from the retail build. All must match
	// before the jump is written; xml2_test compares them with a copy of the exe.
	inline constexpr std::array<guard, 4> guards{{
		{0x4a6b7f, "e82ce9fbff8bd885db0f84d3010000", "extractionPoint keeps the point's entity in ebx (0x4a6b84), the one value read after the block"},
		{choice_block,
		 "e8b16e01008bf0e87a4614008b166a006a016a016a0068e0d468008bf88b2f68f80700008bceff5208508bcfff551c",
		 "the Xtract choice (0x4a6c7a-0x4a6ca8): id 2040, openmenu('worldmap'), added unconditionally"},
		{next_choice, "8b431c5068c4d468008d4c24206a4051e832980b0083c410",
		 "the Change Team add (0x4a6ca9): the entity's handle in ebx, extractionPointChange(%d,0)"},
		{0x68d4e0, "6f70656e6d656e752827776f726c646d6170272900", "the line \"openmenu('worldmap')\" (0x68d4e0)"},
	}};

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

	// Where a `jmp rel8` at `at` (the code at `va`) goes; 0 for anything else.
	inline DWORD jump_target(const std::uint8_t* at, const DWORD va)
	{
		return at[0] == 0xeb ? static_cast<DWORD>(va + 2 + static_cast<std::int8_t>(at[1])) : 0;
	}

	// The change: the Xtract block is jumped over.
	inline void apply(std::uint8_t* image)
	{
		std::memcpy(image + (choice_block - image_base), patched_jump.data(), patched_jump.size());
	}
}
