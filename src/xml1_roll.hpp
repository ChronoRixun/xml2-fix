#pragma once
#include "patch_transaction.hpp"
#include <string>
namespace xml1_economy::roller {
bool contribute(patch_transaction::transaction&,std::string& failure);
bool installed();
std::string status();
std::string recent_rolls(); // bounded read-only test-pipe history
// Normal-depth retail stack, used by the two bridges and mapped-code fixture.
void __cdecl begin(void* stack);
void __cdecl select(void* stack);
void __cdecl finish(void* stack);
}
