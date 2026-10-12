#include "script_functions.hpp"

#include "forced_teams_rules.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "script_function_overflow.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace script_functions
{
	namespace
	{
		using namespace script_functions_rules;

		int installed_capacity = stock_capacity;
		int chosen_capacity = stock_capacity;
		bool key_present = false, fix_functions_registered = false;

		// [Test] ScriptFunctionProbes: n(i) functions that only log their argument.
		constexpr std::size_t probe_max = 64;
		char probe_names[probe_max][24]{};
		func_entry probe_table[probe_max]{};
		std::size_t probe_count = 0;
		constexpr const char probe_ret[] = "n";
		constexpr const char probe_args[] = "i";

		void* __cdecl probe_handler(void* args)
		{
			int value = 0;
			if (forced_teams_rules::read_int_argument(reinterpret_cast<forced_teams_rules::get_argument_t>(static_cast<std::uintptr_t>(forced_teams_rules::argument_getter)), args, 0, value))
				logger::write("script functions: probe called with %d (overflow names %zu of %zu)", value, script_function_overflow::table().count(), script_function_overflow::table().capacity());
			else
				logger::write("script functions: probe called, its argument unreadable");
			return nullptr;
		}

		void choose_probes()
		{
			const int wanted = ini::number(L"Test", L"ScriptFunctionProbes", 0);
			if (wanted <= 0) return;
			probe_count = static_cast<std::size_t>(wanted) < probe_max ? static_cast<std::size_t>(wanted) : probe_max;
			for (std::size_t i = 0; i < probe_count; ++i)
			{
				std::snprintf(probe_names[i], sizeof probe_names[i], "xml2fixProbe%zu", i + 1);
				probe_table[i] = {reinterpret_cast<const void*>(&probe_handler), probe_names[i], probe_ret, probe_args};
			}
			logger::write("script functions: [Test] ScriptFunctionProbes=%d - xml2fixProbe1..%zu n(i) go behind the fix's own functions", wanted, probe_count);
		}

		// The script system's registered names so far (0 before the game builds it).
		DWORD registered_names()
		{
			__try
			{
				const DWORD system = *reinterpret_cast<const DWORD*>(static_cast<std::uintptr_t>(forced_teams_rules::script_system_pointer));
				return system ? *reinterpret_cast<const DWORD*>(static_cast<std::uintptr_t>(system + count_offset)) : 0;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return 0xffffffff;
			}
		}

		void registered(const func_entry* entry, const added result)
		{
			const char* name = entry && entry->name ? entry->name : "(no name)";
			const auto& table = script_function_overflow::table();
			switch (result)
			{
			case added::added:
				logger::write("script functions: %s registered past the game's 320 (overflow name %zu of %zu)", name, table.count(), table.capacity());
				break;
			case added::duplicate:
				logger::write("script functions: %s registered twice - the first registration kept", name);
				break;
			case added::no_name:
				break;
			case added::full:
				logger::write("script functions: ERROR: %s dropped - all %d names are taken ([Limits] ScriptFunctions)", name, installed_capacity);
				break;
			}
		}

		bool guards_match()
		{
			xml1_hook_transaction::memory memory;
			for (const auto& g : guards)
			{
				std::vector<unsigned char> bytes(g.hex.size() / 2);
				if (!memory.read(g.va, bytes.data(), bytes.size())) return false;
				for (std::size_t i = 0; i < bytes.size(); ++i)
				{
					const auto byte = static_cast<unsigned char>(std::stoul(std::string(g.hex.substr(i * 2, 2)), nullptr, 16));
					if (bytes[i] != byte)
					{
						logger::write("script functions: 0x%08X isn't the retail code (%s) - the game's 320 names stay", g.va, g.what);
						return false;
					}
				}
			}
			return true;
		}
	}

	int capacity()
	{
		return installed_capacity;
	}

	bool requested()
	{
		return chosen_capacity > stock_capacity;
	}

	bool requested_by_key()
	{
		return key_present && chosen_capacity > stock_capacity;
	}

	agreement capability()
	{
		return agreement_entry(key_present, fix_functions_registered, chosen_capacity, installed_capacity);
	}

	std::span<const func_entry> probes()
	{
		return {probe_table, probe_count};
	}

	void install(const HMODULE game)
	{
		choose_probes();
		// The fix's own functions are registered with [Game] ForcedTeams (forced_teams.hpp); with them the game's 320 are full.
		const bool fix_functions = forced_teams_rules::decide(ini::text(L"Game", L"ForcedTeams"), ini::text(L"Game", L"AddHero"), ini::text(L"Game", L"JoinHero")).registered;
		const auto key = ini::text(L"Limits", L"ScriptFunctions");
		const auto chosen = decide(key, fix_functions);
		if (!chosen.note.empty()) logger::write("script functions: %s", chosen.note.c_str());
		key_present = key.has_value();
		fix_functions_registered = fix_functions;
		chosen_capacity = chosen.capacity;
		if (chosen.capacity == stock_capacity)
		{
			logger::write("script functions: the game's own 320 names (%s)", fix_functions ? "[Limits] ScriptFunctions=320" : "no [Limits] ScriptFunctions and no [Game] ForcedTeams");
			return;
		}
		if (!game || reinterpret_cast<std::uintptr_t>(game) != 0x400000)
		{
			logger::write("script functions: XMen2.exe isn't loaded at 0x400000 (not the game?) - the game's 320 names stay");
			return;
		}
		if (!guards_match()) return;
		if (const DWORD names = registered_names(); names > forced_teams_rules::builtin_count)
		{
			logger::write("script functions: the game has registered its script functions already (%lu names) - the game's 320 names stay", names);
			return;
		}
		script_function_overflow::table().reset(static_cast<std::size_t>(chosen.capacity - stock_capacity));
		const auto reg = register_after(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&script_function_overflow::register_stub)));
		const auto look = lookup_after(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&script_function_overflow::lookup_stub)));
		const std::vector<xml1_hook_transaction::patch> writes{
			{register_site, {register_before.begin(), register_before.end()}, {reg.begin(), reg.end()}},
			{lookup_site, {lookup_before.begin(), lookup_before.end()}, {look.begin(), look.end()}},
		};
		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (xml1_hook_transaction::apply(memory, writes, &safe))
		{
			installed_capacity = chosen.capacity;
			script_function_overflow::set_notify(&registered);
			logger::write("script functions: %d names (the game's tree keeps its 320 and their indices; names past it go to the fix's table of %d: registration 0x%08X, lookup 0x%08X)",
			              chosen.capacity, chosen.capacity - stock_capacity, register_site, lookup_site);
		}
		else if (safe)
		{
			script_function_overflow::table().reset(0);
			logger::write("script functions: the patch transaction was refused (nothing written) - the game's 320 names stay");
		}
		else
		{
			script_function_overflow::table().reset(0);
			game_version::refuse_startup();
			logger::write("script functions: rollback/protection verification failed; process startup refused");
		}
	}
}
