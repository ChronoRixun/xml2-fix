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
#include <vector>

namespace world_backfaces
{
	namespace
	{
		using namespace world_backfaces_rules;
		std::atomic<int> state{-1}; // 1 xml1 (patched); -1 the game's own; 0 asked for but nothing patched

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
		if (wanted == setting::invalid)
		{
			logger::write("world back faces: [Game] WorldBackfaces=%s is neither xml1 nor xml2 - movement sweeps keep the game's own back faces", value.c_str());
			return;
		}
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
		const std::vector<xml1_hook_transaction::patch> writes{
			{back_flag_operand, {retail_back_flag}, {xml1_back_flag}},
		};
		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (xml1_hook_transaction::apply(memory, writes, &safe))
		{
			state = 1;
			world_backfaces_state::trace_back_faces = xml1_back_flag; // the entity-capacity owner of 0x4a9650, when installed
			logger::write("world back faces: xml1 ([Game] WorldBackfaces=xml1): the movement trace helper (0x4a9650) clears the collider's back-face flag "
			              "(0x%08lX push 0): actor and entity movement meets only the level's front faces, as in X-Men Legends",
			              static_cast<unsigned long>(back_flag_operand - 1));
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
}
