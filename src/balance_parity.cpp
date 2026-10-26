#include "balance_parity.hpp"

#include "balance_parity_rules.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <cstring>
#include <string>

namespace balance_parity
{
	namespace
	{
		using namespace balance_parity_rules;
		bool wanted = false, bad_value = false;
		unsigned asked = 0, applied = 0;

		const site* first_mismatch_guarded(const unsigned features)
		{
			__try
			{
				return first_mismatch(reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(image_base)), features);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return &receive_site;
			}
		}

		unsigned read_key(const wchar_t* key, const char* name, const unsigned feature)
		{
			const auto raw = ini::text(L"Balance", key).value_or("");
			const auto chosen = parse(raw);
			if (!chosen.error.empty())
			{
				wanted = bad_value = true;
				logger::write("balance: [Balance] %s=%s %s - the game's own stays, and online play stays closed (gameplay rule 41 unverified)", name,
				              std::string(ini_rules::value_text(raw)).c_str(), chosen.error.c_str());
				return 0;
			}
			if (chosen.xml1) wanted = true;
			return chosen.xml1 ? feature : 0u;
		}
	}

	bool requested() { return wanted; }
	unsigned installed() { return applied; }
	bool verified() { return !wanted || (!bad_value && applied == asked); }
	std::array<std::uint32_t, 4> values() { return agreement_values(applied); }

	void install(const HMODULE game)
	{
		if (applied) return;
		asked = read_key(L"ReceiveRounding", "ReceiveRounding", receive_rounding) | read_key(L"DamageCap", "DamageCap", damage_cap);
		if (!asked) return;
		if (!game || reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("balance: XMen2.exe isn't loaded at 0x400000 (not the game?) - the receive stages and the damage cap stay the game's own");
			return;
		}
		if (const site* s = first_mismatch_guarded(asked))
		{
			logger::write("balance: 0x%08lX isn't the retail code (%s) - the receive stages and the damage cap stay the game's own", static_cast<unsigned long>(s->va), s->what);
			return;
		}
		auto* memory_block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		if (!memory_block)
		{
			logger::write("balance: no memory for the receive stubs (error %lu) - the game's own stays", GetLastError());
			return;
		}
		const auto base = static_cast<address>(reinterpret_cast<std::uintptr_t>(memory_block));
		const auto code = block(base);
		std::memcpy(memory_block, code.data(), code.size());
		DWORD old = 0;
		if (!VirtualProtect(memory_block, 4096, PAGE_EXECUTE_READ, &old) || !FlushInstructionCache(GetCurrentProcess(), memory_block, 4096))
		{
			logger::write("balance: can't make the receive stubs executable (error %lu) - the game's own stays", GetLastError());
			VirtualFree(memory_block, 0, MEM_RELEASE);
			return;
		}
		std::vector<xml1_hook_transaction::patch> all;
		for (auto& w : writes_for(asked, base)) all.push_back({w.va, w.before, w.after});
		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (!xml1_hook_transaction::apply(memory, all, &safe))
		{
			if (!safe)
			{
				game_version::refuse_startup();
				logger::write("balance: rollback/protection verification of the receive stages failed; process startup refused");
				return;
			}
			VirtualFree(memory_block, 0, MEM_RELEASE);
			logger::write("balance: writing the receive stages / the cap failed and every byte went back - the game's own stays");
			return;
		}
		applied = asked;
		if (applied & receive_rounding)
			logger::write("balance: X-Men Legends 1's receive stages ([Balance] ReceiveRounding=xml1, default.xbe 0x45517..0x45558): the damage, "
			              "the damage less defense (at least 0) and that times the scale are whole numbers, rounded toward zero (0x437a54, 0x437a75)");
		if (applied & damage_cap)
			logger::write("balance: X-Men Legends 1's damage cap ([Balance] DamageCap=xml1, default.xbe 0x5ce0d): a hit does at most 32000 (the game's own: 1000000; 0x453bfd, 0x453c0c)");
	}
}
