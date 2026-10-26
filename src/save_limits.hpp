#pragma once
#include "patch_transaction.hpp"
namespace save_limits {
// Prepared only for the explicit W2 profile seam. No configuration key selects it.
bool contribute_w2(patch_transaction::contribution&, std::string& why);
bool installed();
unsigned talent_capacity();
unsigned talent_count_offset(unsigned pool);
}
