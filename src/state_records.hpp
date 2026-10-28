#pragma once
#include "patch_transaction.hpp"

namespace state_records
{
    // Prepare only. The W2 integrator owns adding/committing the contribution.
    bool contribute_w2(patch_transaction::contribution& c, std::string& why);
}
