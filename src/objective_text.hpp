#pragma once
#include <Windows.h>
#include "patch_transaction.hpp"
namespace objective_text
{
	// [Game] ObjectiveDescriptions: the four hooks in one transaction of their own (the configured baseline)
	void install(HMODULE game);
	// The same hooks as a contribution to another transaction (docs/limits-wave2.md 3.9, W2-L-10): the W2 install
	// relocates the loaded-objective pool whose allocation site 0x489a01 this module also hooks, so under W2 both join
	// the one startup transaction and the shared site is a union-guard expectation, never two patches on one site.
	// false (nothing added) when the key is off or the executable is not the supported one.
	bool contribute(patch_transaction::transaction& t, HMODULE game);
	// W2 owns the shared allocation hook and calls reset with the full index.
	bool contribute(patch_transaction::transaction& t, HMODULE game, bool w2);
	void reset(unsigned index) noexcept;
}
