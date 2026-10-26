#pragma once
#include "xml1_hook_transaction.hpp"
#include <vector>
namespace xml1_air_carry {
// Part of rule 45's existing transaction; no independent rule. flying_carry=false ([Game] Xml1FlyingCarry=0)
// registers none of the five carry handlers and leaves the descent admission (0x433e6c) native; fix7's two flight
// entries, the upkeep (0x4ec7a0) and the %flight_pwr parser filter (0x4f74b2) are written either way.
bool prepare(std::vector<xml1_hook_transaction::patch>&, bool flying_carry = true);
void abandon();
void* lookup(const char*);
}
