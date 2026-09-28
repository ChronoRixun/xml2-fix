#pragma once

// [Game] PostgameScript: what runs after the end credits, for a mod with its own campaign (the
// X-Men Legends 1 port plays XML1's r505 and goes back to the main menu), kept apart from the
// patching so xml2_test can check it without the game: the ini value's rules, the line the game is
// handed, every retail byte the change relies on and the one operand it writes.
//
// The research is in the xml1-port repository, research/frontend/M2_DESIGN.md (D.1); every address
// below was read again from the retail XMen2.exe for this code (image base 0x400000, no
// relocations) and is in `guards`.
//
// What the game does: a campaign's last script opens the credits menu (XML2's UI/menus/credits_end,
// which XML1's finale opens too) with endgame="true", which CREDITS_MENU (vtable 0x69f1d4; its
// constructor 0x5b7e30) reads into bit 0 of +0x1954 (0x5b1950, vt+0x5c). Its step function (0x5b1c60,
// vt+0x38) runs a small state machine at +0x1958 once the credits have rolled: state 3 records the
// win in the profile, state 4 waits for any popup and has the console run "runscript
// saveloadProcess(2)" (push 0x69e7c8 at 0x5b1df4) - the end-of-game save - and moves to state 2;
// state 2 waits for the save and any popup, has the console run "runscript
// loadZone('act5/egypt/egypt6','')" (push 0x69e7e8 at 0x5b1cbf) and closes the menu (the menu
// manager's vt+0x74 with no name: the current menu, the credits). Without endgame="true" (the
// credits from the main menu) neither line runs.
//
// XML2's line loads XML2's last zone. With PostgameScript set, the push at 0x5b1cbf is pointed at a
// line of the DLL's own, "runscript <name>", and the game runs Scripts/<name>.py there instead; the
// save step before it is left as it is. The console runs the line at once (vt+0x18, 0x55beb0): its
// first word names the command, runscript (0x5f2350) reads ONE more word (0x55b670: it stops at
// any byte up to 0x20, every non-ASCII byte too, and at ';'), and the script interface's loader
// (vt+0xc, 0x4a11c0) takes a word without '(' as a script file: "scripts/" + word + ".py" (0x592520,
// which leaves out the "scripts/" when the word has it already), then runs it at once (vt+0x3c). A
// script that opens a menu (startMovie is "openmenu movie") only asks for it - the menu manager
// opens it at its next update (0x5d5db0 keeps the name at +0x85db0) - so the credits' own close
// right after still closes the credits, and the movie comes up next, as the boot intro's do.

#include "limits_rules.hpp" // guard, matches, image_base

#include <Windows.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace postgame_rules
{
	using limits_rules::guard;
	using limits_rules::image_base;

	// ---- The ini value --------------------------------------------------------------------------------

	constexpr std::string_view runscript_prefix = "runscript ";
	constexpr std::size_t console_max = 127;                                   // what the game's console keeps of a line
	constexpr std::size_t name_max = console_max - runscript_prefix.size(); // 117

	// A script name, or why the value isn't one. Both empty: the key isn't set.
	struct script_choice
	{
		std::string name;
		std::string error;
	};

	inline bool name_character(const char c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '/';
	}

	// "x1/menus/postgame" (Scripts\x1\menus\postgame.py): letters, digits, _ and / only, so the
	// console hands runscript all of it as one word and the loader takes it as a file (no '(');
	// no / at either end or two together; not "scripts/" inside it (the loader would leave its own
	// "scripts/" out); at most name_max characters, so "runscript <name>" fits the console's 127. A
	// ';' starts a comment: no name has one.
	inline script_choice parse_script(std::string_view raw)
	{
		if (const auto comment = raw.find(';'); comment != std::string_view::npos)
		{
			raw = raw.substr(0, comment);
		}
		const auto first = raw.find_first_not_of(" \t");
		if (first == std::string_view::npos)
		{
			return {};
		}
		const std::string name(raw.substr(first, raw.find_last_not_of(" \t") - first + 1));

		script_choice refused;
		for (const char c : name)
		{
			if (!name_character(c))
			{
				const auto byte = static_cast<unsigned char>(c);
				if (c == '.')
				{
					refused.error = "has a '.' - name the script without its .py (x1/menus/postgame for Scripts\\x1\\menus\\postgame.py)";
				}
				else if (c == '\\')
				{
					refused.error = "has a '\\' - separate folders with / (x1/menus/postgame)";
				}
				else if (byte > 0x20 && byte < 0x7f)
				{
					refused.error = std::string("has a '") + c + "' - a script name is letters, digits, _ and / only";
				}
				else
				{
					char what[16];
					std::snprintf(what, sizeof(what), "0x%02X", byte);
					refused.error = std::string("has a space or byte ") + what + " inside it - a script name is letters, digits, _ and / only (the game's console ends the name there)";
				}
				return refused;
			}
		}
		if (name.front() == '/' || name.back() == '/' || name.find("//") != std::string::npos)
		{
			refused.error = "has a / at an end or two together - a script name is folders and a file, x1/menus/postgame";
			return refused;
		}
		std::string lower = name;
		for (auto& c : lower)
		{
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		if (lower.find("scripts/") != std::string::npos)
		{
			refused.error = "has \"scripts/\" in it - name the script as it sits under the Scripts folder (x1/menus/postgame); the game adds Scripts itself, and leaves it out for a name that has it";
			return refused;
		}
		if (name.size() > name_max)
		{
			refused.error = "is " + std::to_string(name.size()) + " characters; the game's console keeps 127, " + std::to_string(name_max) + " after \"runscript \"";
			return refused;
		}
		return {name, {}};
	}

	// The line the credits hand the console.
	inline std::string command_line(const std::string_view name)
	{
		return std::string(runscript_prefix) + std::string(name);
	}

	// Where the game looks for the script, from the game folder: Scripts\x1\menus\postgame.py.
	inline std::string script_file(const std::string_view name)
	{
		std::string path = "Scripts\\";
		for (const char c : name)
		{
			path += c == '/' ? '\\' : c;
		}
		return path + ".py";
	}

	// ---- The patch ------------------------------------------------------------------------------------

	constexpr DWORD load_push = 0x5b1cbf;         // push imm32 in CREDITS_MENU's state 2
	constexpr DWORD load_operand = load_push + 1; // the one dword written
	constexpr DWORD retail_load_line = 0x69e7e8;  // "runscript loadZone('act5/egypt/egypt6','')"
	constexpr DWORD save_push = 0x5b1df4;         // push imm32 in state 4, left as it is
	constexpr DWORD retail_save_line = 0x69e7c8;  // "runscript saveloadProcess(2)"
	constexpr std::string_view retail_load_text = "runscript loadZone('act5/egypt/egypt6','')";
	constexpr std::string_view retail_save_text = "runscript saveloadProcess(2)";

	// Every byte of XMen2.exe the change relies on, read from the retail build. All must match before
	// the operand is written; xml2_test compares them with a copy of the exe.
	inline constexpr std::array<guard, 20> guards{{
		// The credits menu and its end-of-game steps.
		{0x5b7e5f, "c706d4f16900", "CREDITS_MENU's constructor sets its vtable 0x69f1d4 (0x5b7e5f)"},
		{0x69f20c, "601c5b00", "CREDITS_MENU vt+0x38 = its step function 0x5b1c60 (0x69f20c)"},
		{0x69f230, "50195b00", "CREDITS_MENU vt+0x5c = 0x5b1950, which reads endgame= (0x69f230)"},
		{0x5b195e, "6894e769008bcfe85630fbff85c074316894e769008bcfe84630fbff68bc53680050e8dd0b0c008a8e5419000083c40885c00f94c032c1240132c8888e54190000",
		 "endgame=\"true\" sets bit 0 of +0x1954 (0x5b195e)"},
		{0x5b1c81,
		 "8b8658190000bf020000003bc77553e8eb0bf0ff8b108bc8ff521885c07543e85b9603008b108bc8ff527884c07533849e541900007411e8d3abfaff8b1068e8e769008bc8ff5218"
		 "e8526c02008b106a006a008bc8ff52745f8bce5e5be98d960000",
		 "state 2: waits for the save and any popup, runs the line pushed at 0x5b1cbf when endgame, closes the credits (0x5b1c81-0x5b1ce2)"},
		{0x5b1dbc,
		 "83be5819000004754ae8b60af0ff8b108bc8ff521885c0753ae8269503008b108bc8ff527884c0752a849e541900007411e89eaafaff8b1068c8e769008bc8ff52185d89be58190000"
		 "5f8bce5e5be961950000",
		 "state 4: runs \"runscript saveloadProcess(2)\" when endgame, then state 2 (0x5b1dbc-0x5b1e0e)"},
		{0x69e7e8, "72756e736372697074206c6f61645a6f6e652827616374352f65677970742f656779707436272c27272900",
		 "the retail line \"runscript loadZone('act5/egypt/egypt6','')\" (0x69e7e8)"},
		{0x69e7c8, "72756e73637269707420736176656c6f616450726f6365737328322900", "the retail line \"runscript saveloadProcess(2)\" (0x69e7c8)"},
		// The console: the getter, vt+0x18 and the run-now it calls, the runscript command and the word it reads.
		{0x55c890, "8a0dccca7a00b80100000084c875258b15ccca7a000bd0b990c27a008915ccca7a00e889feffff68d0e06700e85d58110083c404b890c27a00c3",
		 "the console's getter (0x55c890)"},
		{0x69a81c, "30c2550020c3550070b65500e0b65500e0c5550020be5500b0be550010c45500", "the console's vtable: vt+0x18 = 0x55beb0 (0x69a81c)"},
		{0x55beb0,
		 "518b44240885c056578bf989442410747e80380074798b078d4c2410518bcfff50088bf08a063c3b745d3c0a745984c074206a0056e836620a0083c404405056e84b620a008bc8e8"
		 "24e4ebff89442408eb08c7442408000000008b57088d7704528d44240c508bcee8b3f804003dffffff3f75043bf6741f8d4c241051ff94862804000083c4048b44241085c07582"
		 "5fb0015e59c204005f32c05e59c20400",
		 "the console's run-now (0x55beb0): the first word names the command, whose handler reads the rest"},
		{0x5f4abf, "6850235f0068e0386a00", "runscript's registration (0x5f4abf)"},
		{0x6a38e0, "72756e73637269707400", "the name \"runscript\" (0x6a38e0)"},
		{0x5f2350,
		 "81ec04020000a1f8386f0089842400020000e829a5f6ff8b8c24080200008b10518bc8ff52086800020000508d54240852e8eafd070083c40cc68424ff01000000e8daf2eaff8b10"
		 "8d0c24518bc8ff520ce8caf2eaff8b106a018d4c2404518bc8ff523ce8b7f2eaff8b108d0c24518bc8ff52108b8c2400020000e891fd070081c404020000c3",
		 "runscript's handler (0x5f2350): one word, loaded (vt+0xc), run at once (vt+0x3c), freed (vt+0x10)"},
		{0x55b670,
		 "56578b7c240c33f685ff8d813c060000c60000744b8b175385d2740b8a1a84db740580fb207e05803a3b750442ebe9908a1a80fb207e1480fb3b740f81fe000200007d0d881c30"
		 "464275e581fe000200005b750233f6c6840e3c0600000089175f5ec20400",
		 "the console's word reader (0x55b670)"},
		// The script interface: its getter and vtable, the loader, the path it builds.
		{0x4a1670, "8a0d046b7500b80100000084c8752e8b0d046b75000bc833c0890d046b7500c705786a75006cd36800a37c6a7500a3806a7500a2846a7500a2c46a7500b8786a7500c3",
		 "the script interface's getter (0x4a1670, vtable 0x68d36c)"},
		{0x68d378, "c0114a00", "the script interface's vt+0xc = the loader 0x4a11c0 (0x68d378)"},
		{0x4a11c0,
		 "81ec18020000a1f8386f0053568bb424240200006810010000898424200200008d4424105650e8850f1d00687420680056c684242f01000000e80c101d0083c4148bd8f7db1adbfe"
		 "c380fb01752b6860d368006854d3680056e802130f006810010000508d4c242051e8420f1d0083c418c684241b01000000",
		 "the loader (0x4a11c0): a word without '(' is a script file, \"scripts/\" + word + \".py\""},
		{0x592520,
		 "56578b7c240cbaa8dc7f008bc72bd7908a08880c024084c975f68b74241085f674225657e8c1fc0d0083c40885c075145756681030680068a8dc7f00e88bfb0d0083c4108b7424"
		 "1485f674265657e897fc0d0083c40885c075185668a8dc7f00681030680068a8dc7f00e85dfb0d0083c410",
		 "the path builder (0x592520): the prefix and the suffix, each unless the word has it"},
		{0x68d354, "736372697074732f000000002e707900", "\"scripts/\" and \".py\" (0x68d354, 0x68d360)"},
	}};

	// The operand's own retail bytes, inside the state 2 guard: push 0x69e7e8.
	inline bool operand_is_retail(const std::uint8_t* image)
	{
		const std::uint8_t* at = image + (load_push - image_base);
		std::uint32_t value = 0;
		std::memcpy(&value, at + 1, sizeof(value));
		return at[0] == 0x68 && value == retail_load_line;
	}

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

	// The change: the push at 0x5b1cbf takes `line`, the address of "runscript <name>".
	inline void apply(std::uint8_t* image, const std::uint32_t line)
	{
		std::memcpy(image + (load_operand - image_base), &line, sizeof(line));
	}
}
