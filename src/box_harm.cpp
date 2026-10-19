#include "box_harm.hpp"
#include "box_harm_bridge.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"
#include <atomic>
#include <cstring>

namespace box_harm
{
	namespace
	{
		std::atomic<int> state{-1};
		const box_harm_rules::guard* mismatch()
		{
			__try { return box_harm_rules::first_mismatch(reinterpret_cast<const std::uint8_t*>(0x400000)); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return &box_harm_rules::guards[0]; }
		}
	}
	void install(HMODULE game)
	{
		using namespace box_harm_rules;
		const auto chosen = parse_rule(ini::text(L"Game",L"BoxHarmOrigin").value_or(""));
		if (!chosen.set || (chosen.error.empty() && chosen.value == rule::xml2)) return;
		state = 0;
		if (!chosen.error.empty())
		{
			logger::write("box harm origin: invalid [Game] BoxHarmOrigin - stock dispatch retained");
			return;
		}
		if (reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base || mismatch())
		{
			logger::write("box harm origin: retail guards refused - stock dispatch retained");
			return;
		}
		xml1_hook_transaction::patch patch{site,{original.begin(),original.end()},std::vector<unsigned char>(original.size(),0x90)};
		patch.after[0] = 0xe9;
		const auto relative = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&box_harm_bridge::bridge)) - (site+5);
		std::memcpy(patch.after.data()+1,&relative,4);
		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (xml1_hook_transaction::apply(memory,{patch},&safe))
		{
			state = 1;
			logger::write("box harm origin: XML1 zero dispatch origin installed; spatial bounds, damage and timers retained");
		}
		else if (safe)
			logger::write("box harm origin: transaction refused - stock dispatch retained");
		else
		{
			game_version::refuse_startup();
			logger::write("box harm origin: rollback/protection verification failed; startup refused");
		}
	}
	bool requested() { return state.load() != -1; }
	bool installed() { return state.load() == 1; }
	bool verified() { return !requested() || installed(); }
}
