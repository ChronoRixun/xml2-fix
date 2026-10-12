#include "death_threshold.hpp"

#include "death_threshold_rules.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <cstring>

namespace death_threshold
{
	namespace
	{
		using namespace death_threshold_rules;

		constexpr const char* as_before = "entities die at 0 health as XMen2.exe has it";
		bool wanted = false, applied = false;

		const site* first_mismatch_guarded(bool& constant_ok)
		{
			__try
			{
				return first_mismatch(reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(image_base)), constant_ok);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				constant_ok = false;
				return &sites[0];
			}
		}
	}

	bool requested() { return wanted; }
	bool installed() { return wanted && applied; }
	bool verified() { return !wanted || applied; }
	std::array<std::uint32_t, 4> values() { return {installed() ? 1u : 0u, 0, 0, 0}; }

	void install(const HMODULE game)
	{
		if (applied)
		{
			return; // installed once, for the whole process
		}
		const auto narrow = ini::text(L"Game", L"DeathRule").value_or("");
		const auto chosen = parse_rule(narrow);
		if (!chosen.error.empty())
		{
			logger::write("death rule: [Game] DeathRule=%s %s - %s", narrow.c_str(), chosen.error.c_str(), as_before);
			return;
		}
		if (!chosen.set || chosen.value == rule::xml2)
		{
			if (chosen.set)
			{
				logger::write("death rule: XML2's own ([Game] DeathRule=xml2) - nothing patched");
			}
			return;
		}
		wanted = true;
		if (!game || reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("death rule: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s; online agreement refused", as_before);
			return;
		}
		bool constant_ok = false;
		if (const site* s = first_mismatch_guarded(constant_ok); s || !constant_ok)
		{
			logger::write("death rule: %s isn't the retail code - %s; online agreement refused",
			              s ? s->what : "the game's 0.0 / 1.0 constants", as_before);
			return;
		}
		// The bridge first, in a block of the fix's own made executable once written; then every site and the jump
		// to the bridge in one transaction.
		auto* block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 64, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		if (!block)
		{
			logger::write("death rule: no memory for the death branch's bridge (error %lu) - %s; online agreement refused", GetLastError(), as_before);
			return;
		}
		const auto block_va = static_cast<address>(reinterpret_cast<std::uintptr_t>(block));
		const auto bridge = death_bridge(zero_constant, death_branch_resume_va, block_va);
		std::memcpy(block, bridge.data(), bridge.size());
		DWORD old = 0;
		if (!VirtualProtect(block, 64, PAGE_EXECUTE_READ, &old) || !FlushInstructionCache(GetCurrentProcess(), block, 64))
		{
			logger::write("death rule: can't make the bridge executable (error %lu) - %s; online agreement refused", GetLastError(), as_before);
			VirtualFree(block, 0, MEM_RELEASE);
			return;
		}
		std::vector<xml1_hook_transaction::patch> writes;
		for (const auto& s : sites)
		{
			writes.push_back({s.va, retail_bytes(s), rule_bytes(s)});
		}
		auto entry = retail_bytes(death_branch_site);
		entry.resize(limits_rules::hex_size(death_branch_displaced));
		writes.push_back({death_branch_va, entry, death_jump(block_va)});
		xml1_hook_transaction::memory memory;
		bool safe = true;
		applied = xml1_hook_transaction::apply(memory, writes, &safe);
		if (!applied && safe)
		{
			VirtualFree(block, 0, MEM_RELEASE); // nothing jumps to it
		}
		if (!applied)
		{
			logger::write(safe ? "death rule: writing the sites failed and every byte went back - %s; online agreement refused"
			                   : "death rule: ERROR: writing the sites failed and they could not all be restored - online agreement refused",
			              as_before);
			return;
		}
		logger::write("death rule: X-Men Legends 1's ([Game] DeathRule=xml1): an entity dies on the hit that leaves it below 1 health "
		              "(7 comparisons at 0x4983ce, 0x4983f0, 0x438b47, 0x438c28, 0x439357, 0x43953c, 0x4395fa now against 1.0; the death branch at 0x498407 "
		              "sets a killing hit's health above 0 to 0)");
	}
}
