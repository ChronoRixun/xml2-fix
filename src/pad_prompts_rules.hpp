#pragma once

// [Input] Prompts: the button prompts show the pad's buttons for a player who plays with a pad, as the
// console versions did ("X Talk to Jean", the skills screen's power wheel, the menus' hint bar, the
// tutorial hints), and the power wheel shows the buttons that fire its powers. Kept apart from the
// patching so xml2_test can check it without the game: the ini values, the game's UI codes and binding
// layout, the pad's names, the choice of binding, which device a player uses, every retail byte the
// change relies on and every byte it writes.
//
// The research is in the xml1-port repository, research/input/prompts.md; every address below was read
// from the retail XMen2.exe (image base 0x400000, no relocations) and is in `guards`.
//
// How the game shows a prompt. Text carries tokens: $ATTACK, $MENU_BACK, "$GUARD Talk to %s"... The font
// manager's token parser (CFontMgr vt+0x3c, 0x5972d0) turns a name into a UI code - through the controller
// name map (0x5538b0: MOVE_X 0, MOVE_Y 1, ATTACK/MENU_ACCEPT 4, SMASH 5, MOVE 6 (jump), POWER 7,
// GUARD/MENU_SUBTRACT 8, ALLY/MENU_DETAILS 9, SOLO 10, NEXT/DPAD_UP 0xb, PREV/DPAD_DN 0xc, TARGET_LOCK/
// MENU_DROP 0xd, MAP_TOGGLE 0xf, DPAD_RT 0x10, DPAD_LF 0x11, MENU 0x12, PAUSE 0x13, MENU_OK 0x14,
// MENU_BACK 0x15, MENU_NEXT 0x16, MENU_PREV 0x17, MENU_OTHER 0x18) or its own list (POWER01..11 0x29..0x33,
// MOVE_* and CAMERA_*, GUAR1..4 0x34..0x37, SOL1..4 0x4c..0x4f, GUAR9 0x50, ATTAC9 0x51, SMAS9 0x52,
// SOL9 0x53) - and hands back the string id 0xF000 + code. The text renderer (0x5ef2e0; the width
// measures 0x596df0 and 0x597c90 alike) asks CStrings::get (vt+8, 0x4bd720) for that id, which calls
// the label function 0x619e30 - from 0x4bd739, its only caller:
//   - code -> binding row, player column and action: GUAR1..4 / SOL1..4 are Guard / Solo of player 1..4
//     in the current row, GUAR9 / ATTAC9 / SMAS9 / SOL9 Guard / LowAttack / HighAttack / Solo of player 1
//     in row 0, anything else the action of 0x619c40 for player 1 in the current row;
//   - the row is [0xa6ac04], set every 5th frame by 0x61c3b0: 0 in play, 1 in menus, 2 on the text entry
//     screen, 3 in the team menu (the skills screen and its power wheel), 4 in [0xa53ee8]'s mode;
//   - the player's binding map for that row: [0xa68f40 + (row * 4 + column) * 4] + 0x18, four binding
//     slots per action (+4 + (action * 4 + slot) * 12: device, control, value), the registry's
//     <Action>1 / <Action>2 in slots 0 and 1;
//   - the first bound one of slots 2, 0, 1 and 1 again (slot 3 is never looked at) is the prompt:
//     "[%s]" of 0x6281f0's name for it (keyboard: the key names in igct.bnx; pads: "Btn %s", "PoV %s",
//     "Axis %s"), "[???]" when no slot is bound.
// At start 0x61b030 loads row 0 from the registry, copies it into rows 1 and 3, takes the keys the menus
// use off them and gives player 1 fixed menu keys in slot 2 - in row 3: LowAttack J, HighAttack Esc,
// Jump Space, Guard E, Ally O, TargetLock P, Pause Enter (KP Enter in slot 3), the arrows. So:
//   - player 1's pad (slot 1) never shows while a keyboard key is bound (slot 0 comes first);
//   - the skills screen's power wheel, whose four labels are the tokens $ATTACK, $SMASH, $GUARD and $MOVE
//     (0x6e76d8, read by 0x5dd7aa and the AI power list at 0x5dfd58), shows the team menu's fixed keys:
//     [J], [Esc], [E], [Space] - Esc being the menus' back key, which HighAttack doubles as there - not
//     the keys that fire the powers in play. Stock XML2 shows the same; the fix's pad bindings don't
//     touch rows 1-4 or this function.
//
// Prompts=auto|pad|keyboard (auto when unset) replaces the call at 0x4bd739 with the fix's label
// function, which reads the same maps and rows:
//   - the power wheel's tokens become Raven's own row-0 ones, $ATTAC9 / $SMAS9 / $GUAR9 (the parser has
//     had them all along; nothing in the game's data used them), and $SMASH (code 5, only ever the SMASH
//     token - the menus' back is MENU_BACK, 0x15) and $MOVE (code 6, only ever MOVE - the menus' other is
//     MENU_OTHER, 0x18) are read in row 0 wherever they appear: they name the buttons of play;
//   - for a player shown the pad: the first slot bound to a pad, named as the fix's pad profile has it
//     (a Logitech Dual Action: buttons 1-12 X A B Y LB RB LT RT Back Start LS RS, the hat the D-pad,
//     X/Y the left stick, Z/Rz the right one); none bound: the keyboard's;
//   - for a player shown the keyboard: the first keyboard or mouse slot in the game's order (2, 0, 1, 3),
//     else the first bound one (a pad-only player's pad, named as above);
//   - auto: the device of the player's last new input - a key or mouse button pressed on the keyboard
//     the game reads, or a button, the D-pad or a stick pushed past half way on the pad of the player's
//     bindings (the map's +0x964, set by the game to its first pad binding), taken from the game's own
//     input state after its per-frame poll (0x6285c0, called at 0x61c479). Before either has been used:
//     the pad when it is connected. A player without keyboard bindings (players 2-4) is shown the pad,
//     one without a pad binding the keyboard, whose pad isn't read (unplugged) the keyboard.
// PromptColors=1 (the default) draws A, B, X and Y as the letter in the Xbox colours - the game's own
// colours 14 (green), 16 (red), 15 (blue) and 17 (yellow), Data/colors.xmlb - where the renderer draws a
// one-character label: it gives such a label colour 41 (white, 0x5ef777), meant for the consoles' button
// glyphs; the fix makes that colour depend on the character. The other buttons read "[LB]", "[Start]",
// "[D-pad Up]". Colour codes can't be put in a label itself: the renderer reads a "~NN" code's digits
// from the text around the token, not from the label. PromptColors=0: "[A]" too.
// Prompts=off: nothing patched, the game's own prompts.

#include "limits_rules.hpp" // guard, matches, hex_size, hex_byte, image_base

#include <Windows.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace pad_prompts_rules
{
	using limits_rules::guard;
	using limits_rules::image_base;

	// ---- The game ---------------------------------------------------------------------------------------

	constexpr DWORD label_call = 0x4bd739;     // call 0x619e30 in CStrings::get (0x4bd720)
	constexpr DWORD label_function = 0x619e30; // const char* __cdecl (int code)
	constexpr DWORD action_function = 0x619c40;
	constexpr DWORD key_name_function = 0x6281f0; // const char* __thiscall (input, int device, int control), ret 8
	constexpr DWORD input_pointer = 0xa6abfc;     // the input object (CInput's configuration: keyboard, mouse, pads)
	constexpr DWORD row_global = 0xa6ac04;        // the binding row in use, 0..4
	constexpr DWORD maps_global = 0xa68f40;       // 5 rows x 4 players of controller configurations
	constexpr DWORD poll_call = 0x61c479;         // call 0x6285c0, the per-frame input poll, in 0x61c3b0
	constexpr DWORD poll_function = 0x6285c0;     // void __thiscall (input)
	constexpr DWORD wheel_tokens = 0x6e76d8;      // "$ATTACK", "$SMASH", "$GUARD", "$MOVE": the power wheel's labels
	constexpr DWORD colour_site = 0x5ef777;       // mov word [esp+esi*2+0x80], 0x411: a one-character label's colour
	constexpr DWORD blank_label = 0x684868;       // " " (what the game returns for an unknown code)
	constexpr DWORD no_name_label = 0x6a4c44;     // " " (a binding 0x6281f0 has no name for)
	constexpr DWORD unbound_label = 0x6a4e6c;     // "[???]"

	constexpr int rows = 5;
	constexpr int players = 4;
	constexpr int slot_count = 4;

	// A player's binding map: the configuration + 0x18.
	constexpr std::size_t map_in_configuration = 0x18;
	constexpr std::size_t map_action_count = 0x0;
	constexpr std::size_t map_records = 0x4;
	constexpr std::size_t record_size = 0xc; // device, control, value
	constexpr std::size_t map_pad_device = 0x964;

	// The input object, as the poll leaves it: the state it just read and the one before.
	constexpr std::size_t keyboard_now = 0x25e4, keyboard_before = 0x26e4;             // 256 DIK bytes each
	constexpr std::size_t mouse_buttons_now = 0x4c8, mouse_buttons_before = 0x4dc;     // DIMOUSESTATE rgbButtons
	constexpr std::size_t pad_devices = 0xc;                                           // 10 IDirectInputDevice8*
	constexpr std::size_t pad_now = 0x4f0, pad_before = 0x4f0 + 0xaa0, pad_stride = 0x110; // DIJOYSTATE2
	constexpr std::size_t pads_read = 0x129cc;                                         // bit i: pad i's state was read
	constexpr int pad_count = 10;
	constexpr std::size_t input_size = 0x12a10; // what 0x61bae0 allocates
	// DIJOYSTATE2
	constexpr std::size_t pad_axes = 0x0, pad_pov = 0x20, pad_buttons = 0x30;
	constexpr int pad_axis_count = 6, pad_button_count = 128;
	constexpr long pad_axis_half = 500; // the game sets every axis to -1000..1000

	// Binding devices: 1 keyboard (control = DIK code), 2 mouse, 3-12 pads (pad index = device - 3).
	constexpr std::uint32_t keyboard_device = 1, mouse_device = 2, first_pad_device = 3, last_pad_device = 12;

	constexpr bool keyboard_or_mouse(const std::uint32_t device) { return device == keyboard_device || device == mouse_device; }
	constexpr bool pad_device(const std::uint32_t device) { return device >= first_pad_device && device <= last_pad_device; }

	// ---- The ini values ---------------------------------------------------------------------------------

	enum class mode
	{
		automatic,
		keyboard,
		pad,
		off,
	};

	struct mode_choice
	{
		mode value = mode::automatic;
		bool set = false;
		std::string error;
	};

	inline std::string_view trimmed(std::string_view s)
	{
		if (const auto comment = s.find(';'); comment != std::string_view::npos)
		{
			s = s.substr(0, comment);
		}
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

	// auto, pad, keyboard or off, any case; a ';' starts a comment. Unset: auto.
	inline mode_choice parse_mode(const std::string_view raw)
	{
		const auto value = lower(trimmed(raw));
		if (value.empty())
		{
			return {};
		}
		if (value == "auto")
		{
			return {mode::automatic, true, {}};
		}
		if (value == "pad")
		{
			return {mode::pad, true, {}};
		}
		if (value == "keyboard")
		{
			return {mode::keyboard, true, {}};
		}
		if (value == "off")
		{
			return {mode::off, true, {}};
		}
		mode_choice refused;
		refused.error = "isn't auto (the device each player last used), pad, keyboard or off (the game's own prompts)";
		return refused;
	}

	struct switch_choice
	{
		bool value = true;
		bool set = false;
		std::string error;
	};

	// 1/0, on/off, true/false, yes/no; unset: 1.
	inline switch_choice parse_switch(const std::string_view raw)
	{
		const auto value = lower(trimmed(raw));
		if (value.empty())
		{
			return {};
		}
		if (value == "1" || value == "on" || value == "true" || value == "yes")
		{
			return {true, true, {}};
		}
		if (value == "0" || value == "off" || value == "false" || value == "no")
		{
			return {false, true, {}};
		}
		switch_choice refused;
		refused.error = "isn't 1 or 0";
		return refused;
	}

	inline const char* describe(const mode m)
	{
		switch (m)
		{
		case mode::automatic:
			return "auto";
		case mode::keyboard:
			return "keyboard";
		case mode::pad:
			return "pad";
		case mode::off:
			return "off";
		}
		return "?";
	}

	// ---- UI codes -----------------------------------------------------------------------------------------

	// The action of a UI code, 0x619c40's table (codes 0..0x33); -1 for none.
	inline constexpr std::array<int, 0x34> actions{{
		2,    0,    0x15, 0x13, 4,    5,    6,    8,    7,    9,    0xb,  0xc,  0xd,  0xa,  -1,   0x10, // 0x00
		0xf,  0xe,  0x12, 0x11, 0x11, 5,    8,    0xb,  6,    -1,   -1,   -1,   -1,   0x17, 3,    1,    // 0x10
		0x16, 0x14, 0x1b, 0x19, 0x18, 0x1a, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, // 0x20
		0x26, 0x27, 0x28, 0x29,                                                                         // 0x30
	}};

	constexpr int action_of(const int code)
	{
		return code >= 0 && code < static_cast<int>(actions.size()) ? actions[static_cast<std::size_t>(code)] : -1;
	}

	struct target
	{
		int code;        // the UI code whose action is shown
		int player;      // the binding column, 0..3
		bool play_row;   // read in row 0 (play), not the current row
	};

	// What 0x619e30 does with a code before it looks up the action.
	constexpr target stock_target(const int code)
	{
		if (code >= 0x34 && code <= 0x37) return {8, code - 0x34, false};    // GUAR1..4
		if (code >= 0x4c && code <= 0x4f) return {10, code - 0x4c, false};   // SOL1..4
		if (code == 0x50) return {8, 0, true};                               // GUAR9
		if (code == 0x51) return {4, 0, true};                               // ATTAC9
		if (code == 0x52) return {5, 0, true};                               // SMAS9
		if (code == 0x53) return {10, 0, true};                              // SOL9
		return {code, 0, false};
	}

	// SMASH (5) and MOVE (6): the only tokens with those codes, the buttons of play.
	constexpr bool play_token(const int code) { return code == 5 || code == 6; }

	// The fix's: the game's, with SMASH and MOVE read in row 0.
	constexpr target prompt_target(const int code)
	{
		auto t = stock_target(code);
		t.play_row = t.play_row || play_token(code);
		return t;
	}

	// ---- Binding maps --------------------------------------------------------------------------------------

	struct binding
	{
		std::uint32_t device = 0;
		std::uint32_t control = 0;
	};

	using action_bindings = std::array<binding, slot_count>;

	inline std::uint32_t read_u32(const std::uint8_t* at)
	{
		std::uint32_t value = 0;
		std::memcpy(&value, at, sizeof(value));
		return value;
	}

	// An action's four slots in a binding map (all unbound past the map's action count).
	inline action_bindings read_bindings(const std::uint8_t* map, const int action)
	{
		action_bindings slots{};
		if (action < 0 || static_cast<std::uint32_t>(action) >= read_u32(map + map_action_count))
		{
			return slots;
		}
		for (int slot = 0; slot < slot_count; ++slot)
		{
			const auto* record = map + map_records + (static_cast<std::size_t>(action) * slot_count + slot) * record_size;
			slots[static_cast<std::size_t>(slot)] = {read_u32(record), read_u32(record + 4)};
		}
		return slots;
	}

	// The pad a map is bound to: the game's +0x964 (its first pad binding's device), as a pad index; -1 for none.
	inline int pad_of(const std::uint8_t* map)
	{
		const auto device = read_u32(map + map_pad_device);
		return pad_device(device) ? static_cast<int>(device - first_pad_device) : -1;
	}

	// Whether any action of a map has a keyboard or mouse binding.
	inline bool has_keyboard(const std::uint8_t* map)
	{
		const auto count = read_u32(map + map_action_count);
		for (std::uint32_t action = 0; action < count && action < 64; ++action)
		{
			for (const auto& b : read_bindings(map, static_cast<int>(action)))
			{
				if (keyboard_or_mouse(b.device))
				{
					return true;
				}
			}
		}
		return false;
	}

	inline constexpr std::array<int, slot_count> stock_order{{2, 0, 1, 1}};
	inline constexpr std::array<int, slot_count> keyboard_order{{2, 0, 1, 3}};

	// The slot 0x619e30 shows: the first bound in 2, 0, 1, 1; -1 for "[???]".
	inline int stock_slot(const action_bindings& slots)
	{
		for (const int s : stock_order)
		{
			if (slots[static_cast<std::size_t>(s)].device != 0)
			{
				return s;
			}
		}
		return -1;
	}

	// Shown the keyboard: the first keyboard or mouse slot in the game's order, else the first bound one.
	inline int keyboard_slot(const action_bindings& slots)
	{
		for (const int s : keyboard_order)
		{
			if (keyboard_or_mouse(slots[static_cast<std::size_t>(s)].device))
			{
				return s;
			}
		}
		for (const int s : keyboard_order)
		{
			if (slots[static_cast<std::size_t>(s)].device != 0)
			{
				return s;
			}
		}
		return -1;
	}

	// Shown the pad: the first slot bound to a pad, else as for the keyboard.
	inline int pad_slot(const action_bindings& slots)
	{
		for (int s = 0; s < slot_count; ++s)
		{
			if (pad_device(slots[static_cast<std::size_t>(s)].device))
			{
				return s;
			}
		}
		return keyboard_slot(slots);
	}

	// ---- The pad's names ----------------------------------------------------------------------------------

	enum class pad_button
	{
		none,
		a, b, x, y,
		lb, rb, lt, rt,
		back, start, ls, rs,
		dpad_up, dpad_down, dpad_left, dpad_right,
		ls_up, ls_down, ls_left, ls_right,
		rs_up, rs_down, rs_left, rs_right,
	};

	// A pad control of the fix's pad profile (a Logitech Dual Action, pad_profile.cpp): 1-16 axis halves
	// (2n+1 above centre, 2n+2 below; X, Y the left stick, Z, Rz the right, down and right above centre),
	// 17-20 the hat right, left, down, up, 21 + n button n.
	constexpr pad_button button_of(const std::uint32_t control)
	{
		constexpr std::array<pad_button, 12> buttons{{pad_button::x, pad_button::a, pad_button::b, pad_button::y, pad_button::lb, pad_button::rb,
		                                              pad_button::lt, pad_button::rt, pad_button::back, pad_button::start, pad_button::ls, pad_button::rs}};
		if (control >= 21 && control < 21 + buttons.size()) return buttons[control - 21];
		switch (control)
		{
		case 17: return pad_button::dpad_right;
		case 18: return pad_button::dpad_left;
		case 19: return pad_button::dpad_down;
		case 20: return pad_button::dpad_up;
		case 1: return pad_button::ls_right;  // X above
		case 2: return pad_button::ls_left;   // X below
		case 3: return pad_button::ls_down;   // Y above
		case 4: return pad_button::ls_up;     // Y below
		case 5: return pad_button::rs_right;  // Z above
		case 6: return pad_button::rs_left;   // Z below
		case 11: return pad_button::rs_down;  // Rz above
		case 12: return pad_button::rs_up;    // Rz below
		default: return pad_button::none;
		}
	}

	constexpr std::string_view name_of(const pad_button b)
	{
		switch (b)
		{
		case pad_button::a: return "A";
		case pad_button::b: return "B";
		case pad_button::x: return "X";
		case pad_button::y: return "Y";
		case pad_button::lb: return "LB";
		case pad_button::rb: return "RB";
		case pad_button::lt: return "LT";
		case pad_button::rt: return "RT";
		case pad_button::back: return "Back";
		case pad_button::start: return "Start";
		case pad_button::ls: return "LS";
		case pad_button::rs: return "RS";
		case pad_button::dpad_up: return "D-pad Up";
		case pad_button::dpad_down: return "D-pad Down";
		case pad_button::dpad_left: return "D-pad Left";
		case pad_button::dpad_right: return "D-pad Right";
		case pad_button::ls_up: return "LS Up";
		case pad_button::ls_down: return "LS Down";
		case pad_button::ls_left: return "LS Left";
		case pad_button::ls_right: return "LS Right";
		case pad_button::rs_up: return "RS Up";
		case pad_button::rs_down: return "RS Down";
		case pad_button::rs_left: return "RS Left";
		case pad_button::rs_right: return "RS Right";
		case pad_button::none: break;
		}
		return {};
	}

	// The game's colours (Data/colors.xmlb) for the face buttons, 0 for the others.
	constexpr int colour_of(const pad_button b)
	{
		switch (b)
		{
		case pad_button::a: return 14; // 0 0.713 0: green
		case pad_button::b: return 16; // 0.8 0 0: red
		case pad_button::x: return 15; // 0 0.271 0.788: blue
		case pad_button::y: return 17; // 0.808 0.608 0: yellow
		default: return 0;
		}
	}

	// A pad binding's prompt: "A" (drawn in its colour) with colours, "[A]" without; "[LB]", "[D-pad Up]";
	// a control the profile doesn't have: "[Button n]" / "[Axis n+]" / "[Axis n-]".
	inline std::string pad_label(const std::uint32_t control, const bool colours)
	{
		const auto b = button_of(control);
		if (b != pad_button::none)
		{
			if (colours && colour_of(b))
			{
				return std::string(name_of(b));
			}
			return "[" + std::string(name_of(b)) + "]";
		}
		if (control >= 21 && control < 21 + pad_button_count)
		{
			return "[Button " + std::to_string(control - 20) + "]";
		}
		if (control >= 1 && control <= 16)
		{
			return "[Axis " + std::to_string((control - 1) / 2 + 1) + ((control - 1) % 2 == 0 ? "+]" : "-]");
		}
		return "[???]";
	}

	// The renderer's colour for a one-character label, by that character: 1000 + a colours.xmlb id (the
	// game's "~NN"). The game's is 1041 (41, white) for all of them; the face buttons' letters get theirs.
	inline std::array<std::uint16_t, 256> label_colours()
	{
		std::array<std::uint16_t, 256> table{};
		table.fill(1041);
		for (const auto b : {pad_button::a, pad_button::b, pad_button::x, pad_button::y})
		{
			table[static_cast<unsigned char>(name_of(b)[0])] = static_cast<std::uint16_t>(1000 + colour_of(b));
		}
		return table;
	}

	// ---- Which device a player's prompts show ------------------------------------------------------------

	enum class shown
	{
		keyboard,
		pad,
	};

	// The last frame (a count of polls, from 1) with new input from the keyboard and mouse, and from each pad.
	struct activity
	{
		std::uint32_t frame = 0;
		std::uint32_t keyboard = 0;
		std::array<std::uint32_t, pad_count> pads{};
		std::uint32_t pads_present = 0; // bit i: pad i was read at the last poll

		static bool pressed(const std::uint8_t now, const std::uint8_t before) { return (now & 0x80) && !(before & 0x80); }

		static long axis(const std::uint8_t* state, const int i)
		{
			return static_cast<long>(static_cast<std::int32_t>(read_u32(state + pad_axes + 4 * static_cast<std::size_t>(i))));
		}

		// A pad state's new input since the one before: a button pressed, the D-pad pushed or turned, a stick
		// pushed past half way.
		static bool pad_moved(const std::uint8_t* now, const std::uint8_t* before)
		{
			for (int i = 0; i < pad_button_count; ++i)
			{
				if (pressed(now[pad_buttons + i], before[pad_buttons + i]))
				{
					return true;
				}
			}
			const auto pov = read_u32(now + pad_pov);
			if ((pov & 0xffff) != 0xffff && pov != read_u32(before + pad_pov))
			{
				return true;
			}
			for (int i = 0; i < pad_axis_count; ++i)
			{
				const long a = axis(now, i), b = axis(before, i);
				if ((a > pad_axis_half || a < -pad_axis_half) && b <= pad_axis_half && b >= -pad_axis_half)
				{
					return true;
				}
			}
			return false;
		}

		// After the game's poll: `input` is its input object.
		void sample(const std::uint8_t* input)
		{
			++frame;
			bool typed = false;
			for (std::size_t i = 0; i < 256 && !typed; ++i)
			{
				typed = pressed(input[keyboard_now + i], input[keyboard_before + i]);
			}
			for (std::size_t i = 0; i < 4 && !typed; ++i)
			{
				typed = pressed(input[mouse_buttons_now + i], input[mouse_buttons_before + i]);
			}
			if (typed)
			{
				keyboard = frame;
			}
			pads_present = read_u32(input + pads_read) & ((1u << pad_count) - 1);
			for (int i = 0; i < pad_count; ++i)
			{
				if ((pads_present >> i & 1) &&
				    pad_moved(input + pad_now + pad_stride * static_cast<std::size_t>(i), input + pad_before + pad_stride * static_cast<std::size_t>(i)))
				{
					pads[static_cast<std::size_t>(i)] = frame;
				}
			}
		}
	};

	// The device a player's prompts show. `keyboard` whether the player has keyboard or mouse bindings, `pad`
	// the pad of its bindings (-1: none).
	inline shown choose(const mode m, const bool keyboard, const int pad, const activity& seen)
	{
		if (m == mode::pad)
		{
			return shown::pad;
		}
		if (m != mode::automatic || pad < 0 || pad >= pad_count)
		{
			return shown::keyboard;
		}
		if (!keyboard)
		{
			return shown::pad;
		}
		if (!(seen.pads_present >> pad & 1))
		{
			return shown::keyboard; // unplugged, or not read yet
		}
		const auto pad_frame = seen.pads[static_cast<std::size_t>(pad)];
		if (pad_frame == 0 && seen.keyboard == 0)
		{
			return shown::pad; // nothing pressed yet: a connected pad
		}
		return pad_frame >= seen.keyboard ? shown::pad : shown::keyboard;
	}

	// ---- The patch -----------------------------------------------------------------------------------------

	// Every byte of XMen2.exe the change relies on, read from the retail build. All must match before anything
	// is written; xml2_test compares them with a copy of the exe.
	inline constexpr std::array<guard, 23> guards{{
		// The label chain.
		{0x4bd720, "8b442404f6c4f056578bf9743a663d00f0723425ff00000050e8f2c615008a0883c40480f9200f85b3000000",
		 "CStrings::get: ids 0xF000-0xF0FF are the label of code id & 0xff, from 0x619e30 (0x4bd720)"},
		{0x68e998, "20d74b00", "CStrings vt+8 = 0x4bd720 (0x68e998)"},
		{0x619e30,
		 "a1fcaba60083ec0885c07509b86848680083c408c38b44240c5632c933f683f834577c458d50cc83fa1f773d0fb6bab89f6100ff24bd9c9f61008bf2b808000000eb268d70b4"
		 "b80a000000eb1cb804000000eb13b808000000eb0cb80a000000eb05b805000000b10150e8a1fdffff8bf883c40483ffff750b5fb8684868005e83c408c384c9a104aca600"
		 "740233c08d34868d44240c508d4c240c518b0cb5408fa6006a025783c118e8d2f500008b44240885c075778b0cb5408fa6008d54240c528d44240c506a005783c118e8aef5"
		 "00008b44240885c075538d4c240c518b0cb5408fa6008d54240c526a015783c118e88af500008b44240885c0752f8d44240c508d4c240c518b0cb5408fa6006a015783c118"
		 "e866f500008b44240885c0750b5fb86c4e6a005e83c408c38b54240c8b0dfcaba6005250e882e2000085c0741e5068644e6a0068188ca600e86a81050083c40c5fb8188ca6"
		 "005e83c408c35fb8444c6a005e83c408c3906a9e6100739e6100849e61007d9e6100929e61008b9e6100999e6100000000000606060606060606060606060606060606060606"
		 "0101010102030405",
		 "the label function: code -> row [0xa6ac04] / column / action, map [0xa68f40 + i*4] + 0x18, slots 2, 0, 1, 1, \"[%s]\" (0x619e30-0x619fd7)"},
		{0x619c40,
		 "8b44240483f8330f8700010000ff2485549d6100b802000000c333c0c3b815000000c3b813000000c3b804000000c3b807000000c3b809000000c3b80c000000c3b80d0000"
		 "00c3b80a000000c3b810000000c3b80f000000c3b80e000000c3b812000000c3b811000000c3b805000000c3b808000000c3b80b000000c3b806000000c3b817000000c3b8"
		 "03000000c3b801000000c3b816000000c3b814000000c3b81b000000c3b819000000c3b818000000c3b81a000000c3b81c000000c3b81d000000c3b81e000000c3b81f0000"
		 "00c3b820000000c3b821000000c3b822000000c3b823000000c3b824000000c3b825000000c3b826000000c3b827000000c3b828000000c3b829000000c383c8ffc38d4900"
		 "549c61005a9c61005d9c6100639c6100699c6100ab9c6100bd9c6100b19c61006f9c6100759c6100b79c61007b9c6100819c6100879c61004d9d61008d9c6100939c6100"
		 "999c61009f9c6100a59c6100a59c6100ab9c6100b19c6100b79c6100bd9c61004d9d61004d9d61004d9d61004d9d6100c39c6100c99c6100cf9c6100d59c6100db9c6100"
		 "e19c6100e79c6100ed9c6100f39c6100f99c6100ff9c6100059d61000b9d6100119d6100179d61001d9d6100239d6100299d61002f9d6100359d61003b9d6100419d6100"
		 "479d6100",
		 "the UI code -> action table (0x619c40-0x619e23)"},
		{0x6294b0, "8b54240885d27c30568b742408578b7c241485ff740c8d04b28d04408b44810489078b7c241885ff740c8d04b28d14408b44910889075f5ec21000",
		 "a binding's device and control: map + 4 + (action * 4 + slot) * 12 (0x6294b0)"},
		{0x6297a0, "578b7c240c85ff7c3c8b542408568b318d42013bc677028bc68b74241889018d04978b54241483fa038d04408d0481897008895004c7400c000000005e7c068991640900005fc21000",
		 "setting a binding: the action count at +0, a pad device kept at +0x964 (0x6297a0)"},
		{0x6295a0, "8b015633f685c05776228d79048d490033c08bd7833a037d204083c20c83f8047cf28b014683c7303bf072e45f5ec7816409000000000000c38d04b05f8d04408b5481045e899164090000c3",
		 "+0x964 = the first pad binding's device, 0 for none (0x6295a0)"},
		{0x6281f0, "8b54240883faff568bf10f840403000081faffff00000f84f80200008b442408", "a binding's name: __thiscall (input, device, control) (0x6281f0)"},
		{0x628240, "c20800", "... ret 8 (0x628240)"},
		{0x6a4e64, "5b25735d000000005b3f3f3f5d00", "\"[%s]\" and \"[???]\" (0x6a4e64)"},
		{0x684868, "2000", "\" \" (0x684868)"},
		{0x6a4c44, "2000", "\" \" (0x6a4c44)"},
		// The per-frame poll and the input object's layout.
		{0x61c473, "8b0dfcaba600e842c10000", "0x61c3b0 polls the input object every frame: call 0x6285c0 (0x61c473)"},
		{0x6285c0,
		 "83ec0c538bd98b83042a0100555657895c24108983082a0100e83230f3ff8b106a018bc8ff5228d993042a0100d8a3082a01008b430485c0d99b0c2a010074588dabe42500"
		 "00558dbbe4260000b9400000008bf56800010000f3a58b0850ff5124",
		 "the poll: the keyboard's state before at +0x26e4, read to +0x25e4 (0x6285c0)"},
		{0x628689, "8db3bc0400008bce8b118d83d004000089108b51048950048b51088950088b510c8b491089500c56", "the mouse's state before at +0x4d0, read to +0x4bc (0x628689)"},
		{0x628746, "8a83dc04000084c0741e8a83c804000084c074", "the mouse buttons at +0x4c8, before at +0x4dc (0x628746)"},
		{0x6287d0, "8b44241033c98988cc290100894c24148da8f00400008d580c8da424000000008b0385c0b944000000745a8dbda00a00008bf5f3a58b0850",
		 "the pads: devices at +0xc, states at +0x4f0, before at +0x4f0 + 0xaa0, read mask +0x129cc (0x6287d0)"},
		{0x62883e, "442410ba01000000d3e20990cc290100eb12b94400000033c08bfdf3abc74520ffffffff8b4424144083c30481c510010000",
		 "... bit i of +0x129cc for a pad read, 0x110 bytes a pad (0x62883e)"},
		// The power wheel and Raven's row-0 tokens.
		{0x6e76d8, "ec116a00e4116a00844068005c276a00", "the power wheel's labels \"$ATTACK\" \"$SMASH\" \"$GUARD\" \"$MOVE\" (0x6e76d8)"},
		{0x5dd7a8, "8bf88b04b5d8766e00", "the power wheel reads them (0x5dd7a8)"},
		{0x5dfd58, "8b14bdd8766e00", "... and so does the AI power list (0x5dfd58)"},
		{0x59794e,
		 "8d4c2418680cd7690051e8ada80d0083c40885c0740abe51000000e9ce0000008d5424186804d7690052e88da80d0083c40885c0740abe50000000e9ae0000008d4424186"
		 "8fcd6690050e86da80d0083c40885c0740abe53000000e98e0000008d4c241868f4d6690051e84da80d0083c40885c07407be52000000eb71",
		 "the token parser: ATTAC9 0x51, GUAR9 0x50, SOL9 0x53, SMAS9 0x52 (0x59794e)"},
		// The renderer's colour for a one-character label.
		{0x5ef735,
		 "83f90175676685c07507b8a07a6e00eb15e8e5e3ecff8b4c24508b10518bc8ff5208894424188a1884db74308a480184c9752980fbbd742480fbbe741f80fbf8741a66c78474"
		 "80000000110466c7847482000000000046c644240f0140894424188b4c241480fb60740eeb048b4c24140fb6d383fa927502b3278a",
		 "the renderer: a one-character label (not 0xbd 0xbe 0xf8) in colour 41, ended after it (0x5ef735)"},
	}};

	// The row-0 tokens the power wheel gets, and the strings the parser looks for.
	inline constexpr std::array<std::string_view, 3> wheel_replacements{{"$ATTAC9", "$SMAS9", "$GUAR9"}};
	inline constexpr std::array<guard, 1> token_strings{{
		{0x69d6f4, "534d415339000000534f4c3900000000475541523900000041545441433900", "\"SMAS9\" \"SOL9\" \"GUAR9\" \"ATTAC9\" (0x69d6f4)"},
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
		for (const auto& g : token_strings)
		{
			if (!limits_rules::matches(image + (g.va - image_base), g.hex))
			{
				return &g;
			}
		}
		return nullptr;
	}

	struct byte_write
	{
		DWORD va;
		std::vector<std::uint8_t> bytes;
	};

	// Where the DLL's parts are.
	struct addresses
	{
		DWORD label = 0;       // const char* __cdecl (int code)
		DWORD poll = 0;        // void __fastcall (input, edx)
		DWORD colour_stub = 0; // colour_stub() placed there; 0: no colours
		std::array<DWORD, 3> wheel{}; // "$ATTAC9", "$SMAS9", "$GUAR9"
	};

	inline std::vector<std::uint8_t> call_bytes(const DWORD va, const DWORD target)
	{
		const std::uint32_t rel = target - (va + 5);
		std::vector<std::uint8_t> bytes{0xe8, 0, 0, 0, 0};
		std::memcpy(bytes.data() + 1, &rel, 4);
		return bytes;
	}

	inline std::vector<std::uint8_t> dword_bytes(const std::uint32_t value)
	{
		std::vector<std::uint8_t> bytes(4);
		std::memcpy(bytes.data(), &value, 4);
		return bytes;
	}

	// The colour site's stub: ax = the table's word for bl (the label's character), stored where the game
	// stores its 0x411, every register as it was.
	//   push edx; movzx edx, bl; mov dx, [edx*2 + table]; mov [esp + esi*2 + 0x88], dx; pop edx; ret
	// (+0x88: the game's +0x80 past the return address and edx.)
	inline std::vector<std::uint8_t> colour_stub(const DWORD table)
	{
		std::vector<std::uint8_t> stub{0x52, 0x0f, 0xb6, 0xd3, 0x66, 0x8b, 0x14, 0x55, 0, 0, 0, 0, 0x66, 0x89, 0x94, 0x74, 0x88, 0x00, 0x00, 0x00, 0x5a, 0xc3};
		std::memcpy(stub.data() + 8, &table, 4);
		return stub;
	}

	constexpr std::size_t colour_site_size = 10; // the mov it replaces

	// Every write: the two calls, the wheel's three tokens and, with colours, the call to the stub at the colour
	// site (and a 5-byte nop after it).
	inline std::vector<byte_write> writes_for(const addresses& a)
	{
		std::vector<byte_write> writes;
		writes.push_back({label_call, call_bytes(label_call, a.label)});
		writes.push_back({poll_call, call_bytes(poll_call, a.poll)});
		for (std::size_t i = 0; i < a.wheel.size(); ++i)
		{
			writes.push_back({static_cast<DWORD>(wheel_tokens + 4 * i), dword_bytes(a.wheel[i])});
		}
		if (a.colour_stub)
		{
			auto bytes = call_bytes(colour_site, a.colour_stub);
			bytes.insert(bytes.end(), {0x0f, 0x1f, 0x44, 0x00, 0x00});
			writes.push_back({colour_site, bytes});
		}
		return writes;
	}

	inline void apply(std::uint8_t* image, const std::vector<byte_write>& writes)
	{
		for (const auto& w : writes)
		{
			std::memcpy(image + (w.va - image_base), w.bytes.data(), w.bytes.size());
		}
	}
}
