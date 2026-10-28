#pragma once

#include <Windows.h>

#include <array>
#include <cstdint>

// Two more of X-Men Legends 1's damage numbers, in [Balance] (balance_parity_rules.hpp, docs/balance.md):
//
//   [Balance]
//   ReceiveRounding = xml1 ; the damage receive's defense and scale stages in whole numbers, as XML1's
//   DamageCap = xml1       ; a hit's damage capped at 32,000 (the game's own: 1,000,000)
//
// Each key on its own; absent or xml2 writes nothing. Gameplay  when either key
// is set: lockstep peers compute every hit, so they must agree.
namespace balance_parity
{
	// Reads the two keys and patches XMen2.exe for them, all writes in one transaction. From DllMain, after balance.
	void install(HMODULE game);

	bool requested(); // either key is set to xml1, or to a value that isn't xml1/xml2 (then unverified)
	bool verified();  // nothing half-installed: every requested part installed
	unsigned installed(); // balance_parity_rules::feature bits
	std::array<std::uint32_t, 4> values();
}
