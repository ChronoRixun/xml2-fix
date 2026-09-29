#pragma once

// [Game] WindowTitle: the game window's title (and so its taskbar button's and alt-tab's name), for a
// mod that is another game - the X-Men Legends 1 port calls its window "X-Men Legends". Kept apart
// from the hooks (window_title.cpp) so xml2_test can check it without the game.
//
// What the game does: XMen2.exe opens its window through the engine (igWindow vt+0x5c at 0x5faf43,
// with the string 0x6a3a70, "X-Men Legends 2"; "X-Men Legends 2 Demo" in the demo build), and
// libIGDisplay.dll's igWin32Window creates it with CreateWindowExA (0x10005894, class
// "igWin32WindowClass") and retitles it with SetWindowTextA (its setTitle, 0x10005a62). The same
// string is also the game's registry root and the start of its save and screenshot folders
// (Activision\X-Men Legends 2), so the string itself is left alone: the two calls in libIGDisplay's
// import table are hooked instead, and for the engine's window only the title they pass is replaced.

#include "ini_rules.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace window_title_rules
{
	constexpr std::string_view engine_class = "igWin32WindowClass"; // igWin32Window's window class
	constexpr std::size_t title_max = 127;

	// The title, or why the value isn't one. Both empty: the key isn't set (the game's own title).
	struct title_choice
	{
		std::string title;
		std::string error;
	};

	// `value`: [Game] WindowTitle as the fix's ini rule has it (up to a ';', trimmed; nullopt when not
	// set). Printable ASCII (the window is an ANSI one), at most title_max characters.
	inline title_choice parse_title(const std::optional<std::string_view> value)
	{
		if (!value)
		{
			return {};
		}
		const auto text = ini_rules::value_text(*value);
		if (text.empty())
		{
			return {};
		}
		title_choice refused;
		for (const char c : text)
		{
			const auto byte = static_cast<unsigned char>(c);
			if (byte < 0x20 || byte > 0x7e)
			{
				refused.error = "has a character that isn't printable ASCII (letters, digits, spaces and punctuation)";
				return refused;
			}
		}
		if (text.size() > title_max)
		{
			refused.error = "is " + std::to_string(text.size()) + " characters - at most " + std::to_string(title_max);
			return refused;
		}
		return {std::string(text), {}};
	}

	// Whether a window of class `class_name` is the engine's (a class name, not an atom).
	inline bool is_engine_class(const std::string_view class_name)
	{
		return class_name == engine_class;
	}
}
