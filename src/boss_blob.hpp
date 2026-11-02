#pragma once
#include <Windows.h>
#include <cstdint>
#include <vector>
#include "xml1_hook_transaction.hpp"
namespace boss_blob {
bool prepare(HMODULE,std::vector<xml1_hook_transaction::patch>&);
void committed(bool);
bool installed();
bool brain(void*,std::uint32_t*);
// Runs on ordinary AI hero updates as well as the boss update, from the sole dispatcher.
bool companion(void*,std::uint32_t*);
bool in_scope();
void on_default_target(const char*);
}
