#pragma once
#include "xml1_hook_transaction.hpp"
#include <vector>
namespace xml1_air_carry {
// Part of rule 45's existing transaction; no independent install/key/rule.
bool prepare(std::vector<xml1_hook_transaction::patch>&);
void abandon();
void* lookup(const char*);
}
