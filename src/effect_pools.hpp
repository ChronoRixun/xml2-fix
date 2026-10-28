#pragma once
#include "effect_pools_rules.hpp"
#include "patch_transaction.hpp"
#include <string>

namespace effect_pools {
using effect_pools_rules::pool;
using effect_pools_rules::descriptor;
// Published only after a verified transaction. At stock the descriptor is readable
// only after its retail guards have been checked. The heap pool stays lazy.
const descriptor& layout(pool p);
std::uint32_t object_address(pool p);
bool installed(pool p);
// Enlist before exports builds its startup transaction, including standalone
// installations without [Save] Format=lc or another agreement-enabling feature.
void require_agreement(HMODULE game);
void install(HMODULE game);
std::string status();
// Shared production/test preparation: no writes or publication. The caller owns
// vector backing, and keeps it alive if commit cannot certify rollback.
bool prepare(pool p,std::uint32_t n,std::uint32_t vector_object,
    patch_transaction::contribution& out,std::string& why);
}
