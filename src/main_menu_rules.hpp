#pragma once

// [Game] MainMenuItems: the item names XMen2.exe's main menu treats specially, for a mod whose main
// menu has its own (the X-Men Legends 1 port uses XML1's menu, its text on button1..button7), kept
// apart from the patching so xml2_test can check it without the game: the ini value's rules, the
// name slots, every push of a slot's name in the menu's code, every retail byte the change relies
// on and the operands it writes.
//
// The research is in the xml1-port repository, research/frontend/M2_DESIGN.md (0.2 and A.4.6); every
// address below was read again from the retail XMen2.exe for this code (image base 0x400000, no
// relocations) and is in `guards`.
//
// What the game does: a menu file with type="MAIN_MENU" is built by 0x5b7e00 (the factory's compare
// at 0x5b855d), whose vtable 0x69f134 has four functions that look items up by name - the item
// lookup 0x5adc10 and the text setter 0x5adcf0 take the name - with XML2's names pushed as string
// constants:
//   - the mouse (vt+0x80, 0x5c9320) looks up nine items into an array: label_option04, 05, 06, 07,
//     08, 09, debug_text, debug, debug_focus (pushes 0x5c933f..0x5c93bf). Each one that is there and
//     enabled (+0x54 bit 8) is hit-tested against its screen rectangle (+0x70..+0x76, the IGB node's
//     bounds, 0x5bc530); the hit slot, clamped to 6 (0x5c944b: cmp edi, 6 / mov ebx, 6 / jge / mov
//     ebx, edi), is focused (vt+4 with the array's item of that slot, 0x5c95b7), and a left-button
//     release on the focused slot presses accept for the player (0x5c947c). The clamp is what makes a
//     click on the two Quit models (slots 7 and 8) a click on Quit (slot 6). Missing items are skipped
//     (only the e3 build's "retail only" popup, [0x6f3c2d], would read one). After a focus change it
//     sets the Quit item's text (0x5c95f4, 0x5c961d).
//   - the update (vt+0x38, 0x5c9640): the Quit item (debug_text, 0x5c9691) reads "Quit" ("~20Quit"
//     while focused, 0x5c96c2 / 0x5c96f8), and accept on it sets the quit flag 0x6f3a2d (0x5c96dd) -
//     the only way out: no console command or script function quits. Then the focused item's name is
//     compared with the cell 0x6e662c (label_option09: openmenu online, 0x5c9803) and the cell
//     0x6e6628 (label_option06: at story level 6 or more the line "set drmode 1;openmenu
//     danger_room", else popup 636; 0x5c988e).
//   - the open (vt+0x44, 0x5c9970) sets the Quit item's text (0x5c9993), replaces its usecmd (+0x24,
//     where the item parser keeps it, 0x5bc8a2) with the dummy "Bidon" (0x5c99a1) and shows
//     desctext1.
//   - the first update step (vt+0x10, 0x5c9260) disables label_option06, 07 and 09 in the e3 build
//     ([0x7298a8] == 2; build.ini says "normal").
//
// MainMenuItems names the items for the nine slots in the mouse's order. Every push of a slot's name
// in those four functions (19, `sites`) is pointed at the DLL's copy of the name given; a slot left
// empty keeps the game's. So the first six are mouse slots (and the e3 disables), the seventh is Quit
// in every respect, the last two the Quit button's model and focus model (a click on them counts as
// the seventh). A menu with one item more than XML2's (the X-Men Legends 1 port's Play Online, its
// button7; its Quit is button8, the seventh name) gives the eighth slot a name: the clamp's compare
// becomes cmp edi, 8 (its imm8 at 0x5c944d; the mov ebx, 6 after it stays: slot 8 still gets Quit's
// index), so a click on slot 7 focuses and accepts that item itself, like the first six, and only
// slot 8 (debug_focus, or the ninth name) still counts as Quit. The slot array itself has nine
// entries (the loop's cmp edi, 9 at 0x5c9540) and nothing else sizes it. The two cells are left alone: the
// Danger Room gate and Play Online stay on items named label_option06 / label_option09 (a mod gives
// its Danger Room item a usecmd instead, the port's Play Online "openmenu online"). Nothing
// outside the four functions changes: the item parser (0x5bca21 / 0x5bca57, the "debug" exception
// of menu "main") and another menu class (0x5cc426..0x5cc476) push the same strings.

#include "limits_rules.hpp" // guard, matches, image_base, operand_write

#include <Windows.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace main_menu_rules
{
	using limits_rules::guard;
	using limits_rules::image_base;
	using limits_rules::operand_write;

	// ---- The slots ------------------------------------------------------------------------------------

	struct slot
	{
		std::string_view retail; // the name the game pushes
		DWORD retail_va;         // where that string is
		const char* role;
	};

	constexpr std::size_t slot_count = 9;
	constexpr std::size_t quit_slot = 6;
	constexpr std::size_t name_max = 31; // the DLL keeps each name in 32 bytes

	inline constexpr std::array<slot, slot_count> slots{{
		{"label_option04", 0x6a135c, "mouse slot 0"},
		{"label_option05", 0x6a134c, "mouse slot 1"},
		{"label_option06", 0x6a1290, "mouse slot 2"}, // the Danger Room gate compares the cell 0x6e6628, left alone
		{"label_option07", 0x6a12d8, "mouse slot 3"}, // after a float constant, so no NUL before it: pushed directly
		{"label_option08", 0x6a133c, "mouse slot 4"},
		{"label_option09", 0x6a1280, "mouse slot 5"}, // Play Online compares the cell 0x6e662c, left alone
		{"debug_text", 0x6a0000, "Quit"},
		{"debug", 0x68c51c, "a seventh mouse slot when named (the Quit button's model otherwise)"},
		{"debug_focus", 0x69fff4, "the Quit button's focus model"},
	}};

	// Every push of a slot's name in MAIN_MENU's four functions: push imm32 at `push`, the operand at push + 1.
	struct site
	{
		DWORD push;
		std::uint8_t slot;
		const char* what;
	};

	inline constexpr std::array<site, 19> sites{{
		{0x5c92b7, 2, "first update step: disabled in the e3 build"},
		{0x5c92c5, 3, "first update step: disabled in the e3 build"},
		{0x5c92d3, 5, "first update step: disabled in the e3 build"},
		{0x5c933f, 0, "mouse: looked up into slot 0"},
		{0x5c934f, 1, "mouse: looked up into slot 1"},
		{0x5c935f, 2, "mouse: looked up into slot 2"},
		{0x5c936f, 3, "mouse: looked up into slot 3"},
		{0x5c937f, 4, "mouse: looked up into slot 4"},
		{0x5c938f, 5, "mouse: looked up into slot 5"},
		{0x5c939f, 6, "mouse: looked up into slot 6"},
		{0x5c93af, 7, "mouse: looked up into slot 7"},
		{0x5c93bf, 8, "mouse: looked up into slot 8"},
		{0x5c95f4, 6, "mouse: its text after a focus change (\"~20Quit\")"},
		{0x5c961d, 6, "mouse: its text after a focus change (\"Quit\")"},
		{0x5c9691, 6, "update: is it focused (accept sets the quit flag 0x6f3a2d)"},
		{0x5c96c2, 6, "update: its text while focused (\"~20Quit\")"},
		{0x5c96f8, 6, "update: its text (\"Quit\")"},
		{0x5c9993, 6, "open: its text (\"Quit\")"},
		{0x5c99a1, 6, "open: its usecmd replaced by the dummy \"Bidon\""},
	}};

	// MAIN_MENU's four functions lie in this range; no other code in it refers to the nine strings.
	constexpr DWORD code_begin = 0x5c9260;
	constexpr DWORD code_end = 0x5c99ee;

	constexpr std::uint8_t push_imm32 = 0x68;

	// The mouse's clamp: cmp edi, 6 at 0x5c944b; a named slot 7 moves it to 8 (see above).
	constexpr std::size_t own_mouse_slot = 7;    // the slot that becomes a mouse slot when named
	constexpr DWORD clamp_compare = 0x5c944b;    // 83 ff 06: cmp edi, 6
	constexpr DWORD clamp_operand = clamp_compare + 2;
	constexpr std::uint8_t retail_clamp = 6;     // slots 6, 7 and 8 focus Quit
	constexpr std::uint8_t eight_item_clamp = 8; // only slot 8 does

	// ---- The ini value --------------------------------------------------------------------------------

	// The names for the nine slots; an empty one keeps the game's. Both `set` false and `error` empty: the
	// key isn't set.
	struct items_choice
	{
		std::array<std::string, slot_count> names;
		std::string error;
		bool set = false;
	};

	inline bool name_character(const char c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
	}

	inline std::string_view trimmed(std::string_view s)
	{
		const auto first = s.find_first_not_of(" \t");
		if (first == std::string_view::npos)
		{
			return {};
		}
		return s.substr(first, s.find_last_not_of(" \t") - first + 1);
	}

	inline std::string lower(std::string_view s)
	{
		std::string out(s);
		for (auto& c : out)
		{
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return out;
	}

	// The name slot `i` ends up with: the one given, or the game's.
	inline std::string_view effective(const items_choice& choice, const std::size_t i)
	{
		return choice.names[i].empty() ? slots[i].retail : std::string_view(choice.names[i]);
	}

	// "button1,button2,button3,button4,button5,button6,button8,button7": up to nine names, comma separated,
	// for label_option04, 05, 06, 07, 08, 09, debug_text, debug, debug_focus in that order; an empty entry
	// or a missing one keeps the game's name. An eighth name is a seventh mouse slot (see above). A name
	// is letters, digits and _ (an item name of the menu file), at most name_max characters. No two slots
	// may end up with the same name, the game's included (ignoring case): the mouse would count one item
	// twice and move the focus back and forth. A ';' starts a comment (the fix's one ini rule).
	inline items_choice parse_items(std::string_view raw)
	{
		raw = ini_rules::value_text(raw);
		if (raw.empty())
		{
			return {};
		}
		items_choice refused;
		items_choice chosen;
		chosen.set = true;
		std::size_t index = 0;
		for (std::size_t start = 0;; ++index)
		{
			const auto comma = raw.find(',', start);
			const auto entry = trimmed(raw.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
			if (index >= slot_count)
			{
				refused.error = "has more than " + std::to_string(slot_count) + " names - the game's main menu has nine name slots (label_option04..09, debug_text, debug, debug_focus)";
				return refused;
			}
			for (const char c : entry)
			{
				if (!name_character(c))
				{
					refused.error = "has \"" + std::string(entry) + "\" - an item name is letters, digits and _ only";
					return refused;
				}
			}
			if (entry.size() > name_max)
			{
				refused.error = "has \"" + std::string(entry) + "\", " + std::to_string(entry.size()) + " characters - at most " + std::to_string(name_max);
				return refused;
			}
			chosen.names[index] = std::string(entry);
			if (comma == std::string_view::npos)
			{
				break;
			}
			start = comma + 1;
		}
		for (std::size_t i = 0; i < slot_count; ++i)
		{
			for (std::size_t j = i + 1; j < slot_count; ++j)
			{
				if (lower(effective(chosen, i)) == lower(effective(chosen, j)))
				{
					refused.error = "gives \"" + std::string(effective(chosen, i)) + "\" to two slots (" + std::string(slots[i].retail) + " and " +
					                std::string(slots[j].retail) + ") - the mouse would count that item twice";
					return refused;
				}
			}
		}
		return chosen;
	}

	// Whether slot `i` gets a name other than the game's.
	inline bool changes(const items_choice& choice, const std::size_t i)
	{
		return !choice.names[i].empty() && choice.names[i] != slots[i].retail;
	}

	// ---- The patch ------------------------------------------------------------------------------------

	// Every byte of XMen2.exe the change relies on, read from the retail build. All must match before any
	// operand is written; xml2_test compares them with a copy of the exe.
	inline constexpr std::array<guard, 23> guards{{
		// The class and its four functions.
		{0x5b855d, "68c8fb690056e8fa9f0b0083c40885c0752b8bcfe82aecffff894424288944241085c0c744241c060000000f84f70300008bc8e86bf8ffff",
		 "a menu of type=\"MAIN_MENU\" is built by 0x5b7e00 (0x5b855d)"},
		{0x69fbc8, "4d41494e5f4d454e5500", "the type name \"MAIN_MENU\" (0x69fbc8)"},
		{0x5b7e1c, "c70634f16900", "0x5b7e00 sets the vtable 0x69f134 (0x5b7e1c)"},
		{0x69f144, "60925c00", "MAIN_MENU vt+0x10 = its first update step 0x5c9260 (0x69f144)"},
		{0x69f16c, "40965c00", "MAIN_MENU vt+0x38 = its update 0x5c9640 (0x69f16c)"},
		{0x69f178, "70995c00", "MAIN_MENU vt+0x44 = its open 0x5c9970 (0x69f178)"},
		{0x69f1b4, "20935c00", "MAIN_MENU vt+0x80 = its mouse handler 0x5c9320 (0x69f1b4)"},
		// The pushes and the code around them.
		{0x5c92a5, "833da898720002c60541828a0000752a6a006890126a008bcee8ad4bfeff6a0068d8126a008bcee89f4bfeff6a006880126a008bcee8914bfeff",
		 "the first update step: the e3 build disables label_option06, 07 and 09 (0x5c92a5)"},
		{0x5c9320,
		 "83ec60a1f8386f00535556578bf133ff8944246c89be2c190000e8811dfeff685c136a008bce89442418e8c148feff684c136a008bce8944242ce8b148feff6890126a008bce"
		 "89442430e8a148feff68d8126a008bce89442434e89148feff683c136a008bce89442438e88148feff6880126a008bce8944243ce87148feff6800006a008bce89442440e861"
		 "48feff681cc568008bce89442444e85148feff68f4ff69008bce89442448e84148feff8b2d30f2670089442448c7442410ffffffff8b4cbc2885c90f84b3000000f64154080f"
		 "84a90000008bc10fbf50760fbf58722bda4b895c241c0fbf5070895424180fbf5072895424240fbf50740fbf40708d5402ff3b4c2414895424207504897c24108b4424788b4c"
		 "247450518d54242052ffd585c00f84f400000083ff06bb060000007d028bdf8b4424103bd80f852401000081bc2480000000020200000f85cb000000a120396f005f5e5dc680"
		 "a09fa00001c70485549fa00004000000b0015b8b4c245ce8c78c0a0083c460c2140081bc2480000000020200000f858e000000a02d3c6f0084c00f84810000008b44bc280fbf"
		 "48760fbf50722bd14a8954241c0fbf4870894c24180fbf5072895424240fbf48740fbf50708d4411ff8b4c24788b5424745189442424528d44242050ffd585c07439e8f51d02"
		 "008b106a008bc8ff5214e8e71d02008b1068f8126a008bc8ff5218e8d61d02008b106a018bc8ff5238e8c81d02008b108bc8ff52684783ff090f8c98feffff",
		 "the mouse handler: nine items looked up by name, hit-tested, slot clamped to 6, accept on a click (0x5c9320-0x5c9548)"},
		{0x5c95cb,
		 "8b4424403944241468f0126a00753de811060600508d4c245468e8126a0051e8fd8a0a008d54245c526800006a0056e8f146feff83c41c5f5e5d32c05b8b4c245ce8508b0a0083"
		 "c460c21400e8d4050600506800006a0056e8c846feff8b4c247c83c410",
		 "the mouse handler: the Quit item's text after a focus change (0x5c95cb)"},
		{0x5c9688,
		 "8bcd8bf8e82f1afeff6800006a008bcd8bf0e87145feff3bf068f0126a00754ae843050600508d54242068e8126a0052e82f8a0a008d442428506800006a0055e82346feff8bcbc1"
		 "e90483c41cf6c101742c5f5e5dc6052d3a6f00015b8b4c2428e8738a0a0083c42cc3e8f9040600506800006a0055e8ed45feff83c410",
		 "the update: the Quit item's text, and accept on it sets the quit flag 0x6f3a2d (0x5c9688)"},
		{0x5c9803, "8b352c666e00", "the update: Play Online compares the focused item with the cell 0x6e662c (0x5c9803)"},
		{0x5c988e, "8b3528666e00", "the update: the Danger Room compares the focused item with the cell 0x6e6628 (0x5c988e)"},
		{0x5c98fb, "6884d16800", "the update: the Danger Room's line (0x5c98fb)"},
		{0x5c9981,
		 "e82a5efeff68f0126a008ad8e85e020600506800006a0056e85243feff83c4106800006a008bcee86342feff8bf885ff74256a02687c136a00e86187030083c4044050687c136a"
		 "00e8328803008bc8e88b0ae5ff8947246a01686ce369008bcee82a44feff",
		 "the open: the Quit item's text and its usecmd (\"Bidon\"), desctext1 shown (0x5c9981)"},
		// The strings and the two cells.
		{0x6e6628, "90126a0080126a00", "the cells 0x6e6628 = \"label_option06\", 0x6e662c = \"label_option09\" (0x6e6628)"},
		{0x6a1280, "6c6162656c5f6f7074696f6e303900006c6162656c5f6f7074696f6e30360000", "\"label_option09\" and \"label_option06\" (0x6a1280)"},
		{0x6a12d8, "6c6162656c5f6f7074696f6e303700", "\"label_option07\" (0x6a12d8)"},
		{0x6a133c, "6c6162656c5f6f7074696f6e303800006c6162656c5f6f7074696f6e303500006c6162656c5f6f7074696f6e303400",
		 "\"label_option08\", \"label_option05\" and \"label_option04\" (0x6a133c)"},
		{0x69fff4, "64656275675f666f6375730064656275675f7465787400", "\"debug_focus\" and \"debug_text\" (0x69fff4)"},
		{0x68c51c, "646562756700", "\"debug\" (0x68c51c)"},
		{0x68d184, "7365742064726d6f646520313b6f70656e6d656e752064616e6765725f726f6f6d00", "the line \"set drmode 1;openmenu danger_room\" (0x68d184)"},
		{0x6a12e8, "7e323025730000005175697400", "\"~20%s\" and \"Quit\" (0x6a12e8)"},
	}};

	constexpr std::string_view danger_room_line = "set drmode 1;openmenu danger_room"; // what a mod's Danger Room item runs

	// Every site's operand still the retail string of its slot: push imm32.
	inline bool sites_are_retail(const std::uint8_t* image)
	{
		for (const auto& s : sites)
		{
			const std::uint8_t* at = image + (s.push - image_base);
			std::uint32_t value = 0;
			std::memcpy(&value, at + 1, sizeof(value));
			if (at[0] != push_imm32 || value != slots[s.slot].retail_va)
			{
				return false;
			}
		}
		return true;
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

	// Whether slot 7 is a mouse slot of its own (the clamp moved to 8), not a model of the Quit button.
	inline bool eight_items(const items_choice& choice)
	{
		return changes(choice, own_mouse_slot);
	}

	// The operand writes: every site of a slot that changes takes that slot's pointer (the DLL's copy of
	// its name), and a named slot 7 moves the mouse's clamp to 8. `pointers[i]` is ignored for a slot
	// that keeps the game's name.
	inline std::vector<operand_write> writes_for(const items_choice& choice, const std::array<std::uint32_t, slot_count>& pointers)
	{
		std::vector<operand_write> writes;
		for (const auto& s : sites)
		{
			if (changes(choice, s.slot))
			{
				writes.push_back({s.push + 1, 4, pointers[s.slot]});
			}
		}
		if (eight_items(choice))
		{
			writes.push_back({clamp_operand, 1, eight_item_clamp});
		}
		return writes;
	}

	// The span the writes lie in: one page of the exe's code.
	constexpr DWORD span_begin = 0x5c92b8;
	constexpr DWORD span_end = 0x5c99a6;

	inline void apply(std::uint8_t* image, const std::vector<operand_write>& writes)
	{
		for (const auto& w : writes)
		{
			limits_rules::apply_write(image, w);
		}
	}
}
