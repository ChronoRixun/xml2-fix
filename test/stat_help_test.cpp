// The stats screen's help text follows [Balance] (stat_help_rules.hpp) - the guard, the number formatting
// and the two rewrites, on made-up texts: no game data.

#include "stat_help_rules.hpp"

#include <cstdio>
#include <string>

int main()
{
	using namespace stat_help_rules;
	int failed = 0;
	auto check = [&](bool ok) { if (!ok) ++failed; };

	// the guard is the vtable slot's dword: get_string at 0x4bd720, little-endian
	check(guards[0].va == 0x68e998);
	check(guards[0].hex == "20d74b00");
	{
		// the bytes the guard wants, in a synthetic image laid out at the same base
		static std::uint8_t image[0x300000] = {}; // .data-sized synthetic XMen2.exe (2.7 MB in)
		auto* slot = image + (vtable_getter_slot - image_base);
		slot[0] = 0x20; slot[1] = 0xd7; slot[2] = 0x4b; slot[3] = 0x00;
		check(first_mismatch(image) == nullptr);
		slot[1] = 0xd8; // some other build's getter
		check(first_mismatch(image) == &guards[0]);
	}

	// numbers the way the ini writes them
	check(number(2.5f) == "2.5");
	check(number(10.0f) == "10");
	check(number(0.5f) == "0.5");
	check(number(0.025f * 100.0f) == "2.5");
	check(number(3.0f) == "3");

	// Body: the retail literal's number is replaced; anything else is left alone
	const std::string body = "Increases your health points ($HP) and chance for double health from potions.\\n\\n"
	                         "~02Health~~\\n25 + 5 per Level + 3 per Body\\n\\n~02Double Heal Chance :~~ ^%d%%\\n";
	const auto fixed = body_help_text(body, 2.5f);
	check(fixed.has_value());
	check(fixed->find("25 + 5 per Level + 2.5 per Body") != std::string::npos);
	check(fixed->find("3 per Body") == std::string::npos);
	check(body_help_text(body, 10.0f)->find("10 per Body") != std::string::npos);
	check(!body_help_text("nothing health-ish here", 2.5f).has_value());
	check(!body_help_text(" per Body with no number before it", 2.5f).has_value());
	check(!body_help_text("", 2.5f).has_value());
	// the number is the run right before " per Body", not anything earlier in the line
	const std::string tricky = "25 + 5 per Level + 3 per Body";
	check(body_help_text(tricky, 2.5f)->rfind("Level + 2.5 per Body") != std::string::npos);

	// Strike: "+2%% per Striking" -> the configured percent, the %% snprintf escape kept
	const std::string strike = "~02Damage Bonus~~\\n+2%% per Striking\\n\\n~02DMG~~\\nRegular Attack, Heavy Attack";
	const auto fixed_strike = strike_help_text(strike, 0.025f);
	check(fixed_strike.has_value());
	check(fixed_strike->find("+2.5%% per Striking") != std::string::npos);
	check(fixed_strike->find("+2%%") == std::string::npos);
	check(strike_help_text(strike, 0.1f)->find("+10%% per Striking") != std::string::npos);
	check(!strike_help_text("+2% per Striking (one percent sign)", 0.025f).has_value());
	check(!strike_help_text("", 0.025f).has_value());

	std::printf("stat help rules: %d failures\n", failed);
	return failed ? 1 : 0;
}
