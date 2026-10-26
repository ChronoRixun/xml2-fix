#pragma once
#include "patch_transaction.hpp"
namespace xml1_economy::digest {
bool contribute(patch_transaction::transaction&,std::string& failure);
bool installed();
void enable(); // opt-in InputPipe diagnostics, before its server thread starts
std::string pipe(const std::string& command); // read-only, pipe thread; events [after], shop
// Native bag outcome, while the outer transaction still owns its captured row.
void bag_result(const char* result);
}
