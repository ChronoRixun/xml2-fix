#include "test_state.hpp"
#include "save_limits.hpp"
#include "entity_capacity.hpp"
#include "entity_capacity_layout.hpp"
#include "actor_pools.hpp"
#include "e4_observe.hpp"
#include "char_sounds.hpp"
#include "motion_pool.hpp"
#include "resident_stats.hpp"
#include "particle_scratch.hpp"
#include "script_pools.hpp"
#include "test_state_rules.hpp"
#include <Windows.h>
#include <mutex>
namespace test_state
{
	namespace
	{
		struct memory
		{
			bool read(std::uint32_t a, void* out, std::size_t n)
			{
				__try
				{
					std::memcpy(out, reinterpret_cast<const void*>(a), n);
					return true;
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
					return false;
				}
			}
		};
		std::mutex lock;
		test_state_rules::tracker history;
		std::string error = "no game-thread sample"; // cleared by the first supported sample
		std::string state = test_state_rules::error_json(error);
		std::string objectives = state;
		std::uint64_t last = 0;
		// Refuse unsupported code layouts before following any object pointers. These
		// sites are read-only accessors, not any of the fix's patched call sites.
		bool supported()
		{
			constexpr std::array<limits_rules::guard, 7> guards{{{0x4c6bd0, "8b442404568bf18b9638", "entity generation lookup"},
																 {0x488aa0, "0fb681a90100008a8018", "objective count lookup"},
																 {0x488ab0, "0fb681a90100008a8018", "objective completion lookup"},
																 {0x488ad0, "0fb681a90100008a8018", "objective hidden lookup"},
																 {0x5d8640, "8b819060080085c07404", "current menu name"},
																 {0x4a36f0, "8b4c240483ec08566a00", "health script reader"},
																 {0x4a1ac0, "8b4c240483ec10566a00", "position script reader"}}};
			memory mem;
			for (auto& g : guards)
			{
				std::uint8_t bytes[10]{};
				// Limits wave 2: the three objective lookups start with W2's wide-index caves when its layout is
				// installed; those bytes are W2's (save_limits::expected), the rest of each guard stays retail.
				const auto expected = save_limits::expected(g.va, g.hex);
				if (!expected || !mem.read(g.va, bytes, sizeof bytes) || !limits_rules::matches(bytes, *expected))
				{
					return false;
				}
			}
			return true;
		}
	} // namespace
	void sample()
	{
		const auto now = GetTickCount64();
		if (last && now - last < 100)
		{
			return;
		}
		last = now;
		e4_observe::sample();
		actor_pools::sample();
		resident_stats::sample();
		static const bool valid = supported();
		if (!valid)
		{
			std::lock_guard guard(lock);
			error = "unsupported executable layout";
			state = objectives = test_state_rules::error_json(error);
			return;
		}
		memory mem;
		test_state_rules::reader<memory> reader(mem);
		{
			const auto& d = entity_capacity::installed();
			reader.table = {d.world_object, d.world_core.entities, d.world_core.live_bits, d.world_core.handles, d.world};
		}
		auto s = reader.read(now);
		std::lock_guard guard(lock);
		error.clear();
		history.observe(s);
		state = test_state_rules::state_json(s);
		objectives = test_state_rules::objectives_json(s);
	}
	std::string reply(const std::string& command)
	{
		std::lock_guard guard(lock);
		if (command == "state")
		{
			return state;
		}
		if (command == "resident_stats") return resident_stats::telemetry_json();
		if (command == "resident_census") return resident_stats::census_json();
		if (command == "motion_pool") return motion_pool::telemetry_json();
		if (command == "char_sounds") return char_sounds::telemetry_json();
		if (command == "script_pools") return script_pools::telemetry_json();
		if (command == "actor_pools") return actor_pools::telemetry_json();
		if (command == "particle_scratch") return particle_scratch::telemetry_json();
		if (command == "objectives")
		{
			return objectives;
		}
		return test_state_rules::events_reply(error, history);
	}
} // namespace test_state
