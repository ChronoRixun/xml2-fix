#include "danger_room_gate.hpp"

#include "danger_room_gate_rules.hpp"
#include "ini.hpp"
#include "log.hpp"

#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <cstring>
#include <vector>

namespace danger_room_gate
{
	namespace
	{
		using namespace danger_room_gate_rules;
		using step_fn = void(__fastcall*)(void* menu, void* edx, int arg);
		using getter_t = void*(__cdecl*)();
		using index_t = short(__fastcall*)(void*, void*, const char*);
		using bit_t = bool(__fastcall*)(void*, void*, int);
		using enable_t = void(__fastcall*)(void*, void*, const char*, int);

		using load_fn = int(__fastcall*)(void* registry, void* edx, void* stream);
		using set_t = void(__fastcall*)(void*, void*, int);
		step_fn original = nullptr;
		load_fn original_load = nullptr;
		char item[name_max + 1] = {};
		int last_state = -1; // what the log last said: 0 unread, 1 available, 2 disabled, 3 fault

		bool changed(const int state)
		{
			const bool c = state != last_state;
			last_state = state;
			return c;
		}

		// The profile's bit for the gate character; false = couldn't be read.
		bool read_bit(bool& bit)
		{
			__try
			{
				void* registry = reinterpret_cast<getter_t>(registry_getter)();
				void* profile = reinterpret_cast<getter_t>(profile_getter)();
				if (!registry || !profile || *static_cast<DWORD*>(registry) != registry_vtable || *static_cast<DWORD*>(profile) != profile_vtable)
					return false;
				const short index = reinterpret_cast<index_t>((*static_cast<DWORD**>(registry))[registry_index_slot / 4])(registry, nullptr, gate_character);
				if (index <= 0) return false;
				bit = reinterpret_cast<bit_t>((*static_cast<DWORD**>(profile))[profile_bit_slot / 4])(profile, nullptr, index);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		// The first game's load also sets the profile bits of the save's characters (default.xbe 0x56fe9);
		// for the gate character only. 0 nothing to do, 1 mirrored, -1 fault.
		int mirror_after_load()
		{
			__try
			{
				void* registry = reinterpret_cast<getter_t>(registry_getter)();
				void* profile = reinterpret_cast<getter_t>(profile_getter)();
				if (!registry || !profile || *static_cast<DWORD*>(registry) != registry_vtable || *static_cast<DWORD*>(profile) != profile_vtable)
					return -1;
				const auto* rvt = *static_cast<DWORD**>(registry);
				const auto* pvt = *static_cast<DWORD**>(profile);
				const short index = reinterpret_cast<index_t>(rvt[registry_index_slot / 4])(registry, nullptr, gate_character);
				if (index <= 0) return -1;
				const bool save_bit = reinterpret_cast<bit_t>(rvt[registry_campaign_slot / 4])(registry, nullptr, index);
				const bool profile_bit = reinterpret_cast<bit_t>(pvt[profile_bit_slot / 4])(profile, nullptr, index);
				if (!mirror(save_bit, profile_bit)) return 0;
				reinterpret_cast<set_t>(pvt[profile_set_slot / 4])(profile, nullptr, index);
				return 1;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return -1;
			}
		}

		// In a network game the loaded campaign is a shared session (a joiner receives the host's save, 0x46e2b0 mode 2),
		// not this profile's own: the first game had no network play, so its load rule applies to local loads only.
		bool in_network_game()
		{
			__try
			{
				const auto* net = static_cast<const std::uint8_t*>(reinterpret_cast<getter_t>(net_getter)());
				return net && net[net_online_offset] != 0;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return true; // unknown: leave the profile alone
			}
		}

		int __fastcall load_hook(void* registry, void* edx, void* stream)
		{
			const int result = original_load(registry, edx, stream);
			if (in_network_game()) return result;
			const int mirrored = mirror_after_load();
			if (mirrored > 0)
				logger::write("danger room gate: the loaded save has '%s' in its campaign - the profile's bit set, as the first game's load does", gate_character);
			else if (mirrored < 0)
				logger::write("danger room gate: the profile couldn't be updated after a load - the profile kept as it was");
			return result;
		}

		bool set_item(void* menu, const bool enabled)
		{
			__try
			{
				if (*static_cast<DWORD*>(menu) != main_menu_vtable) return false;
				reinterpret_cast<enable_t>(item_enable)(menu, nullptr, item, enabled ? 1 : 0);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		void __fastcall first_step_hook(void* menu, void* edx, const int arg)
		{
			original(menu, edx, arg);
			bool bit = false;
			const bool read = read_bit(bit);
			if (!read)
			{
				if (changed(0)) logger::write("danger room gate: the profile's '%s' bit couldn't be read - %s left available", gate_character, item);
				return;
			}
			if (!disable(read, bit))
			{
				// Enabled explicitly too: a main menu kept from earlier in the session may still have it disabled.
				set_item(menu, true);
				if (changed(1)) logger::write("danger room gate: '%s' is in this profile's campaigns - %s available", gate_character, item);
				return;
			}
			if (set_item(menu, false))
			{
				if (changed(2)) logger::write("danger room gate: '%s' hasn't been in a campaign on this profile - %s disabled", gate_character, item);
			}
			else if (changed(3))
				logger::write("danger room gate: the item setter faulted - %s left available", item);
		}
	}

	void install(const HMODULE game)
	{
		const auto value = ini::text(L"Game", L"DangerRoomGate");
		if (!value || value->empty()) return;
		if (!valid_item(*value))
		{
			logger::write("danger room gate: [Game] DangerRoomGate=%s isn't a menu item name - the main menu's Danger Room stays available", value->c_str());
			return;
		}
		if (reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base)
		{
			logger::write("danger room gate: XMen2.exe isn't loaded at 0x400000 - the main menu's Danger Room stays available");
			return;
		}
		for (const auto& g : guards)
		{
			if (!limits_rules::matches(reinterpret_cast<const std::uint8_t*>(g.va), g.hex))
			{
				logger::write("danger room gate: 0x%08lX isn't the retail code (%s) - the main menu's Danger Room stays available", g.va, g.what);
				return;
			}
		}
		std::memcpy(item, value->data(), value->size());
		item[value->size()] = 0;
		// Both slots in one transaction: written, read back, and both put back if either fails.
		original = reinterpret_cast<step_fn>(first_step);
		original_load = reinterpret_cast<load_fn>(registry_load);
		const auto before_step = xml1_hook_transaction::pointer_bytes(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(first_step)));
		const auto before_load = xml1_hook_transaction::pointer_bytes(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(registry_load)));
		std::vector<xml1_hook_transaction::patch> writes{
			{first_step_slot, before_step, xml1_hook_transaction::pointer_bytes(reinterpret_cast<const void*>(&first_step_hook))},
			{registry_load_slot, before_load, xml1_hook_transaction::pointer_bytes(reinterpret_cast<const void*>(&load_hook))},
		};
		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (!xml1_hook_transaction::apply(memory, writes, &safe))
		{
			logger::write("danger room gate: the vtable slots 0x%08lX / 0x%08lX didn't take%s - the main menu's Danger Room stays available",
			              first_step_slot, registry_load_slot, safe ? " (both put back)" : " and couldn't be put back");
			return;
		}
		logger::write("danger room gate: [Game] DangerRoomGate=%s - disabled until '%s' has been in a campaign on this profile (the first game's rule)", item, gate_character);
	}
}
