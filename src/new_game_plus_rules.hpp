#pragma once

// [Game] NewGamePlus=0: New Game never offers XML2's "use saved game statistics" choice, for a mod
// whose campaign had no New Game+ (the X-Men Legends 1 port: XML1 asked for neither a difficulty nor
// statistics). Kept apart from the patching so xml2_test can check it without the game: every retail
// byte the change relies on and the two bytes it writes.
//
// The research is in the xml1-port repository (tools/xml1build/frontend.py BEGIN_STORY_CMD, SPEC 21.2.2);
// every address below was read again from the retail XMen2.exe for this code (image base 0x400000, no
// relocations) and is in `guards`.
//
// What the game does: New Game (the console command newgame, 0x5f3610) runs resetgame, then
// startgamedialog (0x5f2090), the Easy / Normal / Hard prompt; each option runs the script function
// setDifficultyLevel(n) (0x4a0930, registered from the script table entry at 0x68b7a8). That writes the
// difficulty (the game object 0x729960, vt+0x26c: [+0x60c]) and asks the profile (0x72c530, vt+0xb0
// 0x48f730) whether Hard is unlocked: bit 2 of its byte +0x229, which the end credits set after a win on
// Normal (0x5b1d44). Not unlocked: it has the console queue "runscript startFirstMission()" (0x4a0ab1) -
// the starting team, then Scripts/menus/new_game.py - and hands the menu's controller on (0x4a0acf).
// Unlocked: it opens a second prompt instead (0x4a09a4: text 0x86a "Choose a saved game to load
// character statistics from, or use the default character statistics.", options useDefaultStats() /
// useSavedStats(), back = startGameDiffDialog()).
//
// The change: the je at 0x4a099e that takes the not-unlocked path when the profile says no becomes a
// jmp to the same place (nop + jmp rel32, the displacement kept), so New Game starts at once with the
// default statistics, as it does before Hard is unlocked. The profile, the Hard option of the
// difficulty prompt and every other reader of the bit are left alone.

#include "limits_rules.hpp" // guard, matches, image_base

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>

namespace new_game_plus_rules
{
	using limits_rules::guard;
	using limits_rules::image_base;

	constexpr DWORD offer_branch = 0x4a099e;     // je 0x4a0a9c (0f 84 rel32) after the profile's answer
	constexpr DWORD start_path = 0x4a0a9c;       // the not-unlocked path: queue startFirstMission
	constexpr std::array<std::uint8_t, 6> retail_branch{0x0f, 0x84, 0xf8, 0x00, 0x00, 0x00};
	constexpr std::array<std::uint8_t, 6> patched_branch{0x90, 0xe9, 0xf8, 0x00, 0x00, 0x00}; // nop; jmp 0x4a0a9c

	// Every byte of XMen2.exe the change relies on, read from the retail build. All must match before the
	// branch is written; xml2_test compares them with a copy of the exe.
	inline constexpr std::array<guard, 9> guards{{
		{0x68b7a8, "30094a00e4be6800189c680030ce6800",
		 "the script table entry: setDifficultyLevel -> 0x4a0930, returns nothing, takes an int (0x68b7a8)"},
		{0x68bee4, "736574446966666963756c74794c6576656c00", "the name \"setDifficultyLevel\" (0x68bee4)"},
		{0x4a0930,
		 "83ec44a1f8386f008b4c24485356576a0089442450e8e64e03008b108bc8ff52108bf0e888d3fcff8b10568bc8ff926c020000e8a8a8160083b8c001000001751ce86ad3fcff"
		 "8b108bc8ff92680200008bd8e8592217008898df030000e83ef5feff8b108bc8ff92b000000084c00f84f8000000",
		 "setDifficultyLevel (0x4a0930-0x4a09a3): the difficulty written, the profile asked, je 0x4a0a9c when Hard isn't unlocked"},
		{0x4a0a9c,
		 "e86fa7160083b8c0010000017431e8e1bd0b008b1068a07268008bc8ff521ce8607e13008bf0e879d3feff8bf88b068b1f8bceff9038020000508bcfff53348b4c244c5f5e33c05b"
		 "e878161d0083c444c3",
		 "the not-unlocked path (0x4a0a9c-0x4a0aec): the console queues \"runscript startFirstMission()\", the controller handed on"},
		{0x6872a0, "72756e73637269707420737461727446697273744d697373696f6e282900", "the line \"runscript startFirstMission()\" (0x6872a0)"},
		{0x48fed0,
		 "64a1000000008a0d88c972006aff68de3e670050b80100000084c8648925000000007525090588c97200b930c57200c744240800000000e8b4f1ffff68a0dc6700e808221e00"
		 "83c4048b0c24b830c5720064890d0000000083c40cc3",
		 "the profile's getter (0x48fed0): the object at 0x72c530"},
		{0x689a44, "30f74800", "the profile's vt+0xb0 = 0x48f730 (0x689a44)"},
		{0x48f730, "8a8129020000c0e8022401c3", "Hard unlocked = bit 2 of the profile's +0x229 (0x48f730)"},
		{0x469d40, "8b44240489810c060000c2", "the difficulty setter (vt+0x26c, 0x469d40): [+0x60c]"},
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

	// Where the 6 bytes at `at` (the code at `va`) jump: a je rel32 or a nop + jmp rel32; 0 for anything else.
	inline DWORD branch_target(const std::uint8_t* at, const DWORD va)
	{
		std::int32_t rel = 0;
		std::memcpy(&rel, at + 2, sizeof(rel));
		const bool je = at[0] == 0x0f && at[1] == 0x84;
		const bool nop_jmp = at[0] == 0x90 && at[1] == 0xe9;
		return je || nop_jmp ? static_cast<DWORD>(va + 6 + rel) : 0;
	}

	// The change: the je at 0x4a099e always taken.
	inline void apply(std::uint8_t* image)
	{
		std::memcpy(image + (offer_branch - image_base), patched_branch.data(), patched_branch.size());
	}
}
