#pragma once

#include <optional>
#include <string>

// xml2-fix.ini next to the DLL, read by the one rule in ini_rules.hpp (the launcher's too): a value
// ends at the first ';' and is trimmed; a missing or empty value is not set. Every key of every
// section goes through here.
namespace ini
{
	// [section] key: its value, or nullopt when it isn't set.
	std::optional<std::string> text(const wchar_t* section, const wchar_t* key);

	// [section] key as a number (as GetPrivateProfileInt reads one); `fallback` when it isn't set.
	int number(const wchar_t* section, const wchar_t* key, int fallback);

	// [section] key as a switch (1/0, true/false, yes/no, on/off); `fallback` when it isn't set or is
	// something else.
	bool flag(const wchar_t* section, const wchar_t* key, bool fallback);
}
