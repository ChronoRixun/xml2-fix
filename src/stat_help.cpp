#include "stat_help.hpp"

#include "balance.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "stat_help_rules.hpp"

#include <cstdint>
#include <string>

namespace stat_help
{
	namespace
	{
		using namespace stat_help_rules;

		// The game's get_string(this, id) is __thiscall with the one stack argument the caller pushes
		// (balance_rules.hpp's shape): read through __fastcall's dummy edx, its ret 4 cleans the pushed id.
		using retail_t = const char*(__fastcall*)(void*, void*, int);

		retail_t retail_getter = nullptr;
		bool want_body = false;
		bool want_strike = false;
		float body_value = 0.0f;
		float strike_value = 0.0f;
		std::string body_text;
		std::string strike_text;
		balance::native_stat_help native_rates;
		std::string native_names;

		__declspec(noinline) const char* __fastcall get_string_impl(void* self, int id)
		{
			const char* const original = retail_getter(self, nullptr, id);
			if (!original)
			{
				return original;
			}
			if (id == body_help_id && want_body && body_text.empty())
			{
				if (const auto replaced = body_help_text(original, body_value, native_rates.health_per_body, native_names))
				{
					body_text = *replaced;
				}
				else
				{
					want_body = false; // the fetched text is not the retail literal: never touch it
				}
			}
			else if (id == strike_help_id && want_strike && strike_text.empty())
			{
				if (const auto replaced = strike_help_text(original, strike_value, native_rates.strength_per_point, native_names))
				{
					strike_text = *replaced;
				}
				else
				{
					want_strike = false;
				}
			}
			if (id == body_help_id && !body_text.empty())
			{
				return body_text.c_str();
			}
			if (id == strike_help_id && !strike_text.empty())
			{
				return strike_text.c_str();
			}
			return original;
		}

		// The vtable slot's shape: __thiscall (this in ecx, the id the caller pushed; the game's own
		// getter answers with a ret 4). Hand the id to the fastcall impl in edx, then pop the return
		// address and the pushed id and put the return address back - eax (the text) survives, and ecx
		// and edx are scratch at every call site.
		__declspec(naked) void get_string_entry()
		{
			__asm {
				mov edx, [esp + 4]
				call get_string_impl
				pop ecx
				pop edx
				push ecx
				ret
			}
		}

		std::uint8_t* game_image(const HMODULE game)
		{
			return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(reinterpret_cast<std::size_t>(game)));
		}

		const guard* mismatch_guarded(const std::uint8_t* image)
		{
			__try { return first_mismatch(image); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return &guards[0]; }
		}
	}

	void install(const HMODULE game)
	{
		if (!ini::flag(L"Game", L"StatHelp", true))
		{
			logger::write("stat help: off ([Game] StatHelp=0); the stats screen keeps the game's own texts");
			return;
		}
		want_body = balance::hero_health_per_body(&body_value);
		want_strike = balance::hero_strength_per_point(&strike_value);
		native_rates = balance::native_stat_help_values();
		native_names = native_hero_labels(native_rates.heroes);
		if (!want_body && !want_strike)
		{
			logger::write("stat help: no applied [Balance] override for the scaled heroes - the stats screen keeps the game's own texts");
			return;
		}
		if (!game || reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("stat help: unsupported image base; the stats screen keeps the game's own texts");
			return;
		}
		std::uint8_t* image = game_image(game);
		if (const auto* g = mismatch_guarded(image))
		{
			logger::write("stat help: byte mismatch at 0x%08lX (%s); the stats screen keeps the game's own texts", g->va, g->what);
			return;
		}
		retail_getter = reinterpret_cast<retail_t>(image + (retail_getter_va - image_base));
		void* slot = image + (vtable_getter_slot - image_base);
		DWORD before = 0;
		if (!VirtualProtect(slot, sizeof(slot), PAGE_EXECUTE_READWRITE, &before))
		{
			logger::write("stat help: cannot protect the strings manager's vtable (error %lu); the stats screen keeps the game's own texts", GetLastError());
			return;
		}
		*static_cast<void**>(slot) = &get_string_entry;
		FlushInstructionCache(GetCurrentProcess(), slot, sizeof(slot));
		DWORD ignored = 0;
		if (!VirtualProtect(slot, sizeof(slot), before, &ignored))
		{
			logger::write("stat help: could not restore page protection (error %lu)", GetLastError());
		}
		if (want_body)
		{
			logger::write("stat help: on; the stats screen's Body help states %.9g health per body point", static_cast<double>(body_value));
		}
		if (want_strike)
		{
			logger::write("stat help: on; the stats screen's Strike help states %.9g%% per strike point", static_cast<double>(strike_value * 100.0f));
		}
	}
}
