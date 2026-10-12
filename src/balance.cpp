#include "balance.hpp"

#include "balance_rules.hpp"
#include "ini.hpp"
#include "log.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace balance
{
	namespace
	{
		using namespace balance_rules;

		unsigned applied = 0;

		template <typename T>
		T game_function(const DWORD va)
		{
			return reinterpret_cast<T>(static_cast<std::uintptr_t>(va));
		}

		// 0x4b7780 (__thiscall, ret 0x10): the stat `index` of a CStats with `actor`'s affecters; the game reads ax.
		int __cdecl game_stat(void* stats, const int index, void* actor)
		{
			using stat_fn = int(__fastcall*)(void* stats, void* edx, int index, int flag, void* actor, int* out);
			return static_cast<std::int16_t>(game_function<stat_fn>(0x4b7780)(stats, nullptr, index, 1, actor, nullptr));
		}

		// 0x4b87c0 (__thiscall): a CStats' level; the game reads ax.
		int __cdecl game_level(void* stats)
		{
			using level_fn = int(__fastcall*)(void* stats);
			return static_cast<std::int16_t>(game_function<level_fn>(0x4b87c0)(stats));
		}

		// A talent's name by its id: the registry (0x4c05b0), its vt+0x14 with a pointer to the id (0x4bdbe0), then the
		// `name` attribute of the data node the talent keeps at +8, through the game's attribute lookup (0x5649c0).
		const char* __cdecl game_talent_name(int id)
		{
			using registry_fn = void*(__cdecl*)();
			using find_fn = void*(__fastcall*)(void* registry, void* edx, int* id);
			using attribute_fn = const char*(__fastcall*)(void* node, void* edx, const char* name);
			void* registry = game_function<registry_fn>(0x4c05b0)();
			if (!registry)
			{
				return nullptr;
			}
			void* talent = (*reinterpret_cast<find_fn**>(registry))[5](registry, nullptr, &id);
			if (!talent)
			{
				return nullptr;
			}
			void* node = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(talent) + 8);
			if (!node)
			{
				return nullptr;
			}
			return game_function<attribute_fn>(0x5649c0)(node, nullptr, "name");
		}

		std::uint8_t* game_image()
		{
			return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(image_base));
		}

		template <typename T>
		DWORD address_of(T pointer)
		{
			return static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(pointer));
		}

		const guard* first_mismatch_guarded(const unsigned feature)
		{
			__try
			{
				return first_mismatch(game_image(), feature);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return &guards[0].bytes;
			}
		}

		// Every write at once: the pages they are on are made writable first, all of them, so a refusal leaves the
		// code as it was; then every write; then the pages' protection goes back, in reverse order (see xp_curve.cpp).
		bool patch(const std::vector<byte_write>& writes)
		{
			SYSTEM_INFO system{};
			GetSystemInfo(&system);
			const std::uintptr_t page_size = system.dwPageSize;
			std::vector<std::uintptr_t> pages;
			for (const auto& w : writes)
			{
				const auto first = reinterpret_cast<std::uintptr_t>(game_image() + (w.va - image_base));
				pages.push_back(first & ~(page_size - 1));
				pages.push_back((first + w.bytes.size() - 1) & ~(page_size - 1));
			}
			std::ranges::sort(pages);
			pages.erase(std::unique(pages.begin(), pages.end()), pages.end());

			std::vector<DWORD> protection(pages.size());
			for (std::size_t i = 0; i < pages.size(); ++i)
			{
				if (!VirtualProtect(reinterpret_cast<void*>(pages[i]), page_size, PAGE_EXECUTE_READWRITE, &protection[i]))
				{
					logger::write("balance: ERROR: can't unprotect XMen2.exe's code at 0x%08lX (error %lu) - nothing changes", static_cast<unsigned long>(pages[i]), GetLastError());
					for (std::size_t j = i; j-- > 0;)
					{
						VirtualProtect(reinterpret_cast<void*>(pages[j]), page_size, protection[j], &protection[j]);
					}
					return false;
				}
			}
			apply(game_image(), writes);
			for (const auto page : pages)
			{
				FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(page), page_size);
			}
			for (std::size_t i = pages.size(); i-- > 0;)
			{
				VirtualProtect(reinterpret_cast<void*>(pages[i]), page_size, protection[i], &protection[i]);
			}
			return true;
		}

		// The two stubs in a block of their own that may run; the health stub first, the hit stub after it.
		bool build_stubs(addresses& mine)
		{
			auto code = health_stub(address_of(&health_per_body_for));
			const std::size_t hit_at = code.size();
			const auto hit = hit_stub(address_of(&hit_chance_for), hit_resume);
			code.insert(code.end(), hit.begin(), hit.end());
			const std::size_t melee_at = code.size();
			const auto melee = melee_stub(address_of(&melee_weapon_for));
			code.insert(code.end(), melee.begin(), melee.end());
			auto* block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, code.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
			if (!block)
			{
				return false;
			}
			std::memcpy(block, code.data(), code.size());
			DWORD before = 0;
			if (!VirtualProtect(block, code.size(), PAGE_EXECUTE_READ, &before))
			{
				VirtualFree(block, 0, MEM_RELEASE);
				return false;
			}
			FlushInstructionCache(GetCurrentProcess(), block, code.size());
			mine.health_stub = address_of(block);
			mine.hit_stub = address_of(block + hit_at);
			mine.melee_stub = address_of(block + melee_at);
			return true;
		}

		std::string value(const wchar_t* key)
		{
			return ini::text(L"Balance", key).value_or("");
		}
	}

	void install(const HMODULE game)
	{
		ini_values ini;
		ini.enemy_health_per_body = value(L"EnemyHealthPerBody");
		ini.hero_health_per_body = value(L"HeroHealthPerBody");
		ini.hero_stat_scale = value(L"HeroStatScale");
		ini.native_stat_heroes = value(L"NativeStatHeroes");
		ini.strength_bonus = value(L"StrengthBonus");
		ini.mind_bonus = value(L"MindBonus");
		ini.hero_strength_bonus = value(L"HeroStrengthBonus");
		ini.hero_mind_bonus = value(L"HeroMindBonus");
		ini.might_bonus = value(L"MightBonus");
		ini.power_attacks = value(L"PowerAttacks");
		ini.hit_chance = value(L"HitChance");
		ini.hit_chance_base = value(L"HitChanceBase");
		ini.hit_chance_per_point = value(L"HitChancePerPoint");
		ini.rounding = value(L"Rounding");
		ini.damage_rolls = value(L"DamageRolls");
		ini.melee_weapons = value(L"MeleeWeapons");
		const settings chosen = parse(ini);
		for (const auto& line : chosen.refused)
		{
			logger::write("balance: %s", line.c_str());
		}
		unsigned wanted = features_of(chosen);
		if (!wanted)
		{
			return; // no key: the game's own numbers
		}
		if (!game || reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("balance: XMen2.exe isn't loaded at 0x400000 (not the game?) - the game's own combat numbers stay");
			return;
		}
		for (const unsigned feature : {health, strength, mind, might, power_attacks, hit_chance, damage_rolls, melee_weapons})
		{
			if (!(wanted & feature))
			{
				continue;
			}
			if (const guard* g = first_mismatch_guarded(feature))
			{
				logger::write("balance: 0x%08lX isn't the retail code (%s) - %s", g->va, g->what, feature_stays(feature));
				wanted &= ~feature;
			}
		}
		if (!wanted)
		{
			return;
		}

		addresses mine;
		if (!build_stubs(mine))
		{
			logger::write("balance: ERROR: no memory for the stubs (error %lu) - the game's own combat numbers stay", GetLastError());
			return;
		}
		mine.strength_multiplier = address_of(&strength_multiplier);
		mine.mind_multiplier = address_of(&mind_multiplier);
		mine.mind_multiplier_shown = address_of(&mind_multiplier_shown);
		mine.damage_multiplier = address_of(&damage_multiplier);
		mine.damage_scale = address_of(&damage_scale);
		mine.damage_roll = address_of(&damage_roll);

		live.chosen = chosen;
		live.hero_vtable = hero_extension_vtable;
		live.strength = game_function<multiplier_t>(0x4b7940);
		live.mind = game_function<multiplier_t>(0x4b7970);
		live.affecter = game_function<affecter_t>(0x550410);
		live.parser = game_function<parser_t>(0x44ec00);
		live.stat = &game_stat;
		live.level = &game_level;
		live.talent_name = &game_talent_name;
		live.integer_roll = game_function<integer_roll_t>(0x5601e0);
		live.float_roll = game_function<float_roll_t>(0x5601b0);
		live.health[0] = chosen.health_per_body(false);
		live.health[1] = chosen.health_per_body(true);

		const auto writes = writes_for(wanted, mine);
		if (!patch(writes))
		{
			return;
		}
		applied = wanted;

		std::string natives;
		for (const auto& name : chosen.native_stat_heroes)
		{
			natives += (natives.empty() ? "" : ", ") + name;
		}
		logger::write("balance: [Balance] - %zu sites patched; scaled heroes are the herostat characters (stats x %s)%s%s", writes.size(), number_text(chosen.hero_stat_scale).c_str(),
		              natives.empty() ? "" : " except ", natives.c_str());
		if (applied & health)
		{
			logger::write("balance: health %s per body point (the game's own: 3), scaled heroes %s per stored point", number_text(chosen.health_per_body(false)).c_str(),
			              number_text(chosen.health_per_body(true)).c_str());
		}
		if (applied & strength)
		{
			logger::write("balance: strength bonus %s per point (the game's own: 0.05), scaled heroes %s per stored point", number_text(chosen.strength_per_point(false)).c_str(),
			              number_text(chosen.strength_per_point(true)).c_str());
		}
		if (applied & mind)
		{
			logger::write("balance: mind bonus %s per point on mental attacks (the game's own: 0.01), scaled heroes %s per stored point; the stats screen shows it",
			              number_text(chosen.mind_per_point(false)).c_str(), number_text(chosen.mind_per_point(true)).c_str());
		}
		if ((applied & might) && chosen.might_bonus)
		{
			logger::write("balance: the talent might multiplies every hit by 1 + %s per rank, ranks above %d as %d (the game's own: nothing)", number_text(*chosen.might_bonus).c_str(),
			              might_max_rank, might_max_rank);
		}
		if (chosen.rounding_xml1 && (applied & (strength | mind | might)))
		{
			logger::write("balance: X-Men Legends 1's rounding ([Balance] Rounding=xml1): a hit is its roll + the stat bonus on the roll + the damage affecters' on the roll, "
			              "each rounded down (the game's own: each rounded up, the affecters' on the hit after its stat bonus)");
		}
		if (applied & power_attacks)
		{
			logger::write("balance: X-Men Legends 1's power attacks ([Balance] PowerAttacks=xml1): DamageScale=\"%s\" reads as none - no stat bonus, never a miss", power_attack_name);
		}
		if (applied & damage_rolls)
		{
			logger::write("balance: inclusive integer damage draws ([Balance] DamageRolls=xml1), before stat and Might bonuses; hit-chance draws stay float");
		}
		if (applied & melee_weapons)
		{
			logger::write("balance: %zu melee weapon profiles, fixed bonus before stat and Might on matching attack types only", chosen.melee_weapons.size());
		}
		if (applied & hit_chance)
		{
			logger::write("balance: X-Men Legends 1's melee hit chance ([Balance] HitChance=xml1): (%s + speed and level of the attacker - the defender's) x %s, at least 10 %%; "
			              "equal sides hit %s %% of the time; scaled heroes' speed / %s",
			              number_text(chosen.hit_chance_base).c_str(), number_text(chosen.hit_chance_per_point).c_str(),
			              number_text(100.0f * xml1_hit_chance(0.0f, 0.0f, chosen.hit_chance_base, chosen.hit_chance_per_point)).c_str(), number_text(chosen.hero_stat_scale).c_str());
		}
	}

	std::string status()
	{
		return status_text(live.chosen, applied);
	}

	bool hero_health_per_body(float* out)
	{
		if (!(applied & health) || (!live.chosen.hero_health_per_body && live.chosen.native_stat_heroes.empty()))
		{
			return false;
		}
		*out = live.chosen.health_per_body(true);
		return true;
	}

	bool hero_strength_per_point(float* out)
	{
		if (!(applied & strength) || !(live.chosen.strength_bonus || live.chosen.hero_strength_bonus))
		{
			return false;
		}
		*out = live.chosen.strength_per_point(true);
		return true;
	}

	native_stat_help native_stat_help_values()
	{
		native_stat_help out;
		if (!(applied & (health | strength)) || live.chosen.native_stat_heroes.empty()) return out;
		out.heroes = live.chosen.native_stat_heroes;
		if (applied & health) out.health_per_body = live.chosen.health_per_body(false);
		if (applied & strength) out.strength_per_point = live.chosen.strength_per_point(false);
		return out;
	}
}
