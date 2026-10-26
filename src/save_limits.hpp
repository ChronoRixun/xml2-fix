#pragma once
#include "patch_transaction.hpp"
namespace save_limits {
// W2 contribution; native_save_layout owns selection and prerequisites.
bool contribute_w2(patch_transaction::contribution&, std::string& why);
bool installed();
unsigned talent_capacity();
unsigned talent_count_offset(unsigned pool);
unsigned objective_registered_count();
}
