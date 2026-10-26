#include "world_backfaces.hpp"

#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "world_backfaces_rules.hpp"
#include "world_backfaces_state.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace world_backfaces
{
	namespace
	{
		using namespace world_backfaces_rules;
		std::atomic<int> state{-1}; // 1 xml1 (patched); -1 the game's own; 0 asked for but nothing patched
		scope zones;                 // the listed XML1 zones (all = every zone)
		std::array<std::uint8_t, 32> list_digest{};
		// The current zone's flag, recomputed when the zone manager's path changes (movement traces run on the game
		// thread; the cache is only an optimization: the flag is a pure function of the path).
		char last_zone[zone_name_size]{};
		std::uint8_t last_flag = retail_back_flag;
		std::uint8_t __cdecl zone_flag()
		{
			if (zones.all)
			{
				return retail_back_flag; // never installed without a list; fail safe: XMen2.exe's own
			}
			const auto* manager = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(zones_static));
			std::uint32_t vtable = 0;
			std::memcpy(&vtable, manager, 4);
			if (vtable != zones_vtable)
			{
				return retail_back_flag; // no zone manager yet: XMen2.exe's own
			}
			const char* name = reinterpret_cast<const char*>(manager + zone_name_at);
			if (std::memcmp(name, last_zone, zone_name_size) != 0)
			{
				std::memcpy(last_zone, name, zone_name_size);
				last_flag = flag_for(zones, last_zone, zone_name_size);
				char shown[zone_name_size + 1]{};
				std::memcpy(shown, last_zone, zone_name_size);
				logger::write("world back faces: zone '%s' - %s", shown, last_flag == xml1_back_flag ? "one-sided (an X-Men Legends 1 level)" : "two-sided (XMen2.exe's own)");
			}
			return last_flag;
		}
		auto flag_function = &zone_flag;
		std::uint32_t flag_resume_va = flag_resume;
		// Replaces 0x4a96c0 `push 1; fstp dword [esp+0x10]`: the store first (to the same slot, before the push), then
		// the current zone's flag as the vt+0x88 argument. EAX/ECX/EDX are restored; nothing after 0x4a96c6 reads
		// EFLAGS before setting them, and the x87 stack is empty after the store (the call needs no x87 state).
		__declspec(naked) void flag_bridge()
		{
			__asm {
				fstp dword ptr [esp + 0x0c]
				push eax
				push eax
				push ecx
				push edx
				call dword ptr [flag_function]
				movzx eax, al
				mov [esp + 0x0c], eax
				pop edx
				pop ecx
				pop eax
				jmp dword ptr [flag_resume_va]
			}
		}

		const guard* first_mismatch_guarded()
		{
			__try
			{
				return first_mismatch(reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(limits_rules::image_base)));
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return &guards[0];
			}
		}
	}

	void install(const HMODULE game)
	{
		state = -1;
		const auto value = ini::text(L"Game", L"WorldBackfaces").value_or("");
		const auto wanted = parse(value);
		if (wanted == setting::native)
		{
			return; // no key, empty, xml2 or off: the game's own two-sided movement sweeps
		}
		state = 0;
		zones = {};
		if (wanted == setting::invalid)
		{
			logger::write("world back faces: [Game] WorldBackfaces=%s is neither xml1 nor xml2 - movement sweeps keep the game's own back faces", value.c_str());
			return;
		}
		// Revision 2: [WorldBackfaces] ZoneCount + Z1..Zn = the only zones it applies to (a port's XML1 levels). The list
		// is required: without one, or with a count that is not 1..512 in decimal, nothing is patched (unverified).
		const auto count_text = ini::text(L"WorldBackfaces", L"ZoneCount");
		const auto count = parse_zone_count(count_text.value_or(""));
		if (!count)
		{
			logger::write("world back faces: [WorldBackfaces] ZoneCount=%s is %s - it applies only to the zones listed there; "
			              "movement sweeps keep the game's own back faces", count_text.value_or("").c_str(), count_text ? "not a count of 1..512" : "missing");
			return;
		}
		std::vector<std::string> names;
		for (unsigned i = 1; i <= *count; ++i)
		{
			names.push_back(ini::text(L"WorldBackfaces", (L"Z" + std::to_wstring(i)).c_str()).value_or(""));
		}
		if (!make_scope(std::move(names), zones))
		{
			zones = {};
			logger::write("world back faces: [WorldBackfaces] ZoneCount=%u with a missing, malformed or duplicate zone entry - movement sweeps keep the game's own back faces", *count);
			return;
		}
		list_digest = digest(zones);
		if (!game || reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base)
		{
			logger::write("world back faces: XMen2.exe isn't loaded at 0x400000 (not the game?) - movement sweeps keep the game's own back faces");
			return;
		}
		if (const guard* g = first_mismatch_guarded())
		{
			logger::write("world back faces: 0x%08lX isn't the retail code (%s) - movement sweeps keep the game's own back faces", static_cast<unsigned long>(g->va), g->what);
			return;
		}
		const auto target = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&flag_bridge));
		const auto rel = target - (flag_site + 5);
		const std::vector<xml1_hook_transaction::patch> writes{
			{flag_site, {flag_site_retail.begin(), flag_site_retail.end()},
			 {0xe9, std::uint8_t(rel), std::uint8_t(rel >> 8), std::uint8_t(rel >> 16), std::uint8_t(rel >> 24), 0x90}},
		};
		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (xml1_hook_transaction::apply(memory, writes, &safe))
		{
			state = 1;
			std::memset(last_zone, 0, sizeof last_zone);
			last_flag = retail_back_flag;
			world_backfaces_state::source = &zone_flag; // the entity-capacity owner of 0x4a9650 asks the same function
			char hex[17]{};
			for (int i = 0; i < 8; ++i) std::snprintf(hex + 2 * i, 3, "%02x", unsigned(list_digest[i]));
			logger::write("world back faces: xml1 ([Game] WorldBackfaces=xml1): the movement trace helper (0x4a9650) clears the collider's back-face flag "
			              "(0x%08lX bridged) in %zu listed zones: actor and entity movement there meets only the level's front faces, as in X-Men Legends; "
			              "other zones keep XMen2.exe's two-sided sweep; list sha256 %s...",
			              static_cast<unsigned long>(flag_site), zones.zones.size(), hex);
		}
		else if (safe)
		{
			logger::write("world back faces: the patch was refused (protection or readback) - movement sweeps keep the game's own back faces");
		}
		else
		{
			game_version::refuse_startup();
			logger::write("world back faces: rollback/protection verification failed; process startup refused");
		}
	}

	std::string status()
	{
		const int s = state.load();
		return std::string("world back faces ") + (s == 1 ? "xml1" : s == -1 ? "game's own" : "unavailable");
	}

	bool requested()
	{
		return state.load() != -1;
	}

	bool installed()
	{
		return state.load() == 1;
	}

	bool verified()
	{
		return !requested() || installed();
	}

	std::array<std::uint32_t, 4> values()
	{
		return {installed() ? 1u : 0u, zones.all ? 0u : 1u, static_cast<std::uint32_t>(zones.zones.size()), 0u};
	}

	const std::array<std::uint8_t, 32>* detail()
	{
		return requested() && !zones.all ? &list_digest : nullptr;
	}
}
