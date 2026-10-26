#pragma once
#include "fight_moves_layout.hpp"
#include "patch_transaction.hpp"

namespace fight_moves
{
// Publish capacity only after commit. Base is the current lazy singleton, read on demand.
// Must run before limits::contribute prepares MoveTriggers' side arrays.
void install(HMODULE game,const patch_transaction::hooks* test_hooks=nullptr);
bool installed();
fight_moves_layout::pool_descriptor descriptor();
struct counts { std::uint64_t refused=0,map_refused=0,aliasing=0; };
counts counters();
std::string status();
}
