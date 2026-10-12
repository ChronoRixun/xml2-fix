#include "pack_amount.hpp"

#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "pack_amount_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <atomic>
#include <cstdint>
#include <vector>

namespace pack_amount
{
	namespace
	{
		using namespace pack_amount_rules;
		constexpr const char* as_before = "packs restore the game's own third";
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

		std::vector<unsigned char> bytes(const std::array<std::uint8_t, 4>& b)
		{
			return {b.begin(), b.end()};
		}
	}

	void install(const HMODULE game)
	{
		const auto narrow = ini::text(L"Game", L"PackAmount").value_or("");
		const auto chosen = parse_rule(narrow);
		if (!chosen.error.empty())
		{
			state = 0;
			logger::write("pack amount: [Game] PackAmount=%s %s - %s", narrow.c_str(), chosen.error.c_str(), as_before);
			return;
		}
		if (!chosen.set || chosen.value == rule::xml2)
		{
			return; // no key or xml2: the game's own third
		}
		state = 0;
		if (!game || reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base)
		{
			logger::write("pack amount: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", as_before);
			return;
		}
		if (const guard* g = first_mismatch_guarded())
		{
			logger::write("pack amount: 0x%08lX isn't the retail code (%s) - %s", static_cast<unsigned long>(g->va), g->what, as_before);
			return;
		}
		const std::vector<xml1_hook_transaction::patch> writes{
			{health_fraction, bytes(retail_fraction), bytes(xml1_fraction_bytes)},
			{energy_fraction, bytes(retail_fraction), bytes(xml1_fraction_bytes)},
		};
		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (xml1_hook_transaction::apply(memory, writes, &safe))
		{
			state = 1;
			logger::write("pack amount: xml1 ([Game] PackAmount=xml1): restoreHealth (0x%08lX) and restoreEnergy (0x%08lX) restore a quarter of the "
			              "maximum, as X-Men Legends 1's packs; a double heal keeps the game's 0.66",
			              static_cast<unsigned long>(health_fraction), static_cast<unsigned long>(energy_fraction));
		}
		else if (safe)
		{
			logger::write("pack amount: the patches were refused (protection or readback) - %s", as_before);
		}
		else
		{
			game_version::refuse_startup();
			logger::write("pack amount: rollback/protection verification failed; process startup refused");
		}
	}

	std::string status()
	{
		const int s = state.load();
		return std::string("pack amount ") + (s == 1 ? "xml1" : s == -1 ? "game's own" : "unavailable");
	}

	bool requested()
	{
		return state.load() != -1;
	}

	bool installed()
	{
		return state.load() == 1;
	}
}
