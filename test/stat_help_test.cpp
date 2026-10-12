// The stats screen's help text follows [Balance] (stat_help_rules.hpp) - the guard, the number formatting
// and the two rewrites, on made-up texts: no game data.

#include "stat_help_rules.hpp"

#include <Windows.h>

#include <cstdarg>
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

	// [Game] DoubleHeal=0: the Body and Focus helps lose their double-heal lines (made-up text around the matched
	// fragments; "\\n" is the UI's two-character escape).
	const std::string invented_body = "Raises invented points ($HP) and chance for double health from potions.\\n\\n~02Invented~~\\n7 per Body\\n\\n~02Double Heal Chance :~~ ^%d%%\\n\\n~02After~~";
	const auto plain_body = body_help_without_double_heal(invented_body);
	check(plain_body.has_value() && *plain_body == "Raises invented points ($HP).\\n\\n~02Invented~~\\n7 per Body\\n\\n~02After~~");
	check(!body_help_without_double_heal("no such phrase\\n\\n~02Double Heal Chance :~~ ^%d%%").has_value());
	check(!body_help_without_double_heal(*plain_body).has_value()); // already plain: untouched
	// Both edits compose in either order (the fix applies [Balance]'s first).
	check(body_help_text(*plain_body, 2.5f)->find("2.5 per Body") != std::string::npos);
	check(body_help_without_double_heal(*body_help_text(invented_body, 2.5f))->find("Double Heal") == std::string::npos);
	const std::string invented_focus = "Raises invented points ($EP), energy regeneration, Melee $DMG_MENTAL, and chance for double energy from potions.\\n\\n~02Regen :~~ +^%d%%\\n~02Double Energy Chance :~~ ^%d%%\\n~02Bonus :~~ +^%d%%";
	const auto plain_focus = focus_help_without_double_energy(invented_focus);
	check(plain_focus.has_value() && *plain_focus == "Raises invented points ($EP), energy regeneration and Melee $DMG_MENTAL.\\n\\n~02Regen :~~ +^%d%%%.0s\\n~02Bonus :~~ +^%d%%");
	check(!focus_help_without_double_energy("no such phrase").has_value());
	// The screen's three arguments still land where they belong: regeneration, (the chance, taken and not printed),
	// damage bonus. The game formats with msvcr71's _vsnprintf: checked there too when the installed game has it.
	std::snprintf(rendered, sizeof(rendered), plain_focus->c_str(), 12, 34, 56);
	check(std::string(rendered) == "Raises invented points ($EP), energy regeneration and Melee $DMG_MENTAL.\\n\\n~02Regen :~~ +^12%\\n~02Bonus :~~ +^56%");
	if (HMODULE crt = LoadLibraryW(L"D:\\Games\\X-Men Legends II\\msvcr71.dll"))
	{
		using vsn_t = int(__cdecl*)(char*, size_t, const char*, va_list);
		if (auto vsn = reinterpret_cast<vsn_t>(GetProcAddress(crt, "_vsnprintf")))
		{
			auto game_format = [&](const char* format, ...) { va_list a; va_start(a, format); const int n = vsn(rendered, sizeof(rendered), format, a); va_end(a); return n; };
			check(game_format(plain_focus->c_str(), 12, 34, 56) > 0 && std::string(rendered).find("+^12%\\n~02Bonus :~~ +^56%") != std::string::npos);
			std::printf("stat help rules: msvcr71 _vsnprintf checked\n");
		}
		FreeLibrary(crt);
	}

	std::printf("stat help rules: %d failures\n", failed);
	return failed ? 1 : 0;
}
