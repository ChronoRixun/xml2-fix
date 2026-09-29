#pragma once

// How the fix reads xml2-fix.ini: one rule for every key of every section, the same one the Ultimate
// Legends launcher uses when it shows and edits the file. Kept apart from the fix's own file (ini.hpp)
// so xml2_test can check it on files of its own.
//
// The Windows profile API (GetPrivateProfileString) hands over everything after the '=' with the
// spaces around it trimmed, an inline comment included: "Mode=borderless   ; the desktop's size" comes
// back as "borderless   ; the desktop's size". So:
//
//   - A value is the text after '=' up to the first ';', the Windows INI comment character, with the
//     spaces and tabs around it trimmed: "Enabled=0   ; off for now" is "0". A '#' is part of the
//     value: a name or a path may hold one.
//   - A key that is missing, or whose value is empty once the comment is cut ("ShowZone=", "LocalIP=
//     ; later"), is not set: the key's default, exactly as if the line weren't there.
//   - A value with a ';' of its own can't be written (none of the fix's values needs one).
//
// Numbers (Width, Height) follow the same rule and then read as GetPrivateProfileInt reads them: an
// optional sign and decimal digits (or 0x and hex digits), up to the first other character, 0 when
// there are none - "Width=2560   ; my screen" is 2560, "Width=   ; later" the default. Switches
// (Topmost, RunInBackground, LogNetwork, NewGamePlus, Enabled, ...) take 1/0, true/false, yes/no or
// on/off in any case, as the launcher does; anything else is the key's default, as the launcher shows it.

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ini_rules
{
	inline bool is_blank(const char c)
	{
		return c == ' ' || c == '\t' || c == '\r' || c == '\n';
	}

	inline std::string_view trimmed(std::string_view text)
	{
		while (!text.empty() && is_blank(text.front())) text.remove_prefix(1);
		while (!text.empty() && is_blank(text.back())) text.remove_suffix(1);
		return text;
	}

	// The value part of what the profile API hands over: up to the first ';', trimmed.
	inline std::string_view value_text(std::string_view raw)
	{
		if (const auto comment = raw.find(';'); comment != std::string_view::npos)
		{
			raw = raw.substr(0, comment);
		}
		return trimmed(raw);
	}

	// A key's value, or nullopt when the key is missing (`raw` nullopt) or its value is empty.
	inline std::optional<std::string> value(const std::optional<std::string_view> raw)
	{
		if (!raw)
		{
			return std::nullopt;
		}
		const auto text = value_text(*raw);
		if (text.empty())
		{
			return std::nullopt;
		}
		return std::string(text);
	}

	// A number as GetPrivateProfileInt reads one, `fallback` when the key isn't set.
	inline int number(const std::optional<std::string_view> raw, const int fallback)
	{
		const auto text = value(raw);
		if (!text)
		{
			return fallback;
		}
		std::string_view digits = *text;
		bool negative = false;
		if (digits.front() == '+' || digits.front() == '-')
		{
			negative = digits.front() == '-';
			digits.remove_prefix(1);
		}
		unsigned base = 10;
		if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
		{
			base = 16;
			digits.remove_prefix(2);
		}
		std::uint32_t result = 0;
		for (const char c : digits)
		{
			unsigned digit = 0;
			if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
			else if (base == 16 && c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
			else if (base == 16 && c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
			else break;
			result = result * base + digit;
		}
		return static_cast<int>(negative ? 0u - result : result);
	}

	// A switch (Topmost, RunInBackground, InputPipe, NewGamePlus, [Discord] Enabled, ...): 1/0, true/false,
	// yes/no or on/off in any case, as the launcher reads and writes them; nullopt when the key isn't set
	// or says something else (the key's default then, as the launcher shows it).
	inline std::optional<bool> flag(const std::optional<std::string_view> raw)
	{
		const auto text = value(raw);
		if (!text)
		{
			return std::nullopt;
		}
		std::string lower(*text);
		for (auto& c : lower)
		{
			if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
		}
		if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") return true;
		if (lower == "0" || lower == "false" || lower == "no" || lower == "off") return false;
		return std::nullopt;
	}

	// The profile API's UTF-16 as the fix's parsers take it: ASCII as it is, anything else as 0x7f, a
	// byte every value's rules refuse (no key of the fix takes a non-ASCII value).
	inline std::string narrow(const std::wstring_view wide)
	{
		std::string text;
		text.reserve(wide.size());
		for (const wchar_t c : wide)
		{
			text += c < 0x80 ? static_cast<char>(c) : '\x7f';
		}
		return text;
	}

	// What the profile API hands over for [section] key of `file`, comment and all; nullopt when the
	// key (or the file) is missing. Values longer than the buffer come back cut, and every key's rules
	// refuse one that long anyway.
	inline std::optional<std::string> raw(const std::filesystem::path& file, const wchar_t* section, const wchar_t* key)
	{
		constexpr wchar_t absent[] = L"\x01"; // no ini holds this, so it tells a missing key from an empty one
		std::vector<wchar_t> buffer(1024);
		const DWORD length = GetPrivateProfileStringW(section, key, absent, buffer.data(), static_cast<DWORD>(buffer.size()), file.c_str());
		const std::wstring_view text(buffer.data(), length);
		if (text == absent)
		{
			return std::nullopt;
		}
		return narrow(text);
	}

	// [section] key of `file` by the rule above.
	inline std::optional<std::string> read(const std::filesystem::path& file, const wchar_t* section, const wchar_t* key)
	{
		const auto text = raw(file, section, key);
		return value(text ? std::optional<std::string_view>(*text) : std::nullopt);
	}

	inline int read_number(const std::filesystem::path& file, const wchar_t* section, const wchar_t* key, const int fallback)
	{
		const auto text = raw(file, section, key);
		return number(text ? std::optional<std::string_view>(*text) : std::nullopt, fallback);
	}

	inline bool read_flag(const std::filesystem::path& file, const wchar_t* section, const wchar_t* key, const bool fallback)
	{
		const auto text = raw(file, section, key);
		return flag(text ? std::optional<std::string_view>(*text) : std::nullopt).value_or(fallback);
	}
}
