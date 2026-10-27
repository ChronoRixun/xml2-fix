#pragma once
#include "patch_transaction.hpp"
namespace xml1_economy::digest {
bool contribute(patch_transaction::transaction&,std::string& failure);
bool installed();
#ifdef XML2_ECONOMY_DIGEST_TEST
void test_observe(void* shop);
bool test_fault_cleanup();
#endif
void enable(); // opt-in InputPipe diagnostics, before its server thread starts
std::string pipe(const std::string& command); // read-only, pipe thread; events [after], shop
// Native bag outcome, while the outer transaction still owns its captured row.
void bag_result(const char* result);
}
