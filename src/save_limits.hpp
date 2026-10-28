#pragma once
#include "patch_transaction.hpp"
#include <optional>
#include <string>
#include <string_view>
namespace save_limits {
// W2 contribution; native_save_layout owns selection and prerequisites.
bool contribute_w2(patch_transaction::contribution&, std::string& why);
bool installed();
unsigned talent_capacity();
unsigned talent_count_offset(unsigned pool);
unsigned objective_registered_count();
// Another module's retail guard (`hex` at `va`) as it must read once W2 is installed: the bytes W2 rewrote (its rows,
// caves and hash-checked replacements, verified against retail by W2's own transaction) are taken from memory, every
// other byte stays the retail expectation. nullopt when a W2 row's retail bytes disagree with the guard (the two
// modules read different code). Before W2 installs (or without it) the guard is returned unchanged.
std::optional<std::string> expected(unsigned va,std::string_view hex);
}
