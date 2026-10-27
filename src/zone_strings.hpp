#pragma once
#include "patch_transaction.hpp"
#include "zone_strings_rules.hpp"

namespace zone_strings
{
 // Adds to the owner's one startup transaction; does not install or select a save profile.
 bool contribute_w2(patch_transaction::contribution& c, std::string& why);
 // Game-thread diagnostic for the last native intern attempt, including the refused limit.
 zone_strings_rules::refusal last_refusal();
}
