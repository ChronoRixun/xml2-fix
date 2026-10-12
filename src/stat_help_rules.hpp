#pragma once

// The stats screen's help text follows [Balance] (the first game's combat numbers, balance_rules.hpp).
// The four stat helps live in Data/strings (ids 0x442..0x445); the Body one states the engine's health
// per body point ("25 + 5 per Level + 3 per Body", the float at 0x6d9770) and the Strike one its damage
// bonus per striking ("+2% per Striking") - two numbers [Balance] replaces for the scaled heroes
// (HeroHealthPerBody, StrengthBonus over HeroStatScale). The texts ship in the game's data file; the
// values can change after a build (xml2-fix.ini is the player's), so the DLL rewrites the fetched text
// at runtime from the same parsed values its combat patches apply, and only when the corresponding patch
// is actually in. Everything here is checked without the game: the addresses, the guard, the rewrite.
//
// How the text travels: XMen2.exe's string manager (global 0x76d878; 0x4bdb30 returns it, writing the
// vtable 0x68e990 at 0x4bdb56) answers get_string(this, id) at vt+8 (0x4bd720: the id's tree lookup
// 0x4bd810, the record's c_str 0x425bc0). The stats screen's own code (0x5e0b32..0x5e0c27) fetches every
// help through it and snprintf's the result into a 512-byte buffer, so one write to the vtable slot
// reaches every display of the two helps; every other id passes to the game's function untouched. The
// screen's own dynamic numbers (the ^%d%% values, 0x4b78c0/0x4b7900/0x4b7970) already read the patched
// multipliers - only these two literals can lie.

#include "limits_rules.hpp" // guard, matches, image_base

#include <array>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stat_help_rules
{
	using limits_rules::guard;
	using limits_rules::image_base;

	// The strings manager's vt+8 slot (.rdata) and the retail get_string it must hold.
	constexpr DWORD vtable_getter_slot = 0x68e998;
	constexpr DWORD retail_getter_va = 0x4bd720;

	inline constexpr std::array<guard, 1> guards{{
		{vtable_getter_slot, "20d74b00", "the strings manager's vt+8: get_string(this, id)"},
	}};

	// The first guard whose bytes `image` (XMen2.exe mapped at its base) doesn't have; nullptr when it matches.
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

	// The two helps whose literals name a [Balance] number (Data/strings ids; the stats screen's only
	// fetches of them: one push each in its own code, 0x5e0b32..0x5e0c27).
	constexpr int strike_help_id = 0x442; // 1090: "~02Damage Bonus~~\n+2%% per Striking\n..."
	constexpr int body_help_id = 0x444;   // 1092: "...~02Health~~\n25 + 5 per Level + 3 per Body\n..."

	// A number the way the ini writes one: 2.5, 10, 0.5 (no trailing zeros; %g already drops them).
	inline std::string number(float value)
	{
		char buffer[32] = {};
		std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
		return buffer;
	}

	// Replaces the run of digits (and one dot) ending at `end` with `replacement`; nullopt when there is
	// no such run - the fetched text is not the retail literal, so nothing may change.
	inline std::optional<std::string> replace_trailing_number(std::string_view text, std::size_t end,
	                                                          const std::string& replacement)
	{
		std::size_t begin = end;
		while (begin > 0 && (text[begin - 1] >= '0' && text[begin - 1] <= '9' || text[begin - 1] == '.'))
		{
			--begin;
		}
		if (begin == end)
		{
			return std::nullopt;
		}
		std::string out(text.substr(0, begin));
		out += replacement;
		out += text.substr(end);
		return out;
	}

	inline std::string native_hero_labels(const std::vector<std::string>& names)
	{
		std::string out;
		for (const auto& name : names)
		{
			if (!out.empty()) out += ", ";
			out += name == "profxastral" ? "Astral Professor X" :
			       name == "profxgladiator" ? "Gladiator Professor X" : name;
		}
		return out;
	}

	// The fetched text is a snprintf format: configured names are literal display data.
	inline std::string escaped_label(std::string_view names)
	{
		std::string out;
		for (char c : names)
		{
			out += c;
			if (c == '%') out += '%';
		}
		return out;
	}

	// "...25 + 5 per Level + 3 per Body..." with the applied health per stored body point.
	inline std::optional<std::string> body_help_text(std::string_view original, float health_per_body,
	                                                std::optional<float> native_rate = std::nullopt,
	                                                std::string_view native_names = {})
	{
		const std::size_t at = original.find(" per Body");
		if (at == std::string_view::npos)
		{
			return std::nullopt;
		}
		auto out = replace_trailing_number(original, at, number(health_per_body));
		if (out && native_rate && *native_rate != health_per_body && !native_names.empty())
		{
			out->insert(out->find(" per Body") + std::string_view(" per Body").size(),
			            " (" + escaped_label(native_names) + ": " + number(*native_rate) + " per Body)");
		}
		return out;
	}

	// "...~02Damage Bonus~~\n+2%% per Striking\n..." with the configured bonus per stored strike point,
	// in percent (0.025 -> "2.5"); the %% is the snprintf escape the fetched text carries.
	inline std::optional<std::string> strike_help_text(std::string_view original, float bonus_per_point,
	                                                  std::optional<float> native_rate = std::nullopt,
	                                                  std::string_view native_names = {})
	{
		const std::size_t at = original.find("%% per Striking");
		if (at == std::string_view::npos)
		{
			return std::nullopt;
		}
		auto out = replace_trailing_number(original, at, number(bonus_per_point * 100.0f));
		if (out && native_rate && *native_rate != bonus_per_point && !native_names.empty())
		{
			out->insert(out->find("%% per Striking") + std::string_view("%% per Striking").size(),
			            " (" + escaped_label(native_names) + ": +" + number(*native_rate * 100.0f) + "%% per Striking)");
		}
		return out;
	}
}
