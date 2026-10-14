#include "xtract.hpp"

#include "ini.hpp"
#include "log.hpp"
#include "xtract_rules.hpp"
#include "xtraction_shop_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"
#include "game_version.hpp"

#include <cstdint>
#include <cstring>

namespace xtract
{
	namespace
	{
		using namespace xtract_rules;

		constexpr const char* as_before = "an Xtraction Point's menu offers the game's Xtract choice";
		bool shop_installed = false;
		bool forge_everywhere = false;
		bool online_guarded = false;   // xtraction_shop_rules::net_guards matched at install
		bool skip_guarded = false;     // xtraction_shop_rules::change_team_guards matched at install
		bool xtract_installed = false; // the Xtract skip is in place, so Change Team can be skipped with it
		int point_services = 0;
		using getter_t = void*(__cdecl*)();
		using text_t = const char*(__fastcall*)(void*, void*, int);
		using option_t = void(__fastcall*)(void*, void*, const char*, const char*, const char*, int, int, int);
		using index_t = short(__fastcall*)(void*, void*, const char*);
		using bit_t = bool(__fastcall*)(void*, void*, int);

		// The registry's campaign bit for `name` (xtraction_shop_rules.hpp): false when it isn't a character.
		bool in_campaign(const char* name, int& index)
		{
			index = 0;
			void* registry = reinterpret_cast<getter_t>(xtraction_shop_rules::registry_getter)();
			if (!registry || *static_cast<DWORD*>(registry) != xtraction_shop_rules::registry_vtable) return false;
			const auto* vt = *static_cast<DWORD**>(registry);
			index = reinterpret_cast<index_t>(vt[xtraction_shop_rules::registry_index_slot / 4])(registry, nullptr, name);
			return index > 0 && reinterpret_cast<bit_t>(vt[xtraction_shop_rules::registry_campaign_slot / 4])(registry, nullptr, index);
		}

		// Online with other players (xtraction_shop_rules::online_shared); unguarded or unreadable: not shared.
		bool shared_online()
		{
			if (!online_guarded) return false;
			const auto net = reinterpret_cast<const std::uint8_t*>(reinterpret_cast<getter_t>(xtraction_shop_rules::net_getter)());
			const auto players = reinterpret_cast<const std::uint8_t*>(reinterpret_cast<getter_t>(xtraction_shop_rules::players_getter)());
			if (!net || !players) return false;
			int count = 0;
			std::memcpy(&count, players + xtraction_shop_rules::players_count_offset, sizeof(count));
			return xtraction_shop_rules::online_shared(net[xtraction_shop_rules::net_online_offset] != 0, count);
		}

		// The Xtract skip's rel8: Change Team or past it (a lite point without its first flag).
		bool write_skip(const std::uint8_t target)
		{
			auto* at = reinterpret_cast<std::uint8_t*>(choice_block + 1);
			if (*at == target) return true;
			DWORD protection = 0;
			if (!VirtualProtect(at, 1, PAGE_EXECUTE_READWRITE, &protection)) return false;
			*at = target;
			FlushInstructionCache(GetCurrentProcess(), at, 1);
			VirtualProtect(at, 1, protection, &protection);
			return *at == target;
		}

		void add_services(void* dialog)
		{
			const int services = xtraction_shop_rules::testing_services(point_services, forge_everywhere);
			__try
			{
				void* texts = reinterpret_cast<getter_t>(xtraction_shop_rules::text_getter)();
				if (!dialog || !texts || *static_cast<DWORD*>(texts) != xtraction_shop_rules::text_vtable) return;
				const auto lookup = reinterpret_cast<text_t>((*static_cast<DWORD**>(texts))[2]);
				const auto add = reinterpret_cast<option_t>((*static_cast<DWORD**>(dialog))[7]);
				const bool shared = shared_online();
				for (const auto& o : xtraction_shop_rules::options)
				{
					if ((services & o.service) == 0) continue;
					int index = 0;
					const bool member = xtraction_shop_rules::needs_campaign(services, o) && in_campaign(o.campaign, index);
					const auto how = xtraction_shop_rules::list_option(shop_installed, services, o, member, shared);
					if (how == xtraction_shop_rules::listing::omitted)
					{
						logger::write("xtraction services: %s not offered - '%s' isn't in the campaign (registry index %d)", o.script, o.campaign ? o.campaign : "?", index);
						continue;
					}
					const char* label = lookup(texts, nullptr, o.label);
					if (!label || !*label)
					{
						logger::write("xtraction services: string %d is missing - %s not offered", o.label, o.script);
						continue;
					}
					const bool greyed = how == xtraction_shop_rules::listing::greyed;
					add(dialog, nullptr, label, o.script, nullptr, o.closes, o.resumes, greyed ? 1 : 0);
					logger::write("xtraction services: %s added%s (point services 0x%X)", o.script,
					              greyed ? " greyed (online with other players: every peer keeps the same options)" : "", static_cast<unsigned>(point_services));
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				logger::write("xtraction services: native option call faulted - menu kept");
			}
		}

		void* __cdecl shop_dialog()
		{
			void* dialog = reinterpret_cast<getter_t>(xtraction_shop_rules::dialog_getter)();
			if (shop_installed && (point_services != 0 || forge_everywhere)) add_services(dialog);
			return dialog; // preserve the native getter's result for vt+0x68 (finalize)
		}

		void install_shop(const HMODULE game)
		{
			forge_everywhere = game_version::test_mode_active() && ini::flag(L"TestMode", L"ForgeEverywhere", false);
			if (!ini::flag(L"Game", L"XtractionShop", false) && !forge_everywhere) return;
			if (reinterpret_cast<std::uintptr_t>(game) != image_base) return;
			for (const auto& g : xtraction_shop_rules::guards)
			{
				if (!limits_rules::matches(reinterpret_cast<const std::uint8_t*>(g.va), g.hex))
				{
					logger::write("xtraction shop: guard mismatch at 0x%08lX - no option hook", g.va);
					return;
				}
			}
			online_guarded = true;
			for (const auto& g : xtraction_shop_rules::net_guards)
			{
				if (!limits_rules::matches(reinterpret_cast<const std::uint8_t*>(g.va), g.hex))
				{
					online_guarded = false;
					logger::write("xtraction shop: 0x%08lX isn't the retail code (%s) - no option is greyed online", g.va, g.what);
					break;
				}
			}
			skip_guarded = true;
			for (const auto& g : xtraction_shop_rules::change_team_guards)
			{
				if (!limits_rules::matches(reinterpret_cast<const std::uint8_t*>(g.va), g.hex))
				{
					skip_guarded = false;
					logger::write("xtraction shop: 0x%08lX isn't the retail code (%s) - a lite point's Change Team is never skipped", g.va, g.what);
					break;
				}
			}
			xml1_hook_transaction::memory memory;
			xml1_hook_transaction::patch patch{xtraction_shop_rules::finalize_call, std::vector<unsigned char>(5), {}};
			if (!memory.read(patch.address, patch.before.data(), patch.before.size())) return;
			patch.after = patch.before;
			const auto delta = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&shop_dialog) -
			                                            (xtraction_shop_rules::finalize_call + 5));
			std::memcpy(patch.after.data() + 1, &delta, sizeof(delta));
			bool safe = true;
			if (!xml1_hook_transaction::apply(memory, {patch}, &safe))
			{
				if (!safe) game_version::refuse_startup();
				logger::write("xtraction shop: option hook transaction failed; original code %s", safe ? "retained" : "not verified as restored");
				return;
			}
			shop_installed = true;
			logger::write("xtraction shop: guarded finalize getter wrapped ([Game] XtractionShop=1): Load, Danger Room, Healer and Forge as each point allows");
		}

		const limits_rules::guard* first_xtract_mismatch()
		{
			__try
			{
				return first_mismatch(reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(image_base)));
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return &guards[0];
			}
		}
	}

	void install(const HMODULE game)
	{
		install_shop(game);
		if (ini::flag(L"Game", L"Xtract", true))
		{
			return; // no key, or 1: the game's own menu
		}
		if (!game || reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("xtract: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", as_before);
			return;
		}
		if (const auto* g = first_xtract_mismatch())
		{
			logger::write("xtract: 0x%08lX isn't the retail code (%s) - %s", g->va, g->what, as_before);
			return;
		}
		auto* target = reinterpret_cast<void*>(static_cast<std::uintptr_t>(choice_block));
		DWORD old_protect = 0;
		if (!VirtualProtect(target, patched_jump.size(), PAGE_EXECUTE_READWRITE, &old_protect))
		{
			logger::write("xtract: ERROR: can't unprotect XMen2.exe's code at 0x%08lX (error %lu) - %s", static_cast<unsigned long>(choice_block), GetLastError(), as_before);
			return;
		}
		apply(reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(image_base)));
		VirtualProtect(target, patched_jump.size(), old_protect, &old_protect);
		xtract_installed = true;
		FlushInstructionCache(GetCurrentProcess(), target, patched_jump.size());
		logger::write("xtract: no Xtract choice ([Game] Xtract=0): an Xtraction Point's menu offers its title, Change Team and Save only "
		              "(extractionPoint's Xtract block at 0x%08lX jumps over the choice to 0x%08lX)",
		              static_cast<unsigned long>(choice_block), static_cast<unsigned long>(next_choice));
	}

	int set_services(const int services)
	{
		const int previous = point_services;
		point_services = services;
		// A lite point without its first flag has no Change Team (default.xbe 0x9f695). Only with the Xtract skip in
		// place (the byte it owns) and the Save add's guard matched; otherwise Change Team stays, with one log line.
		const bool skip = shop_installed && (services & xtraction_shop_rules::no_change_team) != 0;
		if (xtract_installed && skip_guarded)
		{
			if (!write_skip(skip ? xtraction_shop_rules::to_save : xtraction_shop_rules::to_change_team))
				logger::write("xtraction services: ERROR: can't write the Xtract skip at 0x%08lX (error %lu) - Change Team as before", choice_block + 1, GetLastError());
			else if (skip)
				logger::write("xtraction services: no Change Team (a lite point without its first flag)");
		}
		else if (skip)
		{
			logger::write("xtraction services: Change Team kept - the Xtract skip isn't installed ([Game] Xtract=0) or its guards differ");
		}
		return previous;
	}

	std::array<std::uint32_t, 4> capability(const bool revival_xml1)
	{
		return {shop_installed ? 1u : 0u, shop_installed ? static_cast<std::uint32_t>(xtraction_shop_rules::capability) : 0u,
		        shop_installed && online_guarded ? 1u : 0u, revival_xml1 ? 1u : 0u};
	}

	bool requested() { return ini::flag(L"Game", L"XtractionShop", false); }
	bool verified() { return !requested() || shop_installed; }
}
