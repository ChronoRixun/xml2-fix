#include "world_impact.hpp"
#include "world_impact_bridge.hpp"
#include "gameplay_agreement.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"
#include <atomic>
#include <cstring>

namespace world_impact
{
	namespace
	{
		std::atomic<int> state{-1};
		const world_impact_rules::guard* mismatch()
		{
			__try { return world_impact_rules::first_mismatch(reinterpret_cast<const std::uint8_t*>(0x400000)); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return &world_impact_rules::guards[0]; }
		}
	}
	void install(HMODULE game)
	{
		using namespace world_impact_rules;
		const auto text = ini::text(L"Game", L"ObjectWorldImpact").value_or("");
		const auto chosen = parse_rule(text);
		if (!chosen.set || (chosen.error.empty() && chosen.value == rule::xml2)) return;
		state = 0;
		// Integration must assign this feature a shared gameplay rule ID. Until
		// then an opt-in cannot accidentally admit a peer with different physics.
		gameplay_agreement::reject_unregistered_rule("ObjectWorldImpact");
		if (!chosen.error.empty())
		{
			logger::write("object world impact: invalid [Game] ObjectWorldImpact - stock impact damage retained");
			return;
		}
		if (reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base || mismatch())
		{
			logger::write("object world impact: retail guards refused - stock impact damage retained");
			return;
		}
		xml1_hook_transaction::patch patch{site, {original.begin(), original.end()}, std::vector<unsigned char>(original.size(), 0x90)};
		patch.after[0] = 0xe9;
		const auto relative = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&world_impact_bridge::bridge)) - (site + 5);
		std::memcpy(patch.after.data() + 1, &relative, 4);
		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (xml1_hook_transaction::apply(memory, {patch}, &safe))
		{
			state = 1;
			logger::write("object world impact: XML1 zero damage for non-physical collision targets; physical targets and authored throw hits unchanged");
		}
		else if (safe)
			logger::write("object world impact: transaction refused - stock impact damage retained");
		else
		{
			game_version::refuse_startup();
			logger::write("object world impact: rollback/protection verification failed; startup refused");
		}
	}
	std::string status()
	{
		const auto value = state.load();
		return std::string("object world impact ") + (value == 1 ? "xml1 (online pending rule ID)" : value == -1 ? "xml2" : "unavailable");
	}
}
