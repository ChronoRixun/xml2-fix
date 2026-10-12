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

	// Native-stat heroes keep their original attributes: their effective rate must not be
	// described by the scaled heroes' rate. Preserve the standard formula and its dynamic suffix.
	const std::string exceptions = "Astral Professor X, Gladiator Professor X";
	check(native_hero_labels({"profxastral", "profxgladiator"}) == exceptions);
	check(native_hero_labels({"invented_hero"}) == "invented_hero");
	const auto qualified_body = body_help_text(body, 2.5f, 10.0f, exceptions);
	check(qualified_body && qualified_body->find("2.5 per Body (Astral Professor X, Gladiator Professor X: 10 per Body)") != std::string::npos);
	check(qualified_body && qualified_body->find("^%d%%") != std::string::npos);
	const auto qualified_strike = strike_help_text(strike, 0.025f, 0.1f, exceptions);
	check(qualified_strike && qualified_strike->find("+2.5%% per Striking (Astral Professor X, Gladiator Professor X: +10%% per Striking)") != std::string::npos);
	// Equal rates and absent/refused native rates do not add an exception claim.
	check(body_help_text(body, 2.5f, 2.5f, exceptions) == fixed);
	check(body_help_text(body, 2.5f, std::nullopt, exceptions) == fixed);
	check(strike_help_text(strike, 0.025f, 0.025f, exceptions) == fixed_strike);
	check(strike_help_text(strike, 0.025f, std::nullopt, exceptions) == fixed_strike);
	check(body_help_text(body, 2.5f, 10.0f, "") == fixed);
	check(!body_help_text("unsupported wording", 2.5f, 10.0f, exceptions));
	check(!strike_help_text("unsupported wording", 0.025f, 0.1f, exceptions));
	// Configured names are display data, never snprintf directives.
	check(body_help_text(body, 2.5f, 10.0f, "invented%hero")->find("invented%%hero: 10 per Body") != std::string::npos);
	char rendered[512] = {};
	std::snprintf(rendered, sizeof(rendered), qualified_body->c_str(), 42);
	check(std::string(rendered).find("^42%") != std::string::npos);
	check(std::string(rendered).find("Astral Professor X, Gladiator Professor X: 10 per Body") != std::string::npos);
	std::snprintf(rendered, sizeof(rendered), qualified_strike->c_str());
	check(std::string(rendered).find("+2.5% per Striking") != std::string::npos);
	check(std::string(rendered).find("+10% per Striking") != std::string::npos);

	std::printf("stat help rules: %d failures\n", failed);
	return failed ? 1 : 0;
}
