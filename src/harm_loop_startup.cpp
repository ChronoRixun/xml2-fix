#include "harm_loop_startup.hpp"
#include "harm_loop_startup_bridge.hpp"
#include "game_version.hpp"
#include "gameplay_agreement.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"
#include <atomic>
#include <cstring>

namespace harm_loop_startup
{
	namespace
	{
		std::atomic<int> state{-1};
		const harm_loop_startup_rules::guard* mismatch()
		{
			__try { return harm_loop_startup_rules::first_mismatch(reinterpret_cast<const std::uint8_t*>(0x400000)); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return &harm_loop_startup_rules::guards[0]; }
		}
	}
	void install(HMODULE game)
	{
		using namespace harm_loop_startup_rules;
		const auto chosen = parse_rule(ini::text(L"Game",L"HarmLoopStartup").value_or(""));
		if (!chosen.set || (chosen.error.empty() && chosen.value == rule::xml2)) return;
		state = 0;
		// Startup scheduling is separate from BoxHarmOrigin (rule 21). Until a
		// rule is assigned, even a refused request must not negotiate as stock.
		gameplay_agreement::reject_unregistered_rule("HarmLoopStartup");
		if (!chosen.error.empty())
		{
			logger::write("harm loop startup: invalid [Game] HarmLoopStartup - stock startup retained");
			return;
		}
		if (reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base || mismatch())
		{
			logger::write("harm loop startup: retail guards refused - stock startup retained");
			return;
		}
		xml1_hook_transaction::patch patch{site,{original.begin(),original.end()},std::vector<unsigned char>(original.size(),0x90)};
		patch.after[0] = 0xe9;
		const auto relative = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&harm_loop_startup_bridge::bridge)) - (site+5);
		std::memcpy(patch.after.data()+1,&relative,4);
		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (xml1_hook_transaction::apply(memory,{patch},&safe))
		{
			state = 1;
			logger::write("harm loop startup: XML1 ordinary loop startup installed; authored first activation retained");
		}
		else if (safe)
			logger::write("harm loop startup: transaction refused - stock startup retained");
		else
		{
			game_version::refuse_startup();
			logger::write("harm loop startup: rollback/protection verification failed; startup refused");
		}
	}
	bool requested() { return state.load() != -1; }
	bool installed() { return state.load() == 1; }
}
