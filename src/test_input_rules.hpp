#pragma once

// The test input pipe's rules, kept apart from the pipe and the hooks so xml2_test can check
// them: DirectInput key names, the one-line commands, the synthetic key state that is merged
// into the keyboard state the game reads, and the lines "script" and "console" hand to the
// game's console queue, with the retail code that queue is.

#include "limits_rules.hpp" // guard, matches: retail bytes compared before use

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace test_input_rules
{
	constexpr DWORD default_tap_ms = 80;
	constexpr DWORD max_hold_ms = 10000;       // a key the pipe holds down is released after this whatever the client does
	constexpr unsigned char key_down = 0x80;   // the "pressed" bit of a DirectInput keyboard state byte

	// ---- The game's console queue (script, console) ---------------------------------------------------
	//
	// XMen2.exe's console is a static object at 0x7ac290, handed out by 0x55c890 (it builds it at the
	// first call). Its vtable 0x69a81c: vt+0 (0x55c230) runs every queued command, vt+8 (0x55b670)
	// reads one word, vt+0x18 (0x55beb0) runs a line now, vt+0x1c (0x55c410) queues one:
	// `bool __thiscall queue(const char* line)`, ret 4, false when the line is empty or two commands
	// are already waiting ([this+0x630] == 2); it keeps 127 characters (strncpy 0x80, then a NUL at
	// [127]). The game's frame (0x401d70) runs the queue at 0x40220f and reads the keyboard at
	// 0x402875 (0x61c300 -> 0x6285c0 -> GetDeviceState(256)), both on one thread, so the pipe queues
	// from inside that keyboard read and the line runs at the next frame. The game queues this way
	// itself: loadMapKeepTeam (0x4a0d03), the team menu's confirm, the Danger Room's loadmap.
	//
	// "script STATEMENT" queues "runscript STATEMENT". The runscript command (0x5f2350, registered
	// at 0x5f4abf) takes ONE word: vt+8 stops at any byte up to 0x20 (a signed compare, so every
	// non-ASCII byte too) and at ';'. Then the script interface (0x4a1670) loads the word (vt+0xc,
	// 0x4a11c0: text with a '(' is inline code, split into statements at the four characters \n\r,
	// 0x68d348; anything else is scripts/NAME.py), runs it at once (vt+0x3c, 0x4a1320) and frees it
	// (vt+0x10, 0x4d94d0). So a statement goes without its spaces: outside quotes they are dropped -
	// the script tokenizer (0x4d9740) ends an argument at a space, a tab, a ',' or a ')' alike, so
	// unlockCharacter("storm", "") and unlockCharacter("storm","") are one call, and the game's own
	// runscript lines have none (0x685538: runscript unlockCharacter('','astonishing')) - while
	// inside quotes they can't be sent at all. Quotes go through as they are. Several statements
	// fit on one line joined by the four characters \n\r; `if` needs a space after it, so no
	// conditionals. A statement the game can't compile (unknown function, wrong argument count) is
	// dropped without a word, as it is from a script file.

	constexpr std::size_t console_max = 127;                // what the queue keeps of a line
	constexpr std::string_view runscript_prefix = "runscript ";
	constexpr DWORD console_getter = 0x55c890;              // void* __cdecl (): the console, 0x7ac290
	constexpr DWORD console_vtable = 0x69a81c;
	constexpr DWORD console_queue = 0x55c410;               // vt+0x1c
	constexpr DWORD console_waiting = 0x630;                // commands queued, 0..2
	constexpr DWORD console_slots = 2;

	using limits_rules::guard;

	// What "console" relies on; the getter and vt+0x1c are called.
	inline constexpr std::array<guard, 5> console_guards{{
		{0x55c890, "8a0dccca7a00b80100000084c875258b15ccca7a000bd0b990c27a008915ccca7a00e889feffff68d0e06700e85d58110083c404b890c27a00c3",
		 "the console's getter (0x55c890)"},
		{0x55c410, "8b44240481ec8000000085c0568bf17445803800744083be300600000274376880000000", "the console's queue (0x55c410)"},
		{0x69a81c, "30c2550020c3550070b65500e0b65500e0c5550020be5500b0be550010c45500", "the console's vtable (0x69a81c)"},
		{0x55c230, "81ec84000000a1f8386f00578bf9898424840000008b873006000085c0", "the console's queue runner (0x55c230)"},
		{0x402205, "e886a615008b10568bc8ff12", "the frame's call of the queue runner (0x402205)"},
	}};

	// And what "script" relies on besides: the runscript command and the word it reads.
	inline constexpr std::array<guard, 4> runscript_guards{{
		{0x5f4abf, "6850235f0068e0386a00", "runscript's registration (0x5f4abf)"},
		{0x6a38e0, "72756e73637269707400", "the name \"runscript\" (0x6a38e0)"},
		{0x5f2350, "81ec04020000a1f8386f0089842400020000e829a5f6ff8b8c24080200008b10518bc8ff5208", "runscript's handler (0x5f2350)"},
		{0x55b670, "56578b7c240c33f685ff8d813c060000c60000744b8b175385d2740b8a1a84db740580fb207e05803a3b750442ebe9908a1a80fb207e1480fb3b740f",
		 "the console's word reader (0x55b670)"},
	}};

	// A line for the console queue, or why there isn't one.
	struct queued_line
	{
		std::string text;
		std::string error;
	};

	// Newlines and the like: one command per line.
	inline std::optional<std::string> control_character(const std::string_view text)
	{
		for (const char c : text)
		{
			const auto byte = static_cast<unsigned char>(c);
			if ((byte < 0x20 && c != '\t') || byte == 0x7f)
			{
				char what[64];
				std::snprintf(what, sizeof(what), "a control character (0x%02X) in the line", byte);
				return std::string(what) + " - one command per line, no embedded newlines";
			}
		}
		return std::nullopt;
	}

	inline std::string too_long(const std::string_view what, const std::size_t length)
	{
		return std::string(what) + " is " + std::to_string(length) + " characters; the game's console keeps " + std::to_string(console_max);
	}

	// "script STATEMENT" -> "runscript STATEMENT", spaces outside quotes dropped.
	inline queued_line script_line(const std::string_view statement)
	{
		queued_line result;
		if (statement.empty())
		{
			result.error = "script needs a statement, e.g. script unlockCharacter(\"storm\", \"\")";
			return result;
		}
		if (const auto control = control_character(statement))
		{
			result.error = *control;
			return result;
		}
		std::string compact;
		char quote = 0;
		for (const char c : statement)
		{
			if (c == ' ' || c == '\t')
			{
				if (quote)
				{
					result.error = "a space inside a quoted string can't go through runscript: the game's console hands it the statement up to its first space";
					return result;
				}
				continue; // between tokens: the script tokenizer doesn't need it
			}
			if (c == ';')
			{
				result.error = "';' ends a command in the game's console: runscript would get the statement only up to it";
				return result;
			}
			if (static_cast<unsigned char>(c) >= 0x80)
			{
				result.error = "a non-ASCII character: the game's console ends runscript's word at it";
				return result;
			}
			if (c == '"' || c == '\'')
			{
				quote = !quote ? c : quote == c ? 0 : quote;
			}
			compact += c;
		}
		result.text = std::string(runscript_prefix) + compact;
		if (result.text.size() > console_max)
		{
			result.error = too_long("runscript with the statement", result.text.size());
			result.text.clear();
		}
		return result;
	}

	// "console COMMAND" -> COMMAND, as it is.
	inline queued_line console_line(const std::string_view command)
	{
		queued_line result;
		if (command.empty())
		{
			result.error = "console needs a command, e.g. console loadmap nyc/alison/nyc1_1_3 1";
		}
		else if (const auto control = control_character(command))
		{
			result.error = *control;
		}
		else if (command.size() > console_max)
		{
			result.error = too_long("the command", command.size());
		}
		else
		{
			result.text = std::string(command);
		}
		return result;
	}

	struct key_name
	{
		const char* name;
		unsigned char code;
	};

	// DIK_* from dinput.h without the prefix, then aliases. The first entry for a code names it in the log.
	inline constexpr key_name key_names[] = {
		{"ESCAPE", 0x01}, {"1", 0x02}, {"2", 0x03}, {"3", 0x04}, {"4", 0x05}, {"5", 0x06}, {"6", 0x07}, {"7", 0x08}, {"8", 0x09}, {"9", 0x0A},
		{"0", 0x0B}, {"MINUS", 0x0C}, {"EQUALS", 0x0D}, {"BACK", 0x0E}, {"TAB", 0x0F}, {"Q", 0x10}, {"W", 0x11}, {"E", 0x12}, {"R", 0x13},
		{"T", 0x14}, {"Y", 0x15}, {"U", 0x16}, {"I", 0x17}, {"O", 0x18}, {"P", 0x19}, {"LBRACKET", 0x1A}, {"RBRACKET", 0x1B}, {"RETURN", 0x1C},
		{"LCONTROL", 0x1D}, {"A", 0x1E}, {"S", 0x1F}, {"D", 0x20}, {"F", 0x21}, {"G", 0x22}, {"H", 0x23}, {"J", 0x24}, {"K", 0x25}, {"L", 0x26},
		{"SEMICOLON", 0x27}, {"APOSTROPHE", 0x28}, {"GRAVE", 0x29}, {"LSHIFT", 0x2A}, {"BACKSLASH", 0x2B}, {"Z", 0x2C}, {"X", 0x2D}, {"C", 0x2E},
		{"V", 0x2F}, {"B", 0x30}, {"N", 0x31}, {"M", 0x32}, {"COMMA", 0x33}, {"PERIOD", 0x34}, {"SLASH", 0x35}, {"RSHIFT", 0x36}, {"MULTIPLY", 0x37},
		{"LMENU", 0x38}, {"SPACE", 0x39}, {"CAPITAL", 0x3A}, {"F1", 0x3B}, {"F2", 0x3C}, {"F3", 0x3D}, {"F4", 0x3E}, {"F5", 0x3F}, {"F6", 0x40},
		{"F7", 0x41}, {"F8", 0x42}, {"F9", 0x43}, {"F10", 0x44}, {"NUMLOCK", 0x45}, {"SCROLL", 0x46}, {"NUMPAD7", 0x47}, {"NUMPAD8", 0x48},
		{"NUMPAD9", 0x49}, {"SUBTRACT", 0x4A}, {"NUMPAD4", 0x4B}, {"NUMPAD5", 0x4C}, {"NUMPAD6", 0x4D}, {"ADD", 0x4E}, {"NUMPAD1", 0x4F},
		{"NUMPAD2", 0x50}, {"NUMPAD3", 0x51}, {"NUMPAD0", 0x52}, {"DECIMAL", 0x53}, {"OEM_102", 0x56}, {"F11", 0x57}, {"F12", 0x58}, {"F13", 0x64},
		{"F14", 0x65}, {"F15", 0x66}, {"KANA", 0x70}, {"ABNT_C1", 0x73}, {"CONVERT", 0x79}, {"NOCONVERT", 0x7B}, {"YEN", 0x7D}, {"ABNT_C2", 0x7E},
		{"NUMPADEQUALS", 0x8D}, {"PREVTRACK", 0x90}, {"AT", 0x91}, {"COLON", 0x92}, {"UNDERLINE", 0x93}, {"KANJI", 0x94}, {"STOP", 0x95}, {"AX", 0x96},
		{"UNLABELED", 0x97}, {"NEXTTRACK", 0x99}, {"NUMPADENTER", 0x9C}, {"RCONTROL", 0x9D}, {"MUTE", 0xA0}, {"CALCULATOR", 0xA1}, {"PLAYPAUSE", 0xA2},
		{"MEDIASTOP", 0xA4}, {"VOLUMEDOWN", 0xAE}, {"VOLUMEUP", 0xB0}, {"WEBHOME", 0xB2}, {"NUMPADCOMMA", 0xB3}, {"DIVIDE", 0xB5}, {"SYSRQ", 0xB7},
		{"RMENU", 0xB8}, {"PAUSE", 0xC5}, {"HOME", 0xC7}, {"UP", 0xC8}, {"PRIOR", 0xC9}, {"LEFT", 0xCB}, {"RIGHT", 0xCD}, {"END", 0xCF}, {"DOWN", 0xD0},
		{"NEXT", 0xD1}, {"INSERT", 0xD2}, {"DELETE", 0xD3}, {"LWIN", 0xDB}, {"RWIN", 0xDC}, {"APPS", 0xDD}, {"POWER", 0xDE}, {"SLEEP", 0xDF},
		{"WAKE", 0xE3}, {"WEBSEARCH", 0xE5}, {"WEBFAVORITES", 0xE6}, {"WEBREFRESH", 0xE7}, {"WEBSTOP", 0xE8}, {"WEBFORWARD", 0xE9}, {"WEBBACK", 0xEA},
		{"MYCOMPUTER", 0xEB}, {"MAIL", 0xEC}, {"MEDIASELECT", 0xED},
		// dinput.h's aliases and everyday names
		{"ESC", 0x01}, {"ENTER", 0x1C}, {"BACKSPACE", 0x0E}, {"NUMPADSTAR", 0x37}, {"LALT", 0x38}, {"ALT", 0x38}, {"CAPSLOCK", 0x3A},
		{"NUMPADMINUS", 0x4A}, {"NUMPADPLUS", 0x4E}, {"NUMPADPERIOD", 0x53}, {"NUMPADSLASH", 0xB5}, {"RALT", 0xB8}, {"UPARROW", 0xC8},
		{"PGUP", 0xC9}, {"PAGEUP", 0xC9}, {"LEFTARROW", 0xCB}, {"RIGHTARROW", 0xCD}, {"DOWNARROW", 0xD0}, {"PGDN", 0xD1}, {"PAGEDOWN", 0xD1},
		{"CIRCUMFLEX", 0x90}, {"SHIFT", 0x2A}, {"CTRL", 0x1D}, {"CONTROL", 0x1D}, {"DEL", 0xD3}, {"INS", 0xD2}, {"CAPS", 0x3A},
	};

	inline std::string upper(const std::string_view text)
	{
		std::string result(text);
		std::ranges::transform(result, result.begin(), [](const char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
		return result;
	}

	// "ENTER", "DIK_RETURN", "return", "0x1C" or "28" -> the DirectInput scancode. Single digits are the
	// digit keys; a number of two or more digits (or with 0x) is a scancode.
	inline std::optional<unsigned char> parse_key(const std::string_view text)
	{
		if (text.empty())
		{
			return std::nullopt;
		}
		auto name = upper(text);
		if (name.starts_with("DIK_"))
		{
			name.erase(0, 4);
		}
		for (const auto& entry : key_names)
		{
			if (name == entry.name)
			{
				return entry.code;
			}
		}

		const bool hex = name.starts_with("0X");
		const std::string digits = hex ? name.substr(2) : name;
		if (digits.empty() || (!hex && digits.size() < 2) ||
		    !std::ranges::all_of(digits, [hex](const char c) { return hex ? std::isxdigit(static_cast<unsigned char>(c)) != 0 : std::isdigit(static_cast<unsigned char>(c)) != 0; }))
		{
			return std::nullopt;
		}
		const unsigned long value = std::strtoul(digits.c_str(), nullptr, hex ? 16 : 10);
		if (value == 0 || value > 0xFF)
		{
			return std::nullopt;
		}
		return static_cast<unsigned char>(value);
	}

	inline std::string name_of(const unsigned char code)
	{
		for (const auto& entry : key_names)
		{
			if (entry.code == code)
			{
				return entry.name;
			}
		}
		char text[8];
		std::snprintf(text, sizeof(text), "0x%02X", code);
		return text;
	}

	inline std::vector<std::string_view> split(std::string_view text, const char separator = ' ')
	{
		std::vector<std::string_view> parts;
		while (!text.empty())
		{
			const auto end = text.find(separator);
			const auto part = text.substr(0, end);
			if (!part.empty())
			{
				parts.push_back(part);
			}
			if (end == std::string_view::npos)
			{
				break;
			}
			text.remove_prefix(end + 1);
		}
		return parts;
	}

	inline std::string_view trim(std::string_view text)
	{
		while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
		{
			text.remove_prefix(1);
		}
		while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
		{
			text.remove_suffix(1);
		}
		return text;
	}

	// One line from the pipe:
	//   down KEYS [ms]      hold until "up" (or ms, or max_hold_ms)      KEYS: names or scancodes joined with '+'
	//   up KEYS
	//   tap KEYS [ms]       press and release (default_tap_ms)
	//   hold KEYS ms        press, keep for ms, release
	//   release             let go of everything
	//   screenshot PATH     save the current frame (.bmp or .png)
	//   script STATEMENT    queue "runscript STATEMENT" in the game's console (spaces outside quotes dropped)
	//   console COMMAND     queue COMMAND in the game's console, as it is
	//   status | ping
	// Every screen reads these keys, the Advanced Options panel included: its per-frame input
	// function (0x619070) turns released DirectInput keys (Esc, Enter, the arrows) and pad buttons
	// into the WM_KEYUP its widgets handle, while real key messages posted to the window are dropped
	// by its message filter (0x6223d0). So "tap DOWN" moves the panel's selection; the "wm" command
	// that posted key messages did nothing there and is gone (refused with a pointer to "tap").
	struct command
	{
		enum class kind
		{
			empty,
			down,
			up,
			tap,
			hold,
			release,
			screenshot,
			script,
			console,
			status,
			ping,
			unknown
		};

		kind what = kind::empty;
		std::vector<unsigned char> keys;
		DWORD ms = 0;      // 0: the command's default
		std::string path;  // screenshot
		std::string text;  // script, console: the line for the game's console queue
		std::string error; // unknown: what was wrong
	};

	inline command parse_command(const std::string_view line)
	{
		command result;
		const auto text = trim(line);
		if (text.empty())
		{
			return result;
		}

		const auto space = text.find(' ');
		const auto verb = upper(text.substr(0, space));
		const auto rest = space == std::string_view::npos ? std::string_view() : trim(text.substr(space + 1));
		const auto fail = [&](std::string why)
		{
			result.what = command::kind::unknown;
			result.error = std::move(why);
			return result;
		};

		if (verb == "PING") result.what = command::kind::ping;
		else if (verb == "STATUS") result.what = command::kind::status;
		else if (verb == "RELEASE") result.what = command::kind::release;
		else if (verb == "DOWN") result.what = command::kind::down;
		else if (verb == "UP") result.what = command::kind::up;
		else if (verb == "TAP") result.what = command::kind::tap;
		else if (verb == "HOLD") result.what = command::kind::hold;
		else if (verb == "SCREENSHOT") result.what = command::kind::screenshot;
		else if (verb == "SCRIPT") result.what = command::kind::script;
		else if (verb == "CONSOLE") result.what = command::kind::console;
		else if (verb == "WM") return fail("wm is gone: the Advanced Options panel reads the DirectInput keyboard like every other screen - use tap (tap DOWN, tap ENTER, tap LEFT)");
		else return fail("unknown command '" + std::string(text.substr(0, space)) + "' (down, up, tap, hold, release, screenshot, script, console, status, ping)");

		if (result.what == command::kind::script || result.what == command::kind::console)
		{
			auto queued = result.what == command::kind::script ? script_line(rest) : console_line(rest);
			if (!queued.error.empty())
			{
				return fail(std::move(queued.error));
			}
			result.text = std::move(queued.text);
			return result;
		}

		if (result.what == command::kind::screenshot)
		{
			auto path = rest;
			if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
			{
				path = path.substr(1, path.size() - 2);
			}
			if (path.empty())
			{
				return fail("screenshot needs a file path (.png or .bmp)");
			}
			result.path = std::string(path);
			return result;
		}

		if (result.what == command::kind::down || result.what == command::kind::up || result.what == command::kind::tap || result.what == command::kind::hold)
		{
			const auto words = split(rest);
			if (words.empty())
			{
				return fail(std::string(text.substr(0, space)) + " needs a key (ENTER, ESCAPE, W, UP, F1, NUMPAD4, 0x1C ...)");
			}
			if (result.what == command::kind::up && upper(words[0]) == "ALL")
			{
				result.what = command::kind::release;
				return result;
			}
			for (const auto part : split(words[0], '+'))
			{
				const auto code = parse_key(part);
				if (!code)
				{
					return fail("unknown key '" + std::string(part) + "'");
				}
				result.keys.push_back(*code);
			}
			if (words.size() > 1)
			{
				const auto ms = std::string(words[1]);
				if (!std::ranges::all_of(ms, [](const char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
				{
					return fail("'" + ms + "' isn't a number of milliseconds");
				}
				result.ms = static_cast<DWORD>(std::min<unsigned long>(std::strtoul(ms.c_str(), nullptr, 10), max_hold_ms));
			}
			if (result.what == command::kind::hold && result.ms == 0)
			{
				return fail("hold needs a duration in milliseconds");
			}
		}
		return result;
	}

	// The keys the pipe holds down, each until a tick count.
	class synthetic_keys
	{
	public:
		void press(const unsigned char code, const ULONGLONG until) { until_[code] = until ? until : 1; }
		void release(const unsigned char code) { until_[code] = 0; }
		void release_all() { until_.fill(0); }
		bool is_down(const unsigned char code) const { return until_[code] != 0; }

		int held() const
		{
			return static_cast<int>(std::ranges::count_if(until_, [](const ULONGLONG until) { return until != 0; }));
		}

		// OR-s the held keys into a 256-byte DirectInput keyboard state; keys whose time is up are
		// released instead and listed in `expired`.
		void merge(unsigned char* state, const ULONGLONG now, std::vector<unsigned char>& expired)
		{
			for (size_t code = 0; code < until_.size(); ++code)
			{
				if (!until_[code])
				{
					continue;
				}
				if (now >= until_[code])
				{
					until_[code] = 0;
					expired.push_back(static_cast<unsigned char>(code));
					continue;
				}
				state[code] |= key_down;
			}
		}

	private:
		std::array<ULONGLONG, 256> until_{};
	};
}
