#pragma once

// Forced parties for a mod's own campaign (the X-Men Legends 1 port: Magma alone in the mansion,
// the flashbacks with their fixed heroes and costumes), kept apart from the patching so xml2_test
// can check it without the game: the eight script functions and their signatures, the function
// table the game's registration is pointed at, every retail byte they rely on, [Game] ForcedTeams
// and AddHero, the hero-name and costume rules, and what each function does, written over an
// "engine" - the game's own calls in the DLL (forced_teams.cpp), fakes and the game's own code on
// blocks of the test's in xml2_test.
//
// The research is in the xml1-port repository, research/heroes/FORCED_TEAMS_DESIGN.md (sections 1
// and 2), script-functions.md and party-seating.md; every address below was read again from the
// retail XMen2.exe for this code (image base 0x400000, no relocations) and is in `guards`.
//
// How the game registers its script functions: once, at start-up (0x40197b -> game vt+0x13c
// 0x46b750 -> script interface vt+0 = 0x49fe30), 0x49fe30 pushes the table 0x68a908 and its count
// 0x121 and calls 0x4d75a0 on the script system, which puts a pointer to each 16-byte entry
// {handler, name, return signature, argument signature} into a tree of at most 320 names keyed by
// the name, exact case (the 19 builtins, 0x6903d8, are in it already). Scripts look names up when
// they compile (0x4d8970, from the first menu on) and check the argument count and the 's'
// arguments' type there: an unknown name or a mismatched call drops that one statement, nothing
// else. The tree keeps pointers to the entries, not copies, so the table must outlive the game.
//
// A handler is `void* __cdecl (void* args)`: argument i is args->get(i) (0x4d5830, __thiscall,
// NULL past the count), its text vt+0x14 and its number vt+0x10; it returns NULL for an 'n'
// function, otherwise a value from the script system's pool (0x4d8770() -> 0x4d6570 for an int,
// 0x4d9430 for a string), as the game's own handlers do (getGameFlag 0x4a5db0, getName 0x4a1fb0).

#include "limits_rules.hpp" // guard, matches, value_text

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace forced_teams_rules
{
	using limits_rules::guard;

	// ---- The functions ------------------------------------------------------------------------------

	enum class function : std::uint8_t
	{
		xml2fix_feature,  // i(s): 1 if the named feature is on - scripts choose the seat branch or the team menu with it
		seat_party,       // n(ssss): seat exactly these heroes (empty strings = empty slots); the script's next statement loads
		set_skinset,      // n(ss): XML1's mission skinset - the costume for the listed heroes, default for the rest
		push_party,       // n(a): XML1 beginSideMission's first half - zone, party and the entity's spot on the side-mission stack
		pop_party,        // n(s): XML1 endSideMission - back to the pushed zone, party and spot, or the team menu at the zone given
		add_hero,         // i(s): XML1 addHero - seat a hero mid-zone through the game's own dormant 0x46c9f0 ([Game] AddHero)
		get_party_member, // s(i): slot i's hero, "" when empty
		join_hero,        // i(s): XML1 addHero as a reload - the spot saved, the hero added to the saved party, the zone reloaded there
	};

	struct function_spec
	{
		const char* name; // exact case: the game's tree is case-sensitive
		const char* ret;  // 'n' no value, 'i' int, 's' string (the parser types the assigned variable by it)
		const char* args; // one letter per argument: 's' must be a string, 'i' a number, 'a' anything (an entity name)
	};

	// None of these names is in XMen2.exe (xml2_test walks both of its tables). The exe has no "ssss",
	// so the DLL owns its signature strings.
	inline constexpr std::array<function_spec, 8> functions{{
		{"xml2fixFeature", "i", "s"},
		{"seatParty", "n", "ssss"},
		{"setSkinset", "n", "ss"},
		{"pushParty", "n", "a"},
		{"popParty", "n", "s"},
		{"addHero", "i", "s"},
		{"getPartyMember", "s", "i"},
		{"joinHero", "i", "s"},
	}};

	// ---- The table the registration is pointed at ----------------------------------------------------

	struct func_entry
	{
		const void* handler;
		const char* name;
		const char* ret;
		const char* args;
	};
	static_assert(sizeof(func_entry) == 16, "the game's entries are 16 bytes (a Win32 build)");

	constexpr DWORD retail_table = 0x68a908;  // .rdata, 289 entries (setRotZ .. SetDontShowWarningOff)
	constexpr DWORD retail_count = 0x121;
	constexpr DWORD builtin_table = 0x6903d8; // the operators and waittimed/iadd/strcatstr..., registered by the script system's constructor
	constexpr DWORD builtin_count = 0x13;
	constexpr DWORD tree_capacity = 0x140;    // 0x4d7637: a registration past 320 names is skipped without a word
	constexpr DWORD table_count = retail_count + static_cast<DWORD>(functions.size());
	static_assert(builtin_count + table_count <= tree_capacity, "the game's script function tree holds 320 names");

	// The registration 0x49fe30: `push 0x68a908` (imm32 at 0x49fe31), `push 0x121` (imm32 at
	// 0x49fe36), call 0x4d8770, mov ecx,eax, call 0x4d75a0, ret. The two operands share a page.
	constexpr DWORD registration = 0x49fe30;
	constexpr DWORD table_operand = 0x49fe31;
	constexpr DWORD count_operand = 0x49fe36;

	inline std::array<limits_rules::operand_write, 2> registration_writes(const DWORD table)
	{
		return {{{table_operand, 4, table}, {count_operand, 4, table_count}}};
	}

	// The script system ([0x787740], built at the first 0x4d8770) and its tree's name count: past the
	// 19 builtins, the game has registered its functions already and a new table would be ignored.
	constexpr DWORD script_system_pointer = 0x787740;
	constexpr DWORD tree_count_offset = 0x1948;

	// The DLL's table: the game's 289 entries as they are, then the fix's, handlers in `functions` order.
	inline void build_table(const func_entry* retail, const std::array<const void*, functions.size()>& handlers, func_entry* out)
	{
		std::memcpy(out, retail, retail_count * sizeof(func_entry));
		for (std::size_t i = 0; i < functions.size(); ++i)
		{
			out[retail_count + i] = {handlers[i], functions[i].name, functions[i].ret, functions[i].args};
		}
	}

	// ---- [Game] ForcedTeams, AddHero and JoinHero -------------------------------------------------------

	struct switches
	{
		bool registered = false;   // the functions are added (ForcedTeams present)
		bool forced_teams = false; // xml2fixFeature("forcedteams")
		bool add_hero = false;     // xml2fixFeature("addhero"), and addHero itself
		bool join_hero = false;    // xml2fixFeature("joinhero"), and joinHero itself
		std::vector<std::string> notes; // for the log, in order
	};

	// ForcedTeams absent: nothing registered (a script's xml2fixFeature line is dropped at compile and
	// its variable stays 0, so it opens the team menu). ForcedTeams=0: registered, reporting off.
	// ForcedTeams=1: registered, on. AddHero=1 counts only with ForcedTeams=1. Anything but 0 or 1 is
	// logged and taken as 0 (the team menu: the stock behaviour). JoinHero is on with ForcedTeams=1
	// unless it is 0 (a value that isn't 0 or 1 is logged and taken as that default, 1).
	inline switches decide(const std::optional<std::string_view> forced_teams, const std::optional<std::string_view> add_hero,
	                       const std::optional<std::string_view> join_hero = std::nullopt)
	{
		switches result;
		const auto flag = [&](const std::string_view key, const std::string_view text) -> std::optional<bool>
		{
			const auto value = limits_rules::value_text(text);
			if (value == "1") return true;
			if (value == "0") return false;
			result.notes.push_back(std::string(key) + "=" + std::string(value) + " isn't 0 or 1 - taken as 0");
			return std::nullopt;
		};
		if (!forced_teams)
		{
			if (add_hero && limits_rules::value_text(*add_hero) != "0")
			{
				result.notes.push_back("AddHero is set but ForcedTeams isn't - nothing registered");
			}
			if (join_hero && limits_rules::value_text(*join_hero) != "0")
			{
				result.notes.push_back("JoinHero is set but ForcedTeams isn't - nothing registered");
			}
			return result;
		}
		result.registered = true;
		result.forced_teams = flag("ForcedTeams", *forced_teams).value_or(false);
		if (add_hero)
		{
			const bool wanted = flag("AddHero", *add_hero).value_or(false);
			if (wanted && !result.forced_teams)
			{
				result.notes.push_back("AddHero=1 needs ForcedTeams=1 - addHero reports off");
			}
			result.add_hero = wanted && result.forced_teams;
		}
		bool join_wanted = true;
		if (join_hero)
		{
			const auto value = limits_rules::value_text(*join_hero);
			if (value == "0")
			{
				join_wanted = false;
			}
			else if (value != "1")
			{
				result.notes.push_back("JoinHero=" + std::string(value) + " isn't 0 or 1 - taken as 1 (the default)");
			}
			else if (!result.forced_teams)
			{
				result.notes.push_back("JoinHero=1 needs ForcedTeams=1 - joinHero reports off");
			}
		}
		result.join_hero = join_wanted && result.forced_teams;
		return result;
	}

	enum class feature : std::uint8_t
	{
		forced_teams,
		add_hero,
		join_hero,
	};

	inline std::string lowercase(std::string_view text)
	{
		std::string result(text);
		for (auto& c : result)
		{
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return result;
	}

	// A name as the scripts write it: spaces and tabs around it dropped, lowercase (the game's own
	// names are lowercase, and its slot setter lowercases too).
	inline std::string clean_name(const std::string_view text)
	{
		return lowercase(limits_rules::trimmed(text));
	}

	inline std::optional<feature> feature_named(const std::string_view name)
	{
		const auto clean = clean_name(name);
		if (clean == "forcedteams") return feature::forced_teams;
		if (clean == "addhero") return feature::add_hero;
		if (clean == "joinhero") return feature::join_hero;
		return std::nullopt;
	}

	// ---- Hero names ---------------------------------------------------------------------------------

	using party = std::array<std::string, 4>;

	struct seating
	{
		party slots;                       // the non-empty names first, in the order given
		std::vector<std::string> dropped;  // repeats of a name already seated
	};

	// seatParty's four arguments as they are seated: cleaned, a repeat dropped, and compacted, so
	// slot 0 is filled whenever any is (the team menu builds a default party - [HERO] Name1.. or
	// random - only when slot 0 is empty, 0x5e374b) and the party count is the next free slot (the
	// dormant addHero 0x46c9f0 seats at slot = count).
	inline seating seat_order(const party& names)
	{
		seating result;
		std::size_t next = 0;
		for (const auto& raw : names)
		{
			const auto name = clean_name(raw);
			if (name.empty())
			{
				continue;
			}
			if (std::find(result.slots.begin(), result.slots.begin() + static_cast<std::ptrdiff_t>(next), name) != result.slots.begin() + static_cast<std::ptrdiff_t>(next))
			{
				result.dropped.push_back(name);
				continue;
			}
			result.slots[next++] = name;
		}
		return result;
	}

	// setSkinset's hero list: comma separated, cleaned, no empties, no repeats.
	inline std::vector<std::string> split_heroes(const std::string_view list)
	{
		std::vector<std::string> names;
		std::size_t start = 0;
		while (start <= list.size())
		{
			const auto comma = list.find(',', start);
			const auto end = comma == std::string_view::npos ? list.size() : comma;
			const auto name = clean_name(list.substr(start, end - start));
			if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end())
			{
				names.push_back(name);
			}
			if (comma == std::string_view::npos)
			{
				break;
			}
			start = comma + 1;
		}
		return names;
	}

	// "magma / cyclops / - / -" for the log.
	inline std::string describe(const party& slots)
	{
		std::string text;
		for (std::size_t i = 0; i < slots.size(); ++i)
		{
			text += (i ? " / " : "") + (slots[i].empty() ? std::string("-") : slots[i]);
		}
		return text;
	}

	// A call as the script wrote it, for the log: seatParty("magma", "", "", "").
	inline std::string call_text(const std::string_view name, const std::vector<std::string>& args)
	{
		std::string text = std::string(name) + "(";
		for (std::size_t i = 0; i < args.size(); ++i)
		{
			text += (i ? ", \"" : "\"") + args[i] + "\"";
		}
		return text + ")";
	}

	// ---- Costumes -------------------------------------------------------------------------------------

	struct costume
	{
		std::string_view name;
		int index;
	};

	// The game's costume names (the table at 0x6d8aa0: 8-byte {name, index} entries, then {"", -1}).
	// A hero's costume is the byte at stats+0x120; stats+0x255+i is costume i's skin variant, 0 when
	// the hero has no such costume (the reader 0x4b8090 at 0x4b80e7/0x4b8104, the team menu's cycler
	// 0x4b7fc0 skipping a 0). The skin is picked from it when the character loads, at the next zone
	// load, with no unlock check - the byte is what the fix writes. 9 is a spawner's override slot.
	inline constexpr std::array<costume, 9> costumes{{
		{"default", 0},
		{"astonishing", 1},
		{"aoa", 2},
		{"60s", 3},
		{"70s", 4},
		{"weaponx", 5},
		{"future", 6},
		{"winter", 7},
		{"civilian", 8},
	}};
	constexpr DWORD costume_table = 0x6d8aa0;
	constexpr DWORD stats_costume = 0x120;  // byte: the current costume
	constexpr DWORD stats_variants = 0x255; // byte per costume: its skin variant, 0 = none

	// A costume by name, as the game's lookup 0x5602f0 matches it (_stricmp); spaces around it dropped.
	// "magmacivilian" is XML1's and not the game's (the port renames Magma's to civilian, slot 8).
	inline std::optional<int> costume_index(const std::string_view name)
	{
		const auto clean = clean_name(name);
		for (const auto& c : costumes)
		{
			if (c.name == clean)
			{
				return c.index;
			}
		}
		return std::nullopt;
	}

	inline std::string costume_name(const int index)
	{
		for (const auto& c : costumes)
		{
			if (c.index == index)
			{
				return std::string(c.name);
			}
		}
		return "costume " + std::to_string(index);
	}

	// The costumes a mission's skinset sets and takes back: default and XML1's four skinsets. The
	// others (astonishing, aoa, future, winter, a spawner's 9) are the player's pick and stay.
	constexpr bool mission_costume(const int costume)
	{
		return costume == 0 || costume == 3 || costume == 4 || costume == 5 || costume == 8;
	}

	// What setSkinset(costume, heroes) makes of a hero's costume `current`: in a mission costume, the
	// skinset's costume if the hero is listed and has it, otherwise default; any other costume stays.
	// XML1 derived the costume per mission at character load (party-seating.md 8); XML2 keeps it in
	// the hero, so every mission start sets it, and "default" takes the last one back.
	constexpr int skinset_costume(const int current, const bool listed, const int costume, const bool has_variant)
	{
		if (!mission_costume(current))
		{
			return current;
		}
		return listed && costume != 0 && has_variant ? costume : 0;
	}

	// ---- The side-mission stack (pushsidemission / restorelastzone) ----------------------------------

	// Two records of 0x26c bytes at 0x72b5b0 (mission manager 0x48a0e0 -> vt+0x44), the count after
	// them at +0x4dc (0x5f3684, 0x5f36d9). A record: the zone at +0 (0x40 bytes), the party's four
	// names at +0xa4 (0x20 bytes each, by slot; the fill 0x48a2a3-0x48a320 copies each slot's name
	// with strncpy 0x20 and a 0 at +0x1f, "" for an empty slot; restorelastzone reads them at 0x5f4614
	// = its copy + 0xa4). pushsidemission (0x5f3630) fills one from the party of the moment and the
	// entity's spot; the command "restorelastzone <n>" (0x5f4580) seats its names when n is 0 (any
	// other n keeps the party as it is - the team menu's accept runs restorelastzone('1') after
	// seating the menu's picks), restores the game-state block and runs "loadmap <zone> 1" either
	// way (0x5f4678: always 1), whose load puts the party on that spot and pops the record (0x5f48d3).
	// "cancelsidemission" (0x5f2ce0) takes the top record off without loading anything. The stack is
	// saved with the game (0x46bcd4) and loaded with it (0x46e3ec).
	constexpr DWORD side_record_size = 0x26c;
	constexpr DWORD side_count_offset = 0x4dc;
	constexpr DWORD side_zone_size = 0x40;
	constexpr DWORD side_names_offset = 0xa4;
	constexpr DWORD side_name_size = 0x20;
	constexpr int side_records_max = 2;

	struct side_record
	{
		std::string zone;
		party names;
	};

	// ---- One popParty per side mission's end ----------------------------------------------------------
	//
	// A queued command runs at the game's next frame (0x402205 -> the console's vt+0 0x55c230, which
	// runs every command waiting: 0x55c245-0x55c2f9), and until then the zone's scripts can call
	// popParty again: XML1's sent_fb end (nycfb4_finish) is the death script of the zone's sentinel
	// spawners, run for every kill after the objective. A second "restorelastzone 0" would run right
	// after the first, whose "loadmap <zone> 1" pops the record at once (0x5f48c9 loads through game
	// vt+0x150, 0x5f48d3 -> 0x5f44a0 pops), and with no record left restorelastzone runs "mainmenuexit
	// 1" (0x5f46bf): the main menu. So popParty remembers what it queued, and a call that finds it
	// still waiting (the console holds commands, the stack and the zone are as they were) does
	// nothing; so does one that finds its load under way (the zone manager names the new zone before
	// it loads, 0x4840dc -> vt+0xbc 0x4841b0, and reports the load pending, vt+0x24, until it is in),
	// in case the zone being left still runs a script. Anything else - the command has run and its
	// zone is in, another zone, another stack - is a new end and goes ahead.
	// joinHero queues the same "restorelastzone 0" after pushing its record, and is remembered the
	// same way: a second joinHero or popParty while it waits or its load runs does nothing.
	struct queued_pop
	{
		bool restore = false; // "restorelastzone 0"; else the team menu at `to`
		int records = 0;      // the side-mission stack when it was queued
		std::string from;     // the zone it was queued in (lowercase)
		std::string to;       // the zone it loads (lowercase): the record's, or the fallback
		std::string command;
		std::string joined;   // joinHero's hero ("" for popParty)
	};

	// What the functions keep between calls (the game calls them on its one thread).
	struct call_state
	{
		std::optional<queued_pop> pop;
		std::string stack_warned; // the side-mission stack xml2fixFeature last warned about
	};

	// ---- The game's objects and functions -----------------------------------------------------------

	constexpr DWORD game_getter = 0x46dce0;         // __cdecl: the game, 0x729960
	constexpr DWORD game_vtable = 0x686e1c;
	constexpr DWORD game_seat_slot = 0xf0;          // (int slot, uint* handle), ret 8: a name, 0 = empty
	constexpr DWORD game_add_hero_slot = 0x170;     // bool (const char* name), ret 4: the dormant addHero 0x46c9f0
	constexpr DWORD game_party_offset = 0x14;       // four name handles, 0 = empty (0x46c883)
	constexpr DWORD registry_getter = 0x44b8f0;     // __cdecl: the stats registry
	constexpr DWORD registry_vtable = 0x68544c;
	constexpr DWORD registry_index_slot = 0x3c;     // short (const char* name): 0 = no such character
	constexpr DWORD registry_hero_count_slot = 0x48;// int (): the herostat heroes (their list at +0x120dc)
	constexpr DWORD registry_hero_at_slot = 0x4c;   // short (int i)
	constexpr DWORD registry_stats_slot = 0x74;     // stats* (short index)
	constexpr DWORD registry_has_stats_slot = 0x78; // bool (short index)
	constexpr DWORD registry_herostat_slot = 0x7c;  // bool (short index): a herostat hero (entry flag bit 0)
	constexpr DWORD intern_name = 0x425bf0;         // __thiscall (uint* handle, const char*): 0 for ""
	constexpr DWORD name_text = 0x425bc0;           // __thiscall const char* (const uint* handle): "" for 0
	constexpr DWORD script_system_getter = 0x4d8770;
	constexpr DWORD make_int_value = 0x4d6570;      // __thiscall (sys, int), ret 4
	constexpr DWORD make_string_value = 0x4d9430;   // __thiscall (sys, const char*), ret 4
	constexpr DWORD argument_getter = 0x4d5830;     // __thiscall (args, int), ret 4
	constexpr DWORD value_int_slot = 4;             // vt+0x10
	constexpr DWORD value_text_slot = 5;            // vt+0x14
	constexpr DWORD console_getter = 0x55c890;      // __cdecl: the console, 0x7ac290
	constexpr DWORD console_vtable = 0x69a81c;
	constexpr DWORD console_run_slot = 0x18;        // bool (const char* line): runs it now
	constexpr DWORD console_queue_slot = 0x1c;      // bool (const char* line): queued for the next frame; false with two waiting
	constexpr DWORD console_waiting = 0x630;        // int: the commands waiting, 0..2 (the queue's check 0x55c426, the frame's run 0x55c245)
	constexpr DWORD zones_getter = 0x484990;        // __cdecl: the zone manager, 0x72a578 (game+0xc, 0x468e8a)
	constexpr DWORD zones_vtable = 0x68878c;
	constexpr DWORD zones_loading_slot = 0x24;      // bool (): a zone load is pending, [+0x220] & 3 (the HUD skips its frame while it is, 0x59f1d8)
	constexpr DWORD zones_current_slot = 0x5c;      // const char* (): the current zone, +0x1e0 (0x40 bytes), named when its load is asked for
	constexpr std::size_t zone_name_size = 0x40;
	constexpr DWORD entity_by_name = 0x4a1700;      // __cdecl uint* (uint* out, const char* name): _ACTIVE_HERO_ and the like too
	constexpr DWORD entity_of_handle = 0x4654b0;    // __thiscall entity* (const uint* handle)
	constexpr DWORD character_class = 0x718448;     // dword: the class id; bit (id + 0x24) of the entity's class info +0x14 = a character
	constexpr DWORD entity_id_offset = 0x1c;
	constexpr DWORD missions_getter = 0x48a0e0;     // __cdecl: the mission manager
	constexpr DWORD missions_vtable = 0x6892d4;
	constexpr DWORD missions_side_stack_slot = 0x44;
	constexpr DWORD menus_getter = 0x5d8920;        // __cdecl: the menu manager
	constexpr DWORD menus_vtable = 0x6a236c;
	constexpr DWORD menus_current_slot = 0x214;     // const char* (): the current menu's name, "" for none
	constexpr DWORD hud_getter = 0x59ee20;          // __cdecl: the HUD, 0x81d7e0
	constexpr DWORD hud_vtable = 0x69dca4;
	constexpr DWORD hud_leave_slot = 0x80;          // what every script load function calls after queuing its load

	constexpr std::string_view loading_menu = "loading"; // restorelastzone's script function does nothing while it is up (0x4a0791-0x4a07a9)
	constexpr std::size_t console_max = 127;             // what the console keeps of a line

	// Every byte of XMen2.exe the functions rely on, read from the retail build. All must match
	// before anything is patched; xml2_test compares them with a copy of the exe.
	inline constexpr std::array<guard, 101> guards{{
		// The registration and the tree.
		{0x49fe30, "6808a968006821010000e8318903008bc8e85a770300c3", "the registration (0x49fe30: push table, push count, call 0x4d75a0)"},
		{0x4d7637, "81bf4819000040010000", "the tree's 320-name cap (0x4d7637)"},
		{0x4d868f, "68d80369006a13", "the 19 builtins' registration (0x4d868f)"},
		{0x4d877e, "a140777800", "the script system's pointer (0x787740, read at 0x4d877e)"},
		{0x68d36c, "30fe4900", "the script interface's vt+0 (0x68d36c = 0x49fe30)"},
		{0x68a908, "c01c4a0058ce6800189c680054ce6800", "the first retail script function (setRotZ, 0x68a908)"},
		{0x68bb08, "10fe490028bb6800189c680068196800", "the last retail script function (SetDontShowWarningOff, 0x68bb08)"},
		// Arguments and results.
		{0x4d5830, "8b511c33c085d2740d568b7424083bf27d038b04b15ec20400", "the argument getter (0x4d5830)"},
		{0x4d8770, "64a1000000006aff681149670050a140777800", "the script system's getter (0x4d8770)"},
		{0x4d6570, "8b9190c0000033c081fa14060000", "the int value maker (0x4d6570)"},
		{0x4d9445, "8b9190c0000083ec0833c081fa14060000", "the string value maker (0x4d9430)"},
		{0x6907a0, "e0d75500", "a string value's text (vt+0x14 = 0x55d7e0)"},
		{0x69060c, "e0d75500", "an int value's number (vt+0x10 = 0x55d7e0)"},
		{0x55d7e0, "8b4104c3", "the value accessor (0x55d7e0)"},
		// The game and its party.
		{0x46dce0, "64a1000000008a0d8ca072006aff681e38670050b80100000084c864892500000000752509058ca07200b960997200c744240800000000e834b1ffff68b0db6700e8f843200083c4048b0c24b86099720064890d0000000083c40cc3",
		 "the game's getter (0x46dce0)"},
		{0x468e6d, "c7061c6e6800", "the game's vtable (0x686e1c, stored at 0x468e6d)"},
		{0x686f0c, "10c84600", "game vt+0xf0 (the slot setter 0x46c810)"},
		{0x686f8c, "f0c94600", "game vt+0x170 (the dormant addHero 0x46c9f0)"},
		{0x46c810, "81ec08010000a1f8386f0053578bbc241401000085ff8984240c0100008bd90f8ce500000083ff040f8ddc", "the slot setter's prologue (0x46c810)"},
		{0x46c883, "8b74bb148d7cbb14", "the party slots at game+0x14 (0x46c883)"},
		{0x46c928, "81c408010000c20800", "the slot setter's ret 8 (0x46c928)"},
		{0x46c9f0, "6aff68c637670064a100000000506489250000000081ec5c020000a1f8386f005355565789842468020000", "addHero's prologue (0x46c9f0)"},
		{0x46caab, "83f8040f8d4f030000", "addHero's party-full check (0x46caab)"},
		{0x46cdff, "b001eb0232c0", "addHero's bool result (0x46cdff)"},
		{0x46ce23, "81c468020000c20400", "addHero's ret 4 (0x46ce23)"},
		{0x425bf0, "568b74240885f6578bf97426803e0074216a0056e817c51d0083c404405056e82cc51d008bc8e80547ffff89075f5ec20400c707000000005f5ec20400", "the name interner (0x425bf0)"},
		{0x425bc0, "568b3185f67507b8681968005ec3e86dc51d0081e6ffffff008b4cb0048d8401088000005ec3", "the name reader (0x425bc0)"},
		// The stats registry.
		{0x44b8f0, "64a1000000006aff68b132670050a10c77710064892500000000", "the registry's getter (0x44b8f0)"},
		{0x44b541, "c7064c546800", "the registry's vtable (0x68544c, stored at 0x44b541)"},
		{0x685488, "c0ac4400", "registry vt+0x3c (name -> index, 0x44acc0)"},
		{0x685494, "b0b64400", "registry vt+0x48 (hero count, 0x44b6b0)"},
		{0x685498, "e0b64400", "registry vt+0x4c (hero index, 0x44b6e0)"},
		{0x6854c0, "90b84400", "registry vt+0x74 (index -> stats, 0x44b890)"},
		{0x6854c4, "a0b74400", "registry vt+0x78 (index has stats, 0x44b7a0)"},
		{0x6854c8, "c0b74400", "registry vt+0x7c (index is a herostat hero, 0x44b7c0)"},
		{0x44acc0, "81ec08010000a1f8386f00898424040100008b84240c01000085c056578bf10f8499000000", "registry vt+0x3c's prologue (0x44acc0)"},
		{0x44ad5f, "668b845080170000", "registry vt+0x3c's short result (0x44ad5f)"},
		{0x44ad7b, "c20400", "registry vt+0x3c's ret 4 (0x44ad7b)"},
		{0x44b6b0, "8b8130230100c3", "registry vt+0x48 (0x44b6b0)"},
		{0x44b6e0, "8b442404668b8441dc200100c20400", "registry vt+0x4c (0x44b6e0)"},
		{0x44b890, "0fbf4424046bc01c8b8408389b00008d510423821c9b000069c0f804000003c2c20400", "registry vt+0x74 (0x44b890)"},
		{0x44b7a0, "0fbf4424046bc01c568bb408389b000033d285f60f95c28ac25ec20400", "registry vt+0x78 (0x44b7a0)"},
		{0x44b7c0, "0fbf4424046bc01c8a8408409b00002401c20400", "registry vt+0x7c (0x44b7c0)"},
		{0x44c22f, "8ad180e2010ac284c988461866895e1674198b8530230100668b4c241c66898c45dc200100ff8530230100", "the hero list is the herostat entries (0x44c22f)"},
		// Costumes.
		{0x6d8aa0, "902f68000000000020926800010000001c92680002000000189268000300000014926800040000000c926800050000000492680006000000fc91680007000000f09168000800000068196800ffffffff",
		 "the costume table (0x6d8aa0)"},
		{0x4b80e7, "0fb68620010000", "the current costume at stats+0x120 (0x4b80e7)"},
		{0x4b8104, "8a843055020000", "the costume variants at stats+0x255 (0x4b8104)"},
		{0x4b7ffc, "8a84335502000084c076db", "a variant of 0 is no costume (0x4b7ffc)"},
		// Entities and the side-mission stack.
		{0x4a7060, "8d44240c5750e895a6ffff83c4088bc8e83be4fbff8bf085f60f84980000008b168bceff128b154884710083c2248bca83e11fbf01000000d3e7c1fa05857c90147474e8781813008b106a008bc8ff52088b461c506884d568008d4c2418680001000051c644242000e822940b0083c410e8ba570b008b108d4c2410518bc8ff5218",
		 "extractionPointChange's character test and pushsidemission (0x4a7060)"},
		{0x4a1700, "83ec1c568b74242856e82e0c1d0083c40485c089442404", "the entity-by-name lookup (0x4a1700)"},
		{0x4654b0, "568bf1e8682a06008b16518bcc89118b108bc8ff520c5ec3", "the entity getter (0x4654b0)"},
		{0x68d584, "70757368736964656d697373696f6e20256400", "the text \"pushsidemission %d\" (0x68d584)"},
		{0x68d284, "726573746f72656c6173747a6f6e6520257300", "the text \"restorelastzone %s\" (0x68d284)"},
		{0x68d31c, "6c6f61646d61702025732030203100", "the text \"loadmap %s 0 1\" (0x68d31c)"},
		{0x48a0e0, "a108b1720085c07537", "the mission manager's getter (0x48a0e0)"},
		{0x489ff3, "c706d4926800", "the mission manager's vtable (0x6892d4, stored at 0x489ff3)"},
		{0x689318, "70a04800", "mission manager vt+0x44 (the side-mission stack, 0x48a070)"},
		{0x48a070, "b8b0b57200c3", "the side-mission stack (0x72b5b0, 0x48a070)"},
		{0x5f3684, "83b8dc040000027d6b", "pushsidemission's two-record cap (0x5f3684)"},
		{0x5f36d9, "8bb8dc04000069ff6c02000003f8b99b0000008d742410f3a5ff80dc040000", "pushsidemission's append (0x5f36d9)"},
		{0x5f459f, "8b88dc04000033db3bcb0f8e100100005657e82a5be9ff8bf0e8235be9ff8bf88b068bceff50448b178bb0dc0400008bcfff524469f66c0200008d840694fdffff508d8c2494000000e813feffff895c240ce89a82f6ff8b8c24040300008b10518bc8ff520850e831dd070083c40485c0755733ff8db42434010000eb038d49003bf37423381e741f5356e8f1da000083c404405056e806db00008bc8e8df5ce2ff8944240ceb04895c240ce89096e7ff8b108d4c240c51578bc8ff92f00000004783c62083ff047cb7e87296e7ff",
		 "restorelastzone's top record and seat loop (0x5f459f)"},
		// The console.
		{0x55c890, "8a0dccca7a00b80100000084c875258b15ccca7a000bd0b990c27a008915ccca7a00e889feffff68d0e06700e85d58110083c404b890c27a00c3", "the console's getter (0x55c890)"},
		{0x69a834, "b0be550010c45500", "console vt+0x18 and vt+0x1c (0x55beb0 runs a line now, 0x55c410 queues one)"},
		{0x55c410, "8b44240481ec8000000085c0568bf17445803800744083be300600000274376880000000", "the console's queue (0x55c410)"},
		{0x55beb0, "518b44240885c056578bf98944241074", "the console's run-now (0x55beb0)"},
		// What popParty does as the game's script load functions do.
		{0x4a0788, "8bf0e8918113008b1068748868008bc8ff921402000050e8be1d1d0083c40885c07438568d4424086884d2680050c644241000e82c191d0083c40ce8c8c00b008b108d4c2404518bc8ff521ce847e60f008b108bc8ff9280000000",
		 "restorelastzone's script function (0x4a0788)"},
		{0x4a0d30, "83ec44a1f8386f008b4c2448894424406a00e8e94a03008b108bc8ff5214508d442404681cd3680050c644240c00e889131d0083c40ce825bb0b008b108d0c24518bc8ff521ce8a5e00f008b108bc8ff92800000008b4c244033c0e8d1131d0083c444c3",
		 "loadMapChooseTeam (0x4a0d30)"},
		{0x5d8920, "a118ff8a0085c0750ae882ffffffa118ff8a00c3", "the menu manager's getter (0x5d8920)"},
		{0x5d7f4b, "c7066c236a00", "the menu manager's vtable (0x6a236c, stored at 0x5d7f4b)"},
		{0x6a2580, "40865d00", "menu manager vt+0x214 (the current menu's name, 0x5d8640)"},
		{0x5d8640, "8b819060080085c0740483c00cc3b868196800", "the current menu's name (0x5d8640)"},
		{0x688874, "6c6f6164696e6700", "the text \"loading\" (0x688874)"},
		{0x59ee20, "64a1000000008a0d94618a006aff685e84670050b80100000084c8648925000000007525090594618a00b9e0d78100c744240800000000e8e4f8ffff68a0e36700e8b8320d0083c4048b0c24b8e0d7810064890d0000000083c40cc3",
		 "the HUD's getter (0x59ee20)"},
		{0x59e75f, "c706a4dc6900", "the HUD's vtable (0x69dca4, stored at 0x59e75f)"},
		{0x69dd24, "60925900", "HUD vt+0x80 (0x599260)"},
		// What one popParty per side mission's end relies on (queued_pop).
		{0x55c245, "8b873006000085c00f8eae000000", "the frame's run of the console queue starts on the count at +0x630 (0x55c245)"},
		{0x55c2e5, "8b178d44240c508bcfff52188b873006000085c00f8f61ffffff", "and runs every command waiting (0x55c2e5)"},
		{0x5f48ad, "85ff7405e83afeffffe82594e7ff8b4c24088b10518d4c2410518bc8ff925001000085ff7405e8c8fbffff",
		 "\"loadmap <zone> 1\" loads (game vt+0x150) and pops the top record at once (0x5f48ad)"},
		{0x5f44ee, "e8ed5be9ff8b108bc8ff5244ff88dc040000", "the pop (0x5f44fa: dec [stack+0x4dc])"},
		{0x5f46bf, "e8cc81f6ff8b1068ccd168008bc8ff5218", "restorelastzone with no record runs \"mainmenuexit 1\" (0x5f46bf)"},
		{0x68d1cc, "6d61696e6d656e7565786974203100", "the text \"mainmenuexit 1\" (0x68d1cc)"},
		{0x686f6c, "f0984600", "game vt+0x150 (0x4698f0)"},
		{0x4698f0, "568bf18b06ff904c0100008b44240c8b4e0c8b11508b44240c50ff523884c00f95c05ec20800", "game vt+0x150: the zone manager's vt+0x38 (0x4698f0)"},
		{0x468e85, "e806bb010089460c", "game+0xc is the zone manager (0x468e85)"},
		{0x484990, "64a1000000008a0d2caa72006aff68ce3b670050b80100000084c864892500000000752509052caa7200b978a57200c744240800000000e824f2ffff68d0db6700e848d71e0083c4048b0c24b878a5720064890d0000000083c40cc3",
		 "the zone manager's getter (0x484990)"},
		{0x483c1f, "c7068c876800", "the zone manager's vtable (0x68878c, stored at 0x483c1f)"},
		{0x6887b0, "903e4800", "zone manager vt+0x24 (a load pending, 0x483e90)"},
		{0x6887c4, "b0404800", "zone manager vt+0x38 (a zone load, 0x4840b0)"},
		{0x6887e8, "303f4800", "zone manager vt+0x5c (the current zone, 0x483f30)"},
		{0x688848, "b0414800", "zone manager vt+0xbc (names the current zone, 0x4841b0)"},
		{0x483e90, "f6812002000003750333c0c3b801000000c3", "a load pending: [+0x220] & 3 (0x483e90)"},
		{0x483f30, "8d81e0010000c3", "the current zone at +0x1e0 (0x483f30)"},
		{0x4840dc, "8b4424088b16508bceff92bc000000e890e310008b108d8ee0010000518bc8ff92ac000000808e2002000003",
		 "a zone load names the zone (vt+0xbc) and sets it pending (0x4840dc)"},
		{0x4841b0, "56578bf16a008dbee00100006a0057e85ce3100083c40ce86446040084c074136a40686819680083c62056e8f07bf8ff83c40c8b44240c6a405057e8e07bf8ff83c40c5f5ec20400",
		 "the zone's name into +0x1e0 (0x4841b0)"},
		// What joinHero relies on besides pushParty's and popParty's.
		{0x48a2a3, "8b7c241033ed81c7a4000000906a20686819680057e8b37e1e0083c40c885f1fe8183afeff8b10558bc8ff92e000000085c07442e8043afeff8b10558bc8ff92e00000008b303bf37507b868196800eb16e8477e170081e6ffffff008b4cb0048d8401088000006a205057e85d7e1e0083c40c885f1f4583c72083fd047c8e",
		 "a record's party names: slot i's at +0xa4 + 0x20 i, strncpy 0x20 and a 0 at +0x1f, \"\" when empty (0x48a2a3)"},
		{0x5f466e, "8b108bc8ff92040200006a018d842494000000508d4c24186844376a0051885c2420e857da070083c410e8f381f6ff8b108d4c2410518bc8ff5218",
		 "restorelastzone restores the game-state block and runs \"loadmap <zone> 1\" now, whatever its argument (0x5f466e)"},
		{0x5f4a0f, "68e02c5f006830396a00", "cancelsidemission's registration (0x5f4a0f)"},
		{0x6a3930, "63616e63656c736964656d697373696f6e00", "the text \"cancelsidemission\" (0x6a3930)"},
		{0x5f2ce0, "e8fb73e9ff8b108bc8ff52448b88dc04000085c97e12e8e573e9ff8b108bc8ff5244ff88dc040000c3",
		 "cancelsidemission takes the top record off (0x5f2ce0)"},
		{0x55c2dd, "518bcee86bf9ffff", "the frame's run of the queue unlinks a command before running it (0x55c2dd)"},
		{0x55bcb0, "8b8830010000495f898830010000", "and the unlink counts it off (0x55bcb0)"},
	}};

	// ---- Reading a handler's arguments (no C++ objects: the reads are SEH-guarded) --------------------

	using get_argument_t = void*(__fastcall*)(void* args, void* edx, int index); // __thiscall, ret 4
	using value_text_t = const char*(__fastcall*)(void* value, void* edx);
	using value_int_t = int(__fastcall*)(void* value, void* edx);

	constexpr std::size_t argument_max = 255;

	// Argument `index`'s text, up to 255 characters, into `out`; false if there is none or reading faults.
	inline bool read_text_argument(const get_argument_t get, void* args, const int index, char (&out)[argument_max + 1])
	{
		__try
		{
			out[0] = 0;
			void* value = get(args, nullptr, index);
			if (!value)
			{
				return false;
			}
			const char* text = (*static_cast<value_text_t**>(value))[value_text_slot](value, nullptr);
			std::size_t i = 0;
			for (; text && text[i] && i < argument_max; ++i)
			{
				out[i] = text[i];
			}
			out[i] = 0;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			out[0] = 0;
			return false;
		}
	}

	inline bool read_int_argument(const get_argument_t get, void* args, const int index, int& out)
	{
		__try
		{
			void* value = get(args, nullptr, index);
			if (!value)
			{
				return false;
			}
			out = (*static_cast<value_int_t**>(value))[value_int_slot](value, nullptr);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// ---- What each function does ----------------------------------------------------------------------
	//
	// Over an engine `e` with (every call reports a fault in the game's code as nullopt / false):
	//   std::optional<std::string> text_argument(void* args, int i); std::optional<int> int_argument(void* args, int i);
	//   void* make_int(int); void* make_string(const std::string&);
	//   bool forced_teams(); bool add_hero_on(); bool join_hero_on();  - the switches, as they are now
	//   std::optional<int> hero_index(const std::string&);             - registry vt+0x3c, 0 = none
	//   std::optional<bool> herostat(int index);                       - registry vt+0x7c
	//   std::optional<std::string> slot(int i); bool seat(int i, const std::string&);
	//   std::optional<int> hero_count(); std::optional<int> hero_at(int i); std::optional<bool> has_stats(int index);
	//   std::optional<int> costume(int index); std::optional<bool> has_variant(int index, int costume); bool set_costume(int index, int costume);
	//   std::optional<int> side_records(); std::optional<side_record> side_record_at(int i);
	//   bool set_side_name(int record, int slot, const std::string& name); - a record's party name, as the fill writes it
	//   character_lookup find_character(const std::string&, int& id);
	//   bool run_now(const std::string&); std::optional<bool> queue(const std::string&); std::optional<int> waiting();
	//   std::optional<std::string> current_menu(); void leave_hud();
	//   std::optional<std::string> current_zone(); std::optional<bool> zone_loading();
	//   call_state& state();                                           - kept from call to call
	//   std::optional<bool> add_hero(const std::string&);
	//   void log(const std::string&);

	enum class character_lookup : std::uint8_t
	{
		found,
		missing,
		not_character,
		fault,
	};

	constexpr const char* prefix = "forced teams: ";

	template <typename Engine>
	std::optional<party> read_party(Engine& e)
	{
		party slots;
		for (int i = 0; i < 4; ++i)
		{
			const auto name = e.slot(i);
			if (!name)
			{
				return std::nullopt;
			}
			slots[static_cast<std::size_t>(i)] = *name;
		}
		return slots;
	}

	template <typename Engine>
	std::string party_text(Engine& e)
	{
		const auto slots = read_party(e);
		return slots ? describe(*slots) : std::string("(party unreadable)");
	}

	template <typename Engine>
	std::string record_text(Engine& e, const int index)
	{
		const auto record = e.side_record_at(index);
		return record ? record->zone + " with " + describe(record->names) : std::string("(record unreadable)");
	}

	// ForcedTeams off with records on the side-mission stack: pushParty's, from a game saved inside a
	// flashback with ForcedTeams=1 (the switch is read once, at start-up) or begun with it and ended
	// without xml2-fix. Only popParty pops them, and the script asking is about to open the team menu,
	// so they stay - saved with the game (0x46bcd4) until a New Game (resetgame 0x5f2e70 runs
	// "clearsidemissions", 0x5f2efe); meanwhile every zone load runs as a side mission's (0x5f47e0) and
	// a later pushParty has a place fewer. Logged once for each stack seen.
	template <typename Engine>
	void warn_left_records(Engine& e)
	{
		auto& warned = e.state().stack_warned;
		const auto records = e.side_records();
		if (!records || *records <= 0)
		{
			warned.clear();
			return;
		}
		const auto top = record_text(e, *records - 1);
		const auto stack = std::to_string(*records) + " " + top;
		if (warned == stack)
		{
			return;
		}
		warned = stack;
		e.log(std::string(prefix) + "WARNING: ForcedTeams is off but the side-mission stack holds " + (*records == 1 ? std::string("a record") : std::to_string(*records) + " records") +
		      " (the top: " + top + ") - pushParty's, from a flashback begun with ForcedTeams=1 (a game saved inside it?). With ForcedTeams off its end opens the team menu and "
		      "nothing pops the record: it stays, saved with the game, until a New Game. With ForcedTeams=1 the flashback's end returns to it.");
	}

	// Every name must be a character the game knows with a herostat entry: the slot setter takes any
	// name, and one that isn't a hero spawns with the registry's fallback stats. Empty if they all
	// are, else why not.
	template <typename Engine>
	std::string refuse_heroes(Engine& e, const party& names)
	{
		for (const auto& name : names)
		{
			if (name.empty())
			{
				continue;
			}
			const auto index = e.hero_index(name);
			if (!index)
			{
				return "ERROR: the game's name lookup (registry vt+0x3c) faulted on '" + name + "'";
			}
			if (*index == 0)
			{
				return "'" + name + "' isn't a character the game knows";
			}
			const auto hero = e.herostat(*index);
			if (!hero)
			{
				return "ERROR: the game's herostat check (registry vt+0x7c) faulted on '" + name + "'";
			}
			if (!*hero)
			{
				return "'" + name + "' isn't a herostat hero";
			}
		}
		return {};
	}

	template <typename Engine>
	std::optional<std::vector<std::string>> text_arguments(Engine& e, void* args, const int count, const char* function_name)
	{
		std::vector<std::string> values;
		for (int i = 0; i < count; ++i)
		{
			const auto value = e.text_argument(args, i);
			if (!value)
			{
				e.log(std::string(prefix) + function_name + ": ERROR: couldn't read argument " + std::to_string(i + 1) + " - nothing done");
				return std::nullopt;
			}
			values.push_back(*value);
		}
		return values;
	}

	// xml2fixFeature(name): 1 when the feature is on. Read at the call, so a switch changed while the
	// game runs counts from the next call.
	template <typename Engine>
	void* xml2fix_feature(Engine& e, void* args)
	{
		const auto values = text_arguments(e, args, 1, "xml2fixFeature");
		if (!values)
		{
			return e.make_int(0);
		}
		const auto which = feature_named((*values)[0]);
		int on = 0;
		if (which == feature::forced_teams)
		{
			on = e.forced_teams() ? 1 : 0;
		}
		else if (which == feature::add_hero)
		{
			on = e.forced_teams() && e.add_hero_on() ? 1 : 0;
		}
		else if (which == feature::join_hero)
		{
			on = e.forced_teams() && e.join_hero_on() ? 1 : 0;
		}
		e.log(std::string(prefix) + call_text("xml2fixFeature", *values) + " -> " + std::to_string(on) + (which ? "" : " (not a feature: forcedteams, addhero, joinhero)"));
		if (which == feature::forced_teams && !on)
		{
			warn_left_records(e);
		}
		return e.make_int(on);
	}

	// seatParty(h1, h2, h3, h4): the party becomes exactly these heroes, through the game's slot
	// setter (game vt+0xf0), as restorelastzone (0x5f4612-0x5f4667) and the Danger Room's course
	// start do; nothing spawns until the next zone load (0x486dd0), which the script does next. All
	// or nothing: a name that isn't a herostat hero, or no name at all, leaves the party as it is.
	template <typename Engine>
	void* seat_party(Engine& e, void* args)
	{
		const auto values = text_arguments(e, args, 4, "seatParty");
		if (!values)
		{
			return nullptr;
		}
		const auto call = std::string(prefix) + call_text("seatParty", *values);
		const auto order = seat_order({(*values)[0], (*values)[1], (*values)[2], (*values)[3]});
		if (order.slots[0].empty())
		{
			e.log(call + ": no hero named - party left as it is (" + party_text(e) + "; an empty party would have the team menu build a default one)");
			return nullptr;
		}
		if (const auto refusal = refuse_heroes(e, order.slots); !refusal.empty())
		{
			e.log(call + ": " + refusal + " - party left as it is (" + party_text(e) + ")");
			return nullptr;
		}
		const auto before = party_text(e);
		for (int i = 0; i < 4; ++i)
		{
			if (!e.seat(i, order.slots[static_cast<std::size_t>(i)]))
			{
				e.log(call + ": ERROR: the game's slot setter (game vt+0xf0) faulted at slot " + std::to_string(i) + " - party now " + party_text(e));
				return nullptr;
			}
		}
		std::string dropped;
		for (const auto& name : order.dropped)
		{
			dropped += (dropped.empty() ? " (" : ", ") + name;
		}
		if (!dropped.empty())
		{
			dropped += " named twice - seated once)";
		}
		e.log(call + " -> " + party_text(e) + " (was " + before + ")" + dropped);
		return nullptr;
	}

	// setSkinset(costume, heroes): every herostat hero in a mission costume (default, 60s, 70s,
	// weaponx, civilian) gets `costume` if listed in `heroes` and it has that costume, default
	// otherwise; a player's own pick (astonishing, aoa, future, winter) stays. The list is needed
	// because the game can't tell XML1's magmacivilian (Magma's slot 8) from civilian (Iceman's).
	// Takes effect as each hero's character loads, at the next zone load.
	template <typename Engine>
	void* set_skinset(Engine& e, void* args)
	{
		const auto values = text_arguments(e, args, 2, "setSkinset");
		if (!values)
		{
			return nullptr;
		}
		const auto call = std::string(prefix) + call_text("setSkinset", *values);
		const auto costume = costume_index((*values)[0]);
		if (!costume)
		{
			e.log(call + ": '" + clean_name((*values)[0]) + "' isn't one of the game's costumes (default, astonishing, aoa, 60s, 70s, weaponx, future, winter, civilian) - costumes left as they are");
			return nullptr;
		}

		struct listed_hero
		{
			std::string name;
			int index = 0;
			std::string result; // for the log
		};
		std::vector<listed_hero> listed;
		std::string unknown;
		for (const auto& name : split_heroes((*values)[1]))
		{
			const auto index = e.hero_index(name);
			if (!index)
			{
				e.log(call + ": ERROR: the game's name lookup (registry vt+0x3c) faulted on '" + name + "' - costumes left as they are");
				return nullptr;
			}
			const auto hero = *index ? e.herostat(*index) : std::optional<bool>(false);
			if (!hero)
			{
				e.log(call + ": ERROR: the game's herostat check (registry vt+0x7c) faulted on '" + name + "' - costumes left as they are");
				return nullptr;
			}
			if (*hero)
			{
				listed.push_back({name, *index, {}});
			}
			else
			{
				unknown += (unknown.empty() ? "" : ", ") + name;
			}
		}

		const auto count = e.hero_count();
		if (!count)
		{
			e.log(call + ": ERROR: the game's hero count (registry vt+0x48) faulted - costumes left as they are");
			return nullptr;
		}
		int others = 0;
		for (int i = 0; i < *count; ++i)
		{
			const auto index = e.hero_at(i);
			const auto with_stats = index ? e.has_stats(*index) : std::nullopt;
			if (!index || !with_stats)
			{
				e.log(call + ": ERROR: reading hero " + std::to_string(i) + " of " + std::to_string(*count) + " faulted - stopped there, " + std::to_string(others) + " others changed");
				return nullptr;
			}
			const auto match = std::find_if(listed.begin(), listed.end(), [&](const listed_hero& h) { return h.index == *index; });
			const bool is_listed = match != listed.end();
			if (!*with_stats)
			{
				if (is_listed)
				{
					match->result = "skipped (no stats object yet)";
				}
				continue; // nothing to dress
			}
			const auto current = e.costume(*index);
			if (!current)
			{
				e.log(call + ": ERROR: reading hero " + std::to_string(*index) + "'s costume faulted - stopped there");
				return nullptr;
			}
			bool has_variant = false;
			if (is_listed && *costume != 0)
			{
				const auto variant = e.has_variant(*index, *costume);
				if (!variant)
				{
					e.log(call + ": ERROR: reading " + match->name + "'s costumes faulted - stopped there");
					return nullptr;
				}
				has_variant = *variant;
			}
			const int next = skinset_costume(*current, is_listed, *costume, has_variant);
			if (next != *current && !e.set_costume(*index, next))
			{
				e.log(call + ": ERROR: writing hero " + std::to_string(*index) + "'s costume faulted - stopped there");
				return nullptr;
			}
			if (is_listed)
			{
				match->result = !mission_costume(*current) ? "keeps " + costume_name(*current) + " (the player's pick)"
				                : *costume != 0 && !has_variant ? "default (has no " + costume_name(*costume) + " costume)"
				                                                : costume_name(next);
			}
			else if (next != *current)
			{
				++others;
			}
		}

		std::string text;
		for (const auto& h : listed)
		{
			text += (text.empty() ? "" : ", ") + h.name + " " + (h.result.empty() ? std::string("not in the game's hero list") : h.result);
		}
		text += std::string(text.empty() ? "" : "; ") + std::to_string(others) + (others == 1 ? " other hero" : " other heroes") + " back to default from a mission costume";
		if (!unknown.empty())
		{
			text += "; not herostat heroes, skipped: " + unknown;
		}
		e.log(call + " -> " + text);
		return nullptr;
	}

	// pushParty(entity): XML1's beginSideMission, first half - "pushsidemission <entity id>" run now
	// through the console (vt+0x18), as extractionPointChange does (0x4a7060-0x4a70df): the record
	// takes the zone, the party of the moment and the entity's spot, so it must come before the
	// flashback's seatParty. Two records at most; a full stack pushes nothing (the game would skip it
	// without a word, 0x5f3684).
	template <typename Engine>
	void* push_party(Engine& e, void* args)
	{
		const auto values = text_arguments(e, args, 1, "pushParty");
		if (!values)
		{
			return nullptr;
		}
		const auto call = std::string(prefix) + call_text("pushParty", *values);
		const auto before = e.side_records();
		if (!before)
		{
			e.log(call + ": ERROR: the side-mission stack (0x48a0e0 -> vt+0x44) couldn't be read - nothing pushed");
			return nullptr;
		}
		if (*before >= side_records_max)
		{
			e.log(call + ": the side-mission stack is full (" + std::to_string(*before) + " records) - nothing pushed; popParty returns to " + record_text(e, *before - 1));
			return nullptr;
		}
		int id = 0;
		switch (e.find_character((*values)[0], id))
		{
		case character_lookup::found:
			break;
		case character_lookup::missing:
			e.log(call + ": no entity by that name - nothing pushed");
			return nullptr;
		case character_lookup::not_character:
			e.log(call + ": that entity isn't a character - nothing pushed");
			return nullptr;
		case character_lookup::fault:
			e.log(call + ": ERROR: looking the entity up (0x4a1700, 0x4654b0) faulted - nothing pushed");
			return nullptr;
		}
		const auto command = "pushsidemission " + std::to_string(id);
		if (!e.run_now(command))
		{
			e.log(call + ": ERROR: the console (vt+0x18) faulted running '" + command + "'");
			return nullptr;
		}
		const auto after = e.side_records();
		if (!after || *after != *before + 1)
		{
			e.log(call + ": " + command + " pushed nothing (no zone loaded?) - " + (after ? std::to_string(*after) : std::string("?")) + " record(s)");
			return nullptr;
		}
		e.log(call + " -> " + command + ": record " + std::to_string(*after) + " of " + std::to_string(side_records_max) + ", " + record_text(e, *after - 1) +
		      (*before > 0 ? " (below it " + record_text(e, *before - 1) + ": a side mission inside a side mission, or left by one that ended in the team menu)" : std::string()));
		return nullptr;
	}

	// A zone for "loadmap <zone> 0 1": the console reads it as one word (it stops at a space or ';').
	inline bool usable_zone(const std::string_view zone)
	{
		return !zone.empty() && std::string("loadmap ").size() + zone.size() + std::string(" 0 1").size() <= console_max &&
		       std::ranges::all_of(zone, [](const char c) { return static_cast<unsigned char>(c) > 0x20 && static_cast<unsigned char>(c) < 0x7f && c != ';'; });
	}

	// Why this popParty repeats the one queued before it (queued_pop), or "" when it doesn't - then
	// what was queued is forgotten.
	template <typename Engine>
	std::string repeated_pop(Engine& e, const int records)
	{
		auto& last = e.state().pop;
		if (!last)
		{
			return {};
		}
		const auto zone = lowercase(e.current_zone().value_or(""));
		if (const auto waiting = e.waiting(); waiting && *waiting > 0 && records == last->records && zone == last->from)
		{
			return "'" + last->command + "' is queued already, from " + last->from + " (the game runs it at its next frame)";
		}
		if (const auto loading = e.zone_loading(); loading && *loading && zone == last->to && records == last->records - (last->restore ? 1 : 0))
		{
			return "'" + last->command + "' has run and the load of " + last->to + " is under way";
		}
		last.reset();
		return {};
	}

	// popParty(fallbackZone): XML1's endSideMission - "restorelastzone 0" queued (console vt+0x1c) as
	// the game's restorelastzone script function queues it (0x4a0760, not while the loading menu is
	// up), then the HUD call every script load function makes. The command seats the top record's
	// names, restores the game-state block and loads its zone at its spot, which pops it. With no
	// record (an old save, a flashback begun with ForcedTeams=0, a debug start) it queues the team
	// menu at `fallbackZone` instead, as loadMapChooseTeam does ("loadmap %s 0 1", 0x4a0d30) - where
	// the game's own command would drop to the main menu. The queue holds two commands. A call while
	// the last one's command still waits, or its load is under way, does nothing (queued_pop).
	template <typename Engine>
	void* pop_party(Engine& e, void* args)
	{
		const auto values = text_arguments(e, args, 1, "popParty");
		if (!values)
		{
			return nullptr;
		}
		const auto call = std::string(prefix) + call_text("popParty", *values);
		const auto zone = std::string(limits_rules::trimmed((*values)[0]));
		const auto records = e.side_records();
		if (!records)
		{
			e.log(call + ": ERROR: the side-mission stack (0x48a0e0 -> vt+0x44) couldn't be read - nothing done");
			return nullptr;
		}
		if (const auto repeat = repeated_pop(e, *records); !repeat.empty())
		{
			e.log(call + ": " + repeat + " - nothing done (one popParty per side mission's end)");
			return nullptr;
		}
		std::string command;
		std::string where;
		std::string to;
		if (*records <= 0)
		{
			if (!usable_zone(zone))
			{
				e.log(call + ": no side-mission record, and '" + zone + "' can't go in a loadmap command - nothing done");
				return nullptr;
			}
			command = "loadmap " + zone + " 0 1";
			where = "no side-mission record: the team menu at " + zone;
			to = zone;
		}
		else
		{
			if (const auto menu = e.current_menu(); menu && lowercase(*menu) == loading_menu)
			{
				e.log(call + ": the game is loading - nothing done");
				return nullptr;
			}
			command = "restorelastzone 0";
			where = "back to " + record_text(e, *records - 1) + " (record " + std::to_string(*records) + ")";
			const auto record = e.side_record_at(*records - 1);
			to = record ? record->zone : std::string();
		}
		const auto queued = e.queue(command);
		if (!queued)
		{
			e.log(call + ": ERROR: the console's queue (vt+0x1c) faulted on '" + command + "'");
			return nullptr;
		}
		if (!*queued)
		{
			e.log(call + ": ERROR: the console's queue refused '" + command + "' (two commands waiting already) - nothing done");
			return nullptr;
		}
		e.state().pop = queued_pop{*records > 0, *records, lowercase(e.current_zone().value_or("")), lowercase(to), command};
		e.leave_hud();
		e.log(call + " -> " + command + " queued, " + where);
		return nullptr;
	}

	// addHero(hero): with [Game] AddHero=1, the game's own dormant routine (game vt+0x170 =
	// 0x46c9f0, XML1's addHero engine code carried over; nothing in XMen2.exe calls it) seats the hero
	// in slot = party count with no reload: it takes the spot of an NPC with the same stats (removing
	// it), loads the hero's package, gives it a controller, spawns it and refreshes the HUD. 1 if the
	// hero is in the party afterwards. Off: 0, and the script runs its own fallback.
	template <typename Engine>
	void* add_hero(Engine& e, void* args)
	{
		const auto values = text_arguments(e, args, 1, "addHero");
		if (!values)
		{
			return e.make_int(0);
		}
		const auto call = std::string(prefix) + call_text("addHero", *values);
		if (!e.forced_teams() || !e.add_hero_on())
		{
			e.log(call + " -> 0: [Game] AddHero is off - the script's own fallback runs");
			return e.make_int(0);
		}
		const auto name = clean_name((*values)[0]);
		if (name.empty())
		{
			e.log(call + " -> 0: no hero named");
			return e.make_int(0);
		}
		if (const auto refusal = refuse_heroes(e, {name, "", "", ""}); !refusal.empty())
		{
			e.log(call + " -> 0: " + refusal);
			return e.make_int(0);
		}
		const auto before = party_text(e);
		const auto seated = e.add_hero(name);
		if (!seated)
		{
			e.log(call + " -> 0: ERROR: the game's addHero (0x46c9f0) faulted - party now " + party_text(e));
			return e.make_int(0);
		}
		// The routine's bool isn't proof: in game (2026-09-28, nyc1_1_3) it returned true and seated nobody.
		// Only a party slot holding the hero counts, so the script's fallback runs whenever it didn't join.
		const auto slots = read_party(e);
		const bool in_party = slots && std::find(slots->begin(), slots->end(), name) != slots->end();
		if (*seated && !in_party)
		{
			e.log(call + " -> 0: the game's addHero (0x46c9f0) reported success but " + name + " is in no party slot - " + party_text(e) +
			      " (was " + before + "); the script's fallback runs");
			return e.make_int(0);
		}
		e.log(call + " -> " + (in_party ? "1: " : "0: the game's addHero (0x46c9f0) refused (party full?) - ") + party_text(e) + " (was " + before + ")");
		return e.make_int(in_party ? 1 : 0);
	}

	// getPartyMember(i): slot i's hero, "" when empty or i isn't 0..3.
	template <typename Engine>
	void* get_party_member(Engine& e, void* args)
	{
		const auto index = e.int_argument(args, 0);
		if (!index)
		{
			e.log(std::string(prefix) + "getPartyMember: ERROR: couldn't read argument 1 - \"\"");
			return e.make_string("");
		}
		std::string name;
		if (*index >= 0 && *index < 4)
		{
			const auto slot = e.slot(*index);
			if (!slot)
			{
				e.log(std::string(prefix) + "getPartyMember(" + std::to_string(*index) + "): ERROR: reading the slot faulted - \"\"");
				return e.make_string("");
			}
			name = *slot;
		}
		e.log(std::string(prefix) + "getPartyMember(" + std::to_string(*index) + ") -> \"" + name + "\"");
		return e.make_string(name);
	}

	// ---- joinHero ------------------------------------------------------------------------------------------
	//
	// XML1's addHero took an NPC into the party where it stood. XML2 has no working call for that (the
	// dormant 0x46c9f0 returns true and seats nobody), and its extraction-point path opens the team menu.
	// joinHero does what that path does after the menu's accept, without the menu:
	//   1. "pushsidemission <_ACTIVE_HERO_'s id>", run now, as extractionPointChange does (0x4a70df):
	//      the zone, the party and the active hero's spot go on the side-mission stack;
	//   2. the hero's name into the first empty party slot of that record (+0xa4 + 0x20 slot, as the
	//      fill 0x48a2a3 writes names) - the state the game itself records for a party member with no
	//      living body at the push (its spot is the zero vector, 0x48a278);
	//   3. "restorelastzone 0" queued (console vt+0x1c), as popParty and the game's restorelastzone
	//      script function queue it (0x4a07d1), then the HUD call they make. At the next frame the command
	//      seats the record's names through the slot setter (0x5f4612-0x5f4667), restores the game-state
	//      block and runs "loadmap <zone> 1" (0x5f4678), which reloads the zone, puts the party back on
	//      the spot and pops the record (0x5f48d3): nothing stays on the stack.
	// The team menu's accept (0x5e0800) instead seats the picks live, then queues "runscript
	// restorelastzone('1')" (setblackbirdparms' code, 0x4a70e3), whose "restorelastzone 1" keeps the
	// seated party and does the same reload. joinHero leaves the live party alone until the command that
	// reloads seats it, so no frame runs with a hero in a slot and no body in the world, and a refusal
	// at any step changes nothing: a record pushed by a call that then fails comes off again through the
	// game's own "cancelsidemission" (0x5f2ce0), run now.
	// 1: the reload is queued (or was, by the same joinHero, and hasn't run yet). 2: the hero is in the
	// party already - nothing to do. 0: refused (logged): the script's own fallback runs.
	constexpr const char* join_entity = "_ACTIVE_HERO_";

	template <typename Engine>
	bool cancel_record(Engine& e, const std::string& call, const int records)
	{
		const bool ran = e.run_now("cancelsidemission");
		const auto now = e.side_records();
		if (ran && now && *now == records)
		{
			e.log(call + ": the record pushed for it came off again (cancelsidemission) - the stack is as it was, " + std::to_string(records) + " record(s)");
			return true;
		}
		e.log(call + ": ERROR: cancelsidemission " + (ran ? std::string("left ") + (now ? std::to_string(*now) : std::string("?")) + " record(s), not " + std::to_string(records)
		                                                    : std::string("faulted")) +
		      " - a record may stay on the side-mission stack (the next restorelastzone would return to it)");
		return false;
	}

	template <typename Engine>
	void* join_hero(Engine& e, void* args)
	{
		const auto values = text_arguments(e, args, 1, "joinHero");
		if (!values)
		{
			return e.make_int(0);
		}
		const auto call = std::string(prefix) + call_text("joinHero", *values);
		const auto refuse = [&](const std::string& why)
		{
			e.log(call + " -> 0: " + why + " - nothing done, the script's own fallback runs");
			return e.make_int(0);
		};
		if (!e.forced_teams() || !e.join_hero_on())
		{
			return refuse(!e.forced_teams() ? "[Game] ForcedTeams is off" : "[Game] JoinHero=0");
		}
		const auto name = clean_name((*values)[0]);
		if (name.empty())
		{
			return refuse("no hero named");
		}
		if (name.size() >= side_name_size)
		{
			return refuse("'" + name + "' is longer than a side-mission record's names (31 characters)");
		}
		if (const auto refusal = refuse_heroes(e, {name, "", "", ""}); !refusal.empty())
		{
			return refuse(refusal);
		}
		const auto slots = read_party(e);
		if (!slots)
		{
			return refuse("ERROR: the party slots couldn't be read");
		}
		if (std::find(slots->begin(), slots->end(), name) != slots->end())
		{
			e.log(call + " -> 2: " + name + " is in the party already (" + describe(*slots) + ") - nothing to do");
			return e.make_int(2);
		}
		if (std::none_of(slots->begin(), slots->end(), [](const std::string& s) { return s.empty(); }))
		{
			return refuse("the party is full (" + describe(*slots) + ")");
		}
		const auto records = e.side_records();
		if (!records)
		{
			return refuse("ERROR: the side-mission stack (0x48a0e0 -> vt+0x44) couldn't be read");
		}
		// The same trigger or script again before the first call's reload has taken the player away: that
		// reload already adds the hero. Another popParty's or joinHero's on its way: refused.
		const auto pending = e.state().pop;
		if (const auto repeat = repeated_pop(e, *records); !repeat.empty())
		{
			if (pending && pending->joined == name)
			{
				e.log(call + " -> 1: " + repeat + " - it adds " + name + " already, nothing more done");
				return e.make_int(1);
			}
			return refuse(repeat);
		}
		if (const auto menu = e.current_menu(); menu && lowercase(*menu) == loading_menu)
		{
			return refuse("the game is loading");
		}
		if (const auto loading = e.zone_loading(); !loading || *loading)
		{
			return refuse(loading ? "a zone load is under way" : "ERROR: the zone manager (vt+0x24) couldn't be read");
		}
		// The reload must be the only command waiting: one queued before it would run first, in the same
		// frame (0x55c2f9: the queue runs until it is empty), and a load there would race the reload.
		if (const auto waiting = e.waiting(); !waiting || *waiting != 0)
		{
			return refuse(waiting ? std::to_string(*waiting) + " console command(s) waiting already (the reload must be the only one)" : "ERROR: the console's queue couldn't be read");
		}
		if (*records >= side_records_max)
		{
			return refuse("the side-mission stack is full (" + std::to_string(*records) + " records; the top: " + record_text(e, *records - 1) + ")");
		}
		int id = 0;
		switch (e.find_character(join_entity, id))
		{
		case character_lookup::found:
			break;
		case character_lookup::missing:
			return refuse(std::string("no ") + join_entity + " entity");
		case character_lookup::not_character:
			return refuse(std::string(join_entity) + " isn't a character");
		case character_lookup::fault:
			return refuse(std::string("ERROR: looking ") + join_entity + " up (0x4a1700, 0x4654b0) faulted");
		}

		// 1. The record: zone, party, the active hero's spot.
		const auto push = "pushsidemission " + std::to_string(id);
		if (!e.run_now(push))
		{
			const auto now = e.side_records();
			if (now && *now == *records + 1)
			{
				cancel_record(e, call, *records);
			}
			return refuse("ERROR: the console (vt+0x18) faulted running '" + push + "'");
		}
		const auto after = e.side_records();
		if (!after || *after != *records + 1)
		{
			return refuse(push + " pushed nothing (no zone loaded?) - " + (after ? std::to_string(*after) : std::string("?")) + " record(s)");
		}
		const int top = *after - 1;
		const auto undo = [&](const std::string& why)
		{
			cancel_record(e, call, *records);
			return refuse(why);
		};

		// 2. The hero in the record's first empty slot.
		const auto record = e.side_record_at(top);
		if (!record)
		{
			return undo("ERROR: the pushed record couldn't be read");
		}
		if (std::find(record->names.begin(), record->names.end(), name) != record->names.end())
		{
			return undo("the pushed record has " + name + " already (" + describe(record->names) + "), unlike the party slots");
		}
		const auto empty = std::find(record->names.begin(), record->names.end(), std::string());
		if (empty == record->names.end())
		{
			return undo("the pushed record's party is full (" + describe(record->names) + ")");
		}
		const int slot = static_cast<int>(empty - record->names.begin());
		if (!e.set_side_name(top, slot, name))
		{
			return undo("ERROR: writing the name into record " + std::to_string(*after) + " faulted");
		}
		const auto written = e.side_record_at(top);
		if (!written || written->names[static_cast<std::size_t>(slot)] != name)
		{
			return undo("ERROR: record " + std::to_string(*after) + " doesn't read back " + name + " in slot " + std::to_string(slot + 1));
		}

		// 3. The reload.
		const std::string command = "restorelastzone 0";
		const auto queued = e.queue(command);
		if (!queued || !*queued)
		{
			return undo(!queued ? "ERROR: the console's queue (vt+0x1c) faulted on '" + command + "'" : "the console's queue refused '" + command + "'");
		}
		e.state().pop = queued_pop{true, *after, lowercase(e.current_zone().value_or("")), lowercase(written->zone), command, name};
		e.leave_hud();
		e.log(call + " -> 1: " + push + " (record " + std::to_string(*after) + " of " + std::to_string(side_records_max) + "), " + name + " added to its party in slot " +
		      std::to_string(slot + 1) + ", " + command + " queued: " + written->zone + " reloads at the saved spot with " + describe(written->names) + " (was " + describe(*slots) +
		      ") and the record comes off");
		return e.make_int(1);
	}
}
