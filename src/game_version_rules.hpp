#pragma once

// [Online] GameVersion: the version the game gives GameSpy (OpenSpy) and other players, for a mod that
// is another game on the same GameSpy name - the X-Men Legends 1 port and X-Men Legends II are both
// "xmenlegpc", so without it their hosted games show up in each other's lists and a join loads the
// host's zones on the other game's install. Kept apart from the patch (game_version.cpp) so xml2_test
// can check it against a copy of XMen2.exe.
//
// The research is in the xml1-port repository, research/online/port_online_test.md ("Keeping port
// games and XML2 games apart"); every address below was read again from the retail XMen2.exe for this
// code (image base 0x400000, no relocations) and is in `guards`.
//
// What the game does: its version is the string "1.30" at 0x6a49b0 (.rdata; four NULs follow, then
// "NoName" at 0x6a49b8). A hosted game's QR2 key callback hands it to the heartbeat as "gamever"
// (push 0x6a49b0 at 0x604fba -> qr2_buffer_add 0x63c0a0), and four places copy exactly its first five
// bytes - four characters and the NUL - into the network code's own structures: the join request
// (0x607cff), 0x6083a4, 0x612270 (a loop of five) and the session defaults (0x6144a0). Those copies are
// made once, at start-up and when online opens, so a later change doesn't reach them: the string is
// rewritten from DllMain, before any of the game's code runs. In game (the online report) a client
// whose version differs from the host's finds no games - in the server list, on the LAN or by
// "Connect by IP" - so the engine itself keeps the two games apart once their versions differ.
//
// A version is one to four characters (the copies hold four and the NUL), letters, digits, '.', '-'
// and '_' - GameSpy's key/value strings are separated by '\', so none of that, and no spaces.

#include "limits_rules.hpp" // guard, matches, image_base

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace game_version_rules
{
	using limits_rules::guard;
	using limits_rules::image_base;

	constexpr DWORD version_va = 0x6a49b0;
	constexpr std::string_view retail_version = "1.30";
	constexpr std::size_t version_max = 4;   // what the copies take before the NUL
	constexpr std::size_t patched_bytes = 5; // the version and its NUL: all the copies read

	// ---- The ini value --------------------------------------------------------------------------------

	// The version, or why the value isn't one. Both empty: the key isn't set (the game's own 1.30).
	struct version_choice
	{
		std::string version;
		std::string error;
	};

	inline bool version_character(const char c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
	}

	// `value`: [Online] GameVersion as the fix's ini rule has it (up to a ';', trimmed; nullopt when not set).
	inline version_choice parse_version(const std::optional<std::string_view> value)
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
		version_choice refused;
		for (const char c : text)
		{
			if (!version_character(c))
			{
				refused.error = "has a character other than letters, digits, '.', '-' and '_'";
				return refused;
			}
		}
		if (text.size() > version_max)
		{
			refused.error = "is " + std::to_string(text.size()) + " characters - the game keeps " + std::to_string(version_max) + " (its own is 1.30)";
			return refused;
		}
		return {std::string(text), {}};
	}

	// ---- The patch ------------------------------------------------------------------------------------

	// Every byte of XMen2.exe the change relies on, read from the retail build: the string and the room
	// after it, and every place that reads it (each takes at most five bytes). All must match before
	// anything is written; xml2_test compares them with a copy of the exe.
	inline constexpr std::array<guard, 6> guards{{
		{0x6a49b0, "312e333000000000", "the version \"1.30\" and the NULs up to \"NoName\" (0x6a49b0)"},
		{0x604fb6, "8b4c241068b0496a0051e8db700300", "the QR2 key callback hands it to the heartbeat as gamever (qr2_buffer_add 0x63c0a0, 0x604fb6)"},
		{0x607cff, "a0b0496a0088842488000000a1b1496a008884248900000088a4248a00000066a1b3496a008884248b00000088a4248c000000",
		 "the join request copies its five bytes (0x607cff)"},
		{0x6083a4, "8a0db4496a00a1b0496a0033ed884c2460", "a copy of its five bytes (0x6083a4)"},
		{0x612270, "8a88b0496a00888c06100400004083f8057ced", "a copy of its five bytes, a loop of five (0x612270)"},
		{0x6144a0, "8a0db0496a008848288a15b1496a008850298a0db2496a0088482a8a15b3496a0088502b8a0db4496a006860486a005088482c",
		 "the session defaults copy its five bytes (0x6144a0)"},
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

	// The five bytes written at version_va: the version, NULs after it.
	inline std::array<std::uint8_t, patched_bytes> bytes_for(const std::string_view version)
	{
		std::array<std::uint8_t, patched_bytes> bytes{};
		std::memcpy(bytes.data(), version.data(), version.size() < version_max ? version.size() : version_max);
		return bytes;
	}

	inline void apply(std::uint8_t* image, const std::string_view version)
	{
		const auto bytes = bytes_for(version);
		std::memcpy(image + (version_va - image_base), bytes.data(), bytes.size());
	}
}
