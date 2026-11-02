#pragma once
#include "patch_transaction.hpp"
#include "save_format_rules.hpp"
namespace save_manager {
// Append all step-3 W2 pieces to the capacity owner's contribution. No selection
// or commit here: step 8 supplies the key and completes the wider W2 transaction.
bool contribute_w2(patch_transaction::contribution& owner, std::string& why);
}
