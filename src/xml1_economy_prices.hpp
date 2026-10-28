#pragma once
#include "patch_transaction.hpp"

namespace xml1_economy::prices {
// One all-or-nothing rule-52 part, prepared only when the economy is requested.
bool contribute(patch_transaction::transaction&, std::string& failure);
bool installed();
}
