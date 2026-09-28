#include "forced_teams.hpp"
#include "forced_teams_rules.hpp"
#include "log.hpp"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

namespace forced_teams
{
	namespace
	{
		using namespace forced_teams_rules;

		// Read by the functions at every call (xml2fixFeature, addHero), not when a script compiles.
		std::atomic<bool> forced_teams_on{false};
		std::atomic<bool> add_hero_switch{false};

		// What the functions keep from call to call; the game calls them on its one thread.
		call_state kept;

		// The game's tree keeps a pointer to each entry (0x4d7648-0x4d768a), and every script compile
		// reads the entry through it: the table lives as long as the process.
		func_entry table[table_count]{};

		template <typename T>
		T at(const DWORD va)
		{
			return reinterpret_cast<T>(static_cast<std::uintptr_t>(va));
		}

		// The virtual function at byte offset `slot` of `object`'s vtable. `this` travels in ecx
		// (__thiscall); __fastcall with an unused edx calls it the same way.
		template <typename T>
		T method(void* object, const DWORD slot)
		{
			return reinterpret_cast<T>((*static_cast<void***>(object))[slot / 4]);
		}

		template <std::size_t N>
		void copy_text(const char* text, char (&out)[N])
		{
			std::size_t i = 0;
			for (; text && text[i] && i + 1 < N; ++i)
			{
				out[i] = text[i];
			}
			out[i] = 0;
		}

		using getter_t = void*(__cdecl*)();
		using seat_t = void(__fastcall*)(void* game, void* edx, int slot, std::uint32_t* handle);
		using add_hero_t = bool(__fastcall*)(void* game, void* edx, const char* name);
		using intern_t = void(__fastcall*)(std::uint32_t* handle, void* edx, const char* name);
		using name_text_t = const char*(__fastcall*)(const std::uint32_t* handle, void* edx);
		using name_index_t = short(__fastcall*)(void* registry, void* edx, const char* name);
		using index_flag_t = bool(__fastcall*)(void* registry, void* edx, int index);
		using hero_count_t = int(__fastcall*)(void* registry, void* edx);
		using hero_at_t = short(__fastcall*)(void* registry, void* edx, int i);
		using stats_t = std::uint8_t*(__fastcall*)(void* registry, void* edx, int index);
		using make_int_t = void*(__fastcall*)(void* system, void* edx, int value);
		using make_string_t = void*(__fastcall*)(void* system, void* edx, const char* text);
		using console_line_t = bool(__fastcall*)(void* console, void* edx, const char* line);
		using entity_by_name_t = std::uint32_t*(__cdecl*)(std::uint32_t* out, const char* name);
		using entity_of_handle_t = std::uint8_t*(__fastcall*)(const std::uint32_t* handle, void* edx);
		using class_info_t = const std::uint8_t*(__fastcall*)(void* entity, void* edx);
		using side_stack_t = std::uint8_t*(__fastcall*)(void* missions, void* edx);
		using menu_name_t = const char*(__fastcall*)(void* menus, void* edx);
		using zone_name_t = const char*(__fastcall*)(void* zones, void* edx);
		using zone_flag_t = bool(__fastcall*)(void* zones, void* edx);
		using hud_leave_t = void(__fastcall*)(void* hud, void* edx);

		// ---- Calls into XMen2.exe. No C++ objects in these: every one is SEH-guarded, so a fault in the
		// game's code is logged by the caller instead of taking the game down. --------------------------

		bool bytes_match(const guard& g)
		{
			__try
			{
				return limits_rules::matches(at<const std::uint8_t*>(g.va), g.hex);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		// A game object from its getter, when it is the class the guards were read for.
		void* object(const DWORD getter, const DWORD vtable)
		{
			__try
			{
				void* found = at<getter_t>(getter)();
				return found && *static_cast<const DWORD*>(found) == vtable ? found : nullptr;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return nullptr;
			}
		}

		void* call_make_int(const int value)
		{
			__try
			{
				return at<make_int_t>(make_int_value)(at<getter_t>(script_system_getter)(), nullptr, value);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return nullptr;
			}
		}

		void* call_make_string(const char* text)
		{
			__try
			{
				return at<make_string_t>(make_string_value)(at<getter_t>(script_system_getter)(), nullptr, text);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return nullptr;
			}
		}

		bool read_slot(const int slot, char (&out)[argument_max + 1])
		{
			__try
			{
				auto* game = static_cast<std::uint8_t*>(object(game_getter, game_vtable));
				if (!game)
				{
					return false;
				}
				const auto* handle = reinterpret_cast<const std::uint32_t*>(game + game_party_offset + 4 * slot);
				copy_text(at<name_text_t>(name_text)(handle, nullptr), out);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		// As restorelastzone seats a record's names (0x5f4620-0x5f4667): intern ("" gives 0, an empty
		// slot), then the slot setter, which lowercases and does nothing for the name already there.
		bool call_seat(const int slot, const char* name)
		{
			__try
			{
				void* game = object(game_getter, game_vtable);
				if (!game)
				{
					return false;
				}
				std::uint32_t handle = 0;
				at<intern_t>(intern_name)(&handle, nullptr, name);
				method<seat_t>(game, game_seat_slot)(game, nullptr, slot, &handle);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool call_add_hero(const char* name, bool& seated)
		{
			__try
			{
				void* game = object(game_getter, game_vtable);
				if (!game)
				{
					return false;
				}
				seated = method<add_hero_t>(game, game_add_hero_slot)(game, nullptr, name);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool call_hero_index(const char* name, int& index)
		{
			__try
			{
				void* registry = object(registry_getter, registry_vtable);
				if (!registry)
				{
					return false;
				}
				index = method<name_index_t>(registry, registry_index_slot)(registry, nullptr, name);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool call_registry_flag(const DWORD slot, const int index, bool& value)
		{
			__try
			{
				void* registry = object(registry_getter, registry_vtable);
				if (!registry)
				{
					return false;
				}
				value = method<index_flag_t>(registry, slot)(registry, nullptr, index);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool call_hero_count(int& count)
		{
			__try
			{
				void* registry = object(registry_getter, registry_vtable);
				if (!registry)
				{
					return false;
				}
				count = method<hero_count_t>(registry, registry_hero_count_slot)(registry, nullptr);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool call_hero_at(const int i, int& index)
		{
			__try
			{
				void* registry = object(registry_getter, registry_vtable);
				if (!registry)
				{
					return false;
				}
				index = method<hero_at_t>(registry, registry_hero_at_slot)(registry, nullptr, i);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		// stats+offset of hero `index`: read (write < 0) or written.
		bool stats_byte(const int index, const DWORD offset, int& value, const int write)
		{
			__try
			{
				void* registry = object(registry_getter, registry_vtable);
				if (!registry)
				{
					return false;
				}
				std::uint8_t* stats = method<stats_t>(registry, registry_stats_slot)(registry, nullptr, index);
				if (!stats)
				{
					return false;
				}
				if (write >= 0)
				{
					stats[offset] = static_cast<std::uint8_t>(write);
				}
				value = stats[offset];
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		std::uint8_t* side_stack()
		{
			__try
			{
				void* missions = object(missions_getter, missions_vtable);
				return missions ? method<side_stack_t>(missions, missions_side_stack_slot)(missions, nullptr) : nullptr;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return nullptr;
			}
		}

		bool read_side_count(int& count)
		{
			__try
			{
				const std::uint8_t* stack = side_stack();
				if (!stack)
				{
					return false;
				}
				std::memcpy(&count, stack + side_count_offset, sizeof(count));
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool read_side_record(const int i, char (&zone)[side_zone_size + 1], char (&names)[4][side_name_size + 1])
		{
			__try
			{
				const std::uint8_t* stack = side_stack();
				if (!stack || i < 0 || i >= side_records_max)
				{
					return false;
				}
				const std::uint8_t* record = stack + i * side_record_size;
				for (std::size_t c = 0; c < side_zone_size; ++c)
				{
					zone[c] = static_cast<char>(record[c]);
				}
				zone[side_zone_size] = 0;
				for (int n = 0; n < 4; ++n)
				{
					for (std::size_t c = 0; c < side_name_size; ++c)
					{
						names[n][c] = static_cast<char>(record[side_names_offset + n * side_name_size + c]);
					}
					names[n][side_name_size] = 0;
				}
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		// As extractionPointChange resolves its entity (0x4a7060-0x4a70a1): by name, then the class
		// info's character bit, and the entity's id for pushsidemission.
		character_lookup call_find_character(const char* name, int& id)
		{
			__try
			{
				std::uint32_t handle = 0;
				std::uint32_t* found = at<entity_by_name_t>(entity_by_name)(&handle, name);
				std::uint8_t* entity = at<entity_of_handle_t>(entity_of_handle)(found, nullptr);
				if (!entity)
				{
					return character_lookup::missing;
				}
				const std::uint8_t* info = method<class_info_t>(entity, 0)(entity, nullptr);
				const DWORD bit = *at<const DWORD*>(character_class) + 0x24;
				DWORD word = 0;
				std::memcpy(&word, info + 0x14 + (bit >> 5) * 4, sizeof(word));
				if (!(word & (1u << (bit & 31))))
				{
					return character_lookup::not_character;
				}
				std::memcpy(&id, entity + entity_id_offset, sizeof(id));
				return character_lookup::found;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return character_lookup::fault;
			}
		}

		bool call_console(const DWORD slot, const char* line, bool& accepted)
		{
			__try
			{
				void* console = object(console_getter, console_vtable);
				if (!console)
				{
					return false;
				}
				accepted = method<console_line_t>(console, slot)(console, nullptr, line);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool read_waiting(int& count)
		{
			__try
			{
				const auto* console = static_cast<const std::uint8_t*>(object(console_getter, console_vtable));
				if (!console)
				{
					return false;
				}
				std::memcpy(&count, console + console_waiting, sizeof(count));
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool call_current_zone(char (&out)[zone_name_size + 1])
		{
			__try
			{
				void* zones = object(zones_getter, zones_vtable);
				if (!zones)
				{
					return false;
				}
				copy_text(method<zone_name_t>(zones, zones_current_slot)(zones, nullptr), out);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool call_zone_loading(bool& loading)
		{
			__try
			{
				void* zones = object(zones_getter, zones_vtable);
				if (!zones)
				{
					return false;
				}
				loading = method<zone_flag_t>(zones, zones_loading_slot)(zones, nullptr);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool call_current_menu(char (&out)[argument_max + 1])
		{
			__try
			{
				void* menus = object(menus_getter, menus_vtable);
				if (!menus)
				{
					return false;
				}
				copy_text(method<menu_name_t>(menus, menus_current_slot)(menus, nullptr), out);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool call_leave_hud()
		{
			__try
			{
				void* hud = object(hud_getter, hud_vtable);
				if (!hud)
				{
					return false;
				}
				method<hud_leave_t>(hud, hud_leave_slot)(hud, nullptr);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		// The engine forced_teams_rules' functions work over: XMen2.exe.
		struct game_engine
		{
			std::optional<std::string> text_argument(void* args, const int i)
			{
				char text[argument_max + 1];
				if (!read_text_argument(at<get_argument_t>(argument_getter), args, i, text))
				{
					return std::nullopt;
				}
				return std::string(text);
			}

			std::optional<int> int_argument(void* args, const int i)
			{
				int value = 0;
				if (!read_int_argument(at<get_argument_t>(argument_getter), args, i, value))
				{
					return std::nullopt;
				}
				return value;
			}

			void* make_int(const int value)
			{
				return call_make_int(value);
			}

			void* make_string(const std::string& text)
			{
				return call_make_string(text.c_str());
			}

			bool forced_teams()
			{
				return forced_teams_on.load();
			}

			bool add_hero_on()
			{
				return add_hero_switch.load();
			}

			std::optional<int> hero_index(const std::string& name)
			{
				int index = 0;
				return call_hero_index(name.c_str(), index) ? std::optional<int>(index) : std::nullopt;
			}

			std::optional<bool> herostat(const int index)
			{
				bool value = false;
				return call_registry_flag(registry_herostat_slot, index, value) ? std::optional<bool>(value) : std::nullopt;
			}

			std::optional<std::string> slot(const int i)
			{
				char name[argument_max + 1];
				return read_slot(i, name) ? std::optional<std::string>(name) : std::nullopt;
			}

			bool seat(const int i, const std::string& name)
			{
				return call_seat(i, name.c_str());
			}

			std::optional<int> hero_count()
			{
				int count = 0;
				return call_hero_count(count) ? std::optional<int>(count) : std::nullopt;
			}

			std::optional<int> hero_at(const int i)
			{
				int index = 0;
				return call_hero_at(i, index) ? std::optional<int>(index) : std::nullopt;
			}

			std::optional<bool> has_stats(const int index)
			{
				bool value = false;
				return call_registry_flag(registry_has_stats_slot, index, value) ? std::optional<bool>(value) : std::nullopt;
			}

			std::optional<int> costume(const int index)
			{
				int value = 0;
				return stats_byte(index, stats_costume, value, -1) ? std::optional<int>(value) : std::nullopt;
			}

			std::optional<bool> has_variant(const int index, const int costume)
			{
				int value = 0;
				return costume >= 0 && costume < 10 && stats_byte(index, stats_variants + static_cast<DWORD>(costume), value, -1) ? std::optional<bool>(value != 0) : std::nullopt;
			}

			bool set_costume(const int index, const int costume)
			{
				int value = 0;
				return costume >= 0 && costume < 10 && stats_byte(index, stats_costume, value, costume) && value == costume;
			}

			std::optional<int> side_records()
			{
				int count = 0;
				return read_side_count(count) ? std::optional<int>(count) : std::nullopt;
			}

			std::optional<side_record> side_record_at(const int i)
			{
				char zone[side_zone_size + 1];
				char names[4][side_name_size + 1];
				if (!read_side_record(i, zone, names))
				{
					return std::nullopt;
				}
				return side_record{zone, {names[0], names[1], names[2], names[3]}};
			}

			character_lookup find_character(const std::string& name, int& id)
			{
				return call_find_character(name.c_str(), id);
			}

			bool run_now(const std::string& line)
			{
				bool accepted = false;
				return call_console(console_run_slot, line.c_str(), accepted);
			}

			std::optional<bool> queue(const std::string& line)
			{
				bool accepted = false;
				return call_console(console_queue_slot, line.c_str(), accepted) ? std::optional<bool>(accepted) : std::nullopt;
			}

			std::optional<int> waiting()
			{
				int count = 0;
				return read_waiting(count) ? std::optional<int>(count) : std::nullopt;
			}

			std::optional<std::string> current_menu()
			{
				char name[argument_max + 1];
				return call_current_menu(name) ? std::optional<std::string>(name) : std::nullopt;
			}

			std::optional<std::string> current_zone()
			{
				char name[zone_name_size + 1];
				return call_current_zone(name) ? std::optional<std::string>(name) : std::nullopt;
			}

			std::optional<bool> zone_loading()
			{
				bool loading = false;
				return call_zone_loading(loading) ? std::optional<bool>(loading) : std::nullopt;
			}

			call_state& state()
			{
				return kept;
			}

			void leave_hud()
			{
				if (!call_leave_hud())
				{
					logger::write("forced teams: the HUD's vt+0x80 (0x599260) faulted after queuing a load - the load still runs");
				}
			}

			std::optional<bool> add_hero(const std::string& name)
			{
				bool seated = false;
				return call_add_hero(name.c_str(), seated) ? std::optional<bool>(seated) : std::nullopt;
			}

			void log(const std::string& line)
			{
				logger::write("%s", line.c_str());
			}
		};

		// The handlers the game calls: `void* __cdecl (void* args)`. A C++ exception (out of memory)
		// must not reach the game's executor; an 'i' or 's' function still hands it a value to assign.
		template <void* (*work)(game_engine&, void*), char ret>
		void* __cdecl handler(void* args)
		{
			game_engine e;
			try
			{
				return work(e, args);
			}
			catch (...)
			{
				logger::write("forced teams: ERROR: an exception in a script function - no change");
			}
			return ret == 'i' ? e.make_int(0) : ret == 's' ? e.make_string("") : nullptr;
		}

		// In `functions` order.
		const std::array<const void*, functions.size()> handlers{
			reinterpret_cast<const void*>(&handler<&xml2fix_feature<game_engine>, 'i'>),
			reinterpret_cast<const void*>(&handler<&seat_party<game_engine>, 'n'>),
			reinterpret_cast<const void*>(&handler<&set_skinset<game_engine>, 'n'>),
			reinterpret_cast<const void*>(&handler<&push_party<game_engine>, 'n'>),
			reinterpret_cast<const void*>(&handler<&pop_party<game_engine>, 'n'>),
			reinterpret_cast<const void*>(&handler<&add_hero<game_engine>, 'i'>),
			reinterpret_cast<const void*>(&handler<&get_party_member<game_engine>, 's'>),
		};
		static_assert(static_cast<std::size_t>(function::get_party_member) + 1 == functions.size());

		bool copy_retail_table()
		{
			__try
			{
				build_table(at<const func_entry*>(retail_table), handlers, table);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		// The script system's registered names so far: 0 before the game builds it (it does so at its
		// first 0x4d8770, from the registration itself), 19 once the builtins are in.
		DWORD registered_names()
		{
			__try
			{
				const DWORD system = *at<const DWORD*>(script_system_pointer);
				return system ? *at<const DWORD*>(system + tree_count_offset) : 0;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return 0xffffffff;
			}
		}

		// Both operands at once: 0x49fe31..0x49fe39 is one span on one page of the exe's code, made
		// writable, written, and put back. A refusal writes nothing.
		bool write_operands(const std::array<limits_rules::operand_write, 2>& writes)
		{
			void* span = at<void*>(table_operand);
			const SIZE_T size = count_operand + 4 - table_operand;
			DWORD protection = 0;
			if (!VirtualProtect(span, size, PAGE_EXECUTE_READWRITE, &protection))
			{
				return false;
			}
			for (const auto& w : writes)
			{
				std::memcpy(at<void*>(w.va), &w.value, w.size);
			}
			FlushInstructionCache(GetCurrentProcess(), span, size);
			VirtualProtect(span, size, protection, &protection);
			return true;
		}

		std::optional<std::string> ini_value(const std::wstring& ini, const wchar_t* key)
		{
			wchar_t value[128]{};
			if (GetPrivateProfileStringW(L"Game", key, L"", value, static_cast<DWORD>(std::size(value)), ini.c_str()) == 0)
			{
				return std::nullopt;
			}
			std::string narrow;
			for (const wchar_t* p = value; *p; ++p)
			{
				narrow += *p < 128 ? static_cast<char>(*p) : '?';
			}
			return narrow;
		}
	}

	void install(const HMODULE game)
	{
		const auto ini = (logger::module_dir() / L"xml2-fix.ini").wstring();
		const auto chosen = decide(ini_value(ini, L"ForcedTeams"), ini_value(ini, L"AddHero"));
		for (const auto& note : chosen.notes)
		{
			logger::write("forced teams: %s", note.c_str());
		}
		if (!chosen.registered)
		{
			logger::write("forced teams: off (no [Game] ForcedTeams in xml2-fix.ini) - the game's own script functions only");
			return;
		}

		constexpr const char* refused = "no script functions registered; the mod's scripts open the team menu";
		if (!game || reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base)
		{
			logger::write("forced teams: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", refused);
			return;
		}
		for (const auto& g : guards)
		{
			if (!bytes_match(g))
			{
				logger::write("forced teams: 0x%08lX isn't the retail code (%s) - %s", g.va, g.what, refused);
				return;
			}
		}
		// The game registers its functions once, from its own init (0x40197b -> game vt+0x13c 0x46b750
		// -> 0x49fe30), after this DllMain: past the builtins, it has done so already.
		if (const DWORD names = registered_names(); names > builtin_count)
		{
			logger::write("forced teams: the game has registered its script functions already (%lu names) - %s", names, refused);
			return;
		}
		if (!copy_retail_table())
		{
			logger::write("forced teams: ERROR: the game's function table (0x68a908) couldn't be read - %s", refused);
			return;
		}
		const auto table_address = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&table[0]));
		if (!write_operands(registration_writes(table_address)))
		{
			logger::write("forced teams: can't unprotect XMen2.exe's code at 0x%08lX (error %lu) - %s", table_operand, GetLastError(), refused);
			return;
		}
		forced_teams_on = chosen.forced_teams;
		add_hero_switch = chosen.add_hero;

		std::string names;
		for (const auto& f : functions)
		{
			names += (names.empty() ? "" : ", ") + std::string(f.name);
		}
		logger::write("forced teams: %zu script functions added (%s); ForcedTeams=%d AddHero=%d - the game's registration (0x49fe30) takes the DLL's table at 0x%08lX, "
		              "its %lu entries and these (%lu of the tree's 320 names with the 19 builtins)",
		              functions.size(), names.c_str(), chosen.forced_teams ? 1 : 0, chosen.add_hero ? 1 : 0, table_address, retail_count, builtin_count + table_count);
	}
}
