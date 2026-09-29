#pragma once

// The test pipe's pad commands, kept apart from the pipe and the devices so xml2_test can check
// them: the button names (Xbox names, as XInput masks), the one-line pad commands, and the
// synthetic pad state that is merged into the XInput state a pad's DirectInput state is built
// from - so the Logitech Dual Action profile (pad_profile.cpp) maps injected buttons exactly as
// it maps a real pad's: A is the Dual Action's button 2, START button 10, the d-pad its hat.
//
//   pad N BUTTONS [ms]          press and release (default_tap_ms)          BUTTONS: names joined with '+'
//   padhold N BUTTONS ms        press, keep for ms, release
//   paddown N BUTTONS [ms]      hold until padup (or ms, or max_hold_ms)
//   padup N BUTTONS|ALL         let go
//   padrelease [N]              let go of everything on pad N (or on every pad)
//   stick N L|R X Y [ms]        a stick at X,Y (-1..1, Y up, as XInput): with ms, held that long, then centred;
//                               without, held until the next stick command (0 0 lets go), padup ALL or max_hold_ms
//   trigger N L|R VALUE [ms]    a trigger at VALUE (0..1), the same way
//
// N is the pad, 1 to max_pads: the game's pad N ([Test] VirtualPads; with a real pad and no
// virtual ones, the N-th pad the fix presents). A held stick replaces that stick's real position,
// held buttons and triggers add to the real ones - as the pipe's keys add to the real keyboard.

#include "xinput_pad.hpp"

#include <Windows.h>
#include <Xinput.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pad_input_rules
{
	constexpr int max_pads = 4;                 // XInput's four, the game's four players
	constexpr DWORD default_tap_ms = 80;
	constexpr DWORD max_hold_ms = 10000;        // anything the pipe holds on a pad is let go after this whatever the client does

	// XInput's button bits, then the two triggers pressed all the way (the Dual Action's buttons 7 and 8).
	constexpr std::uint32_t left_trigger_bit = 1u << 16;
	constexpr std::uint32_t right_trigger_bit = 1u << 17;
	constexpr int bit_count = 18;

	struct button_name
	{
		const char* name;
		std::uint32_t mask;
	};

	// The first entry for a mask names it in the log.
	inline constexpr button_name button_names[] = {
		{"A", XINPUT_GAMEPAD_A}, {"B", XINPUT_GAMEPAD_B}, {"X", XINPUT_GAMEPAD_X}, {"Y", XINPUT_GAMEPAD_Y},
		{"LB", XINPUT_GAMEPAD_LEFT_SHOULDER}, {"RB", XINPUT_GAMEPAD_RIGHT_SHOULDER}, {"LT", left_trigger_bit}, {"RT", right_trigger_bit},
		{"BACK", XINPUT_GAMEPAD_BACK}, {"START", XINPUT_GAMEPAD_START}, {"LS", XINPUT_GAMEPAD_LEFT_THUMB}, {"RS", XINPUT_GAMEPAD_RIGHT_THUMB},
		{"UP", XINPUT_GAMEPAD_DPAD_UP}, {"DOWN", XINPUT_GAMEPAD_DPAD_DOWN}, {"LEFT", XINPUT_GAMEPAD_DPAD_LEFT}, {"RIGHT", XINPUT_GAMEPAD_DPAD_RIGHT},
		// everyday aliases
		{"SELECT", XINPUT_GAMEPAD_BACK}, {"L3", XINPUT_GAMEPAD_LEFT_THUMB}, {"R3", XINPUT_GAMEPAD_RIGHT_THUMB},
		{"DPADUP", XINPUT_GAMEPAD_DPAD_UP}, {"DPADDOWN", XINPUT_GAMEPAD_DPAD_DOWN}, {"DPADLEFT", XINPUT_GAMEPAD_DPAD_LEFT},
		{"DPADRIGHT", XINPUT_GAMEPAD_DPAD_RIGHT},
	};

	constexpr std::string_view button_list = "A B X Y LB RB LT RT BACK START LS RS UP DOWN LEFT RIGHT";

	inline std::string upper(const std::string_view text)
	{
		std::string result(text);
		std::ranges::transform(result, result.begin(), [](const char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
		return result;
	}

	inline std::optional<std::uint32_t> parse_button(const std::string_view text)
	{
		const auto name = upper(text);
		for (const auto& entry : button_names)
		{
			if (name == entry.name)
			{
				return entry.mask;
			}
		}
		return std::nullopt;
	}

	// "A+START" for a mask, in button_names' order.
	inline std::string buttons_text(const std::uint32_t mask)
	{
		std::string text;
		std::uint32_t named = 0;
		for (const auto& entry : button_names)
		{
			if ((mask & entry.mask) && !(named & entry.mask))
			{
				text += (text.empty() ? "" : "+") + std::string(entry.name);
				named |= entry.mask;
			}
		}
		return text.empty() ? "nothing" : text;
	}

	// ---- Commands ------------------------------------------------------------------------------------

	struct pad_command
	{
		enum class kind
		{
			none,    // not a pad verb
			tap,     // pad
			hold,    // padhold
			down,    // paddown
			up,      // padup
			release, // padrelease, padup N ALL
			stick,
			trigger,
		};

		kind what = kind::none;
		int pad = 0;               // 1..max_pads; 0: every pad (padrelease without one)
		std::uint32_t buttons = 0; // tap, hold, down, up
		int side = 0;              // stick, trigger: 0 left, 1 right
		float x = 0, y = 0;        // stick, -1..1, y up
		float value = 0;           // trigger, 0..1
		DWORD ms = 0;              // 0: the command's default (tap), until let go (down, stick, trigger)
		std::string error;         // set: the line was refused, and why
	};

	inline std::vector<std::string_view> words_of(std::string_view text)
	{
		std::vector<std::string_view> words;
		while (!text.empty())
		{
			const auto start = text.find_first_not_of(" \t");
			if (start == std::string_view::npos)
			{
				break;
			}
			text.remove_prefix(start);
			const auto end = text.find_first_of(" \t");
			words.push_back(text.substr(0, end));
			if (end == std::string_view::npos)
			{
				break;
			}
			text.remove_prefix(end);
		}
		return words;
	}

	// A number in [low, high]: "1", "-0.5", ".25", "1.0".
	inline std::optional<float> parse_number(const std::string_view text, const float low, const float high)
	{
		const std::string copy(text);
		if (copy.empty() || copy.size() > 16)
		{
			return std::nullopt;
		}
		char* end = nullptr;
		const double value = std::strtod(copy.c_str(), &end);
		if (end != copy.c_str() + copy.size() || !std::isfinite(value) || value < low || value > high)
		{
			return std::nullopt;
		}
		return static_cast<float>(value);
	}

	inline std::optional<DWORD> parse_ms(const std::string_view text)
	{
		if (text.empty() || text.size() > 9 || !std::ranges::all_of(text, [](const char c) { return c >= '0' && c <= '9'; }))
		{
			return std::nullopt;
		}
		return static_cast<DWORD>(std::min<unsigned long>(std::strtoul(std::string(text).c_str(), nullptr, 10), max_hold_ms));
	}

	// `verb`: the line's first word, any case; `rest`: the words after it. kind::none when the verb
	// isn't a pad verb (the line is someone else's).
	inline pad_command parse_pad_command(const std::string_view verb_text, const std::string_view rest)
	{
		pad_command result;
		const auto verb = upper(verb_text);
		using kind = pad_command::kind;
		if (verb == "PAD") result.what = kind::tap;
		else if (verb == "PADHOLD") result.what = kind::hold;
		else if (verb == "PADDOWN") result.what = kind::down;
		else if (verb == "PADUP") result.what = kind::up;
		else if (verb == "PADRELEASE") result.what = kind::release;
		else if (verb == "STICK") result.what = kind::stick;
		else if (verb == "TRIGGER") result.what = kind::trigger;
		else return result;

		const std::string name(verb_text);
		const auto fail = [&](std::string why)
		{
			result.error = std::move(why);
			return result;
		};
		const auto words = words_of(rest);
		const auto pad_number = [&](const std::string_view text) -> bool
		{
			if (text.size() == 1 && text[0] >= '1' && text[0] < '1' + max_pads)
			{
				result.pad = text[0] - '0';
				return true;
			}
			return false;
		};
		const std::string pads = "1 to " + std::to_string(max_pads);

		if (result.what == kind::release)
		{
			if (words.size() > 1 || (words.size() == 1 && !pad_number(words[0])))
			{
				return fail(name + " takes a pad number (" + pads + ") or nothing (every pad)");
			}
			return result;
		}

		if (words.empty() || !pad_number(words[0]))
		{
			return fail(name + " needs a pad number first, " + pads + (words.empty() ? "" : " - not '" + std::string(words[0]) + "'") + " (e.g. " +
			            (result.what == kind::stick     ? "stick 1 L 0 1 300"
			             : result.what == kind::trigger ? "trigger 1 R 1 300"
			                                            : name + " 1 A") +
			            ")");
		}

		if (result.what == kind::stick || result.what == kind::trigger)
		{
			const bool stick = result.what == kind::stick;
			const size_t numbers = stick ? 2 : 1;
			if (words.size() < 2 + numbers || words.size() > 3 + numbers)
			{
				return fail(stick ? "stick needs a pad, a side and a position: stick N L|R X Y [ms] (X, Y from -1 to 1, Y up)"
				                  : "trigger needs a pad, a side and a value: trigger N L|R VALUE [ms] (VALUE from 0 to 1)");
			}
			const auto side = upper(words[1]);
			if (side == "L" || side == "LEFT" || side == (stick ? "LS" : "LT"))
			{
				result.side = 0;
			}
			else if (side == "R" || side == "RIGHT" || side == (stick ? "RS" : "RT"))
			{
				result.side = 1;
			}
			else
			{
				return fail("'" + std::string(words[1]) + "' isn't a side: L or R");
			}
			if (stick)
			{
				const auto x = parse_number(words[2], -1.0f, 1.0f);
				const auto y = parse_number(words[3], -1.0f, 1.0f);
				if (!x || !y)
				{
					return fail("'" + std::string(!x ? words[2] : words[3]) + "' isn't a stick position from -1 to 1");
				}
				result.x = *x;
				result.y = *y;
			}
			else
			{
				const auto value = parse_number(words[2], 0.0f, 1.0f);
				if (!value)
				{
					return fail("'" + std::string(words[2]) + "' isn't a trigger value from 0 to 1");
				}
				result.value = *value;
			}
			if (words.size() == 3 + numbers)
			{
				const auto ms = parse_ms(words[2 + numbers]);
				if (!ms)
				{
					return fail("'" + std::string(words[2 + numbers]) + "' isn't a number of milliseconds");
				}
				result.ms = *ms;
			}
			return result;
		}

		if (words.size() < 2)
		{
			return fail(name + " needs buttons after the pad number (" + std::string(button_list) + ", joined with '+')");
		}
		if (result.what == kind::up && upper(words[1]) == "ALL")
		{
			if (words.size() > 2)
			{
				return fail("padup N ALL takes nothing after ALL");
			}
			result.what = kind::release;
			return result;
		}
		std::string_view list = words[1];
		while (!list.empty())
		{
			const auto plus = list.find('+');
			const auto part = list.substr(0, plus);
			const auto mask = parse_button(part);
			if (!mask)
			{
				return fail("unknown pad button '" + std::string(part) + "' (" + std::string(button_list) + ")");
			}
			result.buttons |= *mask;
			if (plus == std::string_view::npos)
			{
				break;
			}
			list.remove_prefix(plus + 1);
			if (list.empty())
			{
				return fail("a '+' with no button after it");
			}
		}
		if (result.what == kind::up)
		{
			if (words.size() > 2)
			{
				return fail("padup takes no duration");
			}
			return result;
		}
		if (words.size() > 3)
		{
			return fail(name + " takes a pad, buttons and at most a duration");
		}
		if (words.size() == 3)
		{
			const auto ms = parse_ms(words[2]);
			if (!ms)
			{
				return fail("'" + std::string(words[2]) + "' isn't a number of milliseconds");
			}
			result.ms = *ms;
		}
		if (result.what == kind::hold && result.ms == 0)
		{
			return fail("padhold needs a duration in milliseconds");
		}
		return result;
	}

	// ---- The state a pad's reads are merged with -------------------------------------------------------

	// -1..1 -> XInput's stick range (-32767..32767, the Dual Action profile reads /32767).
	inline std::int16_t stick_value(const float value)
	{
		return static_cast<std::int16_t>(std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f));
	}

	inline std::uint8_t trigger_value(const float value)
	{
		return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
	}

	// What the pipe holds on one pad, each part until a tick count.
	class synthetic_pad
	{
	public:
		void press(const std::uint32_t buttons, const ULONGLONG until)
		{
			for (int bit = 0; bit < bit_count; ++bit)
			{
				if (buttons & (1u << bit))
				{
					button_until_[bit] = until ? until : 1;
				}
			}
		}

		void release(const std::uint32_t buttons)
		{
			for (int bit = 0; bit < bit_count; ++bit)
			{
				if (buttons & (1u << bit))
				{
					button_until_[bit] = 0;
				}
			}
		}

		// 0, 0 lets go: the real stick counts again.
		void set_stick(const int side, const float x, const float y, const ULONGLONG until)
		{
			auto& stick = sticks_[side & 1];
			stick = {x, y, (x == 0 && y == 0) ? 0 : (until ? until : 1)};
		}

		// 0 lets go.
		void set_trigger(const int side, const float value, const ULONGLONG until)
		{
			auto& trigger = triggers_[side & 1];
			trigger = {value, value == 0 ? 0 : (until ? until : 1)};
		}

		void release_all()
		{
			button_until_.fill(0);
			sticks_[0] = sticks_[1] = {};
			triggers_[0] = triggers_[1] = {};
		}

		std::uint32_t buttons_down() const
		{
			std::uint32_t mask = 0;
			for (int bit = 0; bit < bit_count; ++bit)
			{
				mask |= button_until_[bit] ? (1u << bit) : 0;
			}
			return mask;
		}

		bool stick_held(const int side) const { return sticks_[side & 1].until != 0; }
		bool trigger_held(const int side) const { return triggers_[side & 1].until != 0; }

		// Buttons, sticks and triggers held.
		int held() const
		{
			int count = static_cast<int>(std::ranges::count_if(button_until_, [](const ULONGLONG until) { return until != 0; }));
			for (int side = 0; side < 2; ++side)
			{
				count += stick_held(side) + trigger_held(side);
			}
			return count;
		}

		// Held buttons OR-ed into `state`, a held trigger pulled to at least its value (LT/RT: all the way), a held
		// stick in place of the real one. Parts whose time is up are let go instead and named in `expired`.
		void merge(xinput_pad::raw_state& state, const ULONGLONG now, std::vector<std::string>& expired)
		{
			for (int side = 0; side < 2; ++side)
			{
				auto& stick = sticks_[side];
				if (stick.until && now >= stick.until)
				{
					stick = {};
					expired.push_back(side ? "right stick" : "left stick");
				}
				else if (stick.until)
				{
					(side ? state.right_x : state.left_x) = stick_value(stick.x);
					(side ? state.right_y : state.left_y) = stick_value(stick.y);
				}

				auto& trigger = triggers_[side];
				if (trigger.until && now >= trigger.until)
				{
					trigger = {};
					expired.push_back(side ? "right trigger" : "left trigger");
				}
				else if (trigger.until)
				{
					auto& real = side ? state.right_trigger : state.left_trigger;
					real = std::max(real, trigger_value(trigger.value));
				}
			}

			for (int bit = 0; bit < bit_count; ++bit)
			{
				auto& until = button_until_[bit];
				if (!until)
				{
					continue;
				}
				if (now >= until)
				{
					until = 0;
					expired.push_back(buttons_text(1u << bit));
					continue;
				}
				if (bit < 16)
				{
					state.buttons = static_cast<std::uint16_t>(state.buttons | (1u << bit));
				}
				else
				{
					(bit == 16 ? state.left_trigger : state.right_trigger) = 255;
				}
			}
		}

	private:
		struct axis_pair
		{
			float x = 0, y = 0;
			ULONGLONG until = 0;
		};
		struct trigger_hold
		{
			float value = 0;
			ULONGLONG until = 0;
		};

		std::array<ULONGLONG, bit_count> button_until_{};
		axis_pair sticks_[2]{};
		trigger_hold triggers_[2]{};
	};
}
