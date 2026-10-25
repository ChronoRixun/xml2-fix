#pragma once
#include "xml1_hook_transaction.hpp"

#include <vector>

// Taking NpcPowerRule's installed sites back out (npc_power::withdraw, when NpcSemiAuto's requested weapon brain
// cannot install: the requested configuration is all or nothing). Kept apart so the tests can drive it with a memory
// that fails. Every outcome but `withdrawn` (or `nothing` installed) leaves the rule's patches in the game, so the
// caller refuses startup: a partial configuration never runs, offline or online.
namespace npc_power_withdraw
{
	enum class result
	{
		nothing,   // no site was installed
		withdrawn, // every site is retail again, verified
		failed     // the reverse transaction failed (pages, write, readback); the sites may still be the rule's
	};
	template<class Memory>
	result run(Memory& memory, const std::vector<xml1_hook_transaction::patch>& written)
	{
		if (written.empty())
		{
			return result::nothing;
		}
		std::vector<xml1_hook_transaction::patch> back;
		back.reserve(written.size());
		for (const auto& p : written)
		{
			back.push_back({p.address, p.after, p.before});
		}
		bool safe = true;
		return xml1_hook_transaction::apply(memory, back, &safe) ? result::withdrawn : result::failed;
	}
}
