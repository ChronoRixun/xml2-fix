#pragma once
#include "patch_transaction.hpp"
namespace native_save_layout {
void prepare(patch_transaction::transaction&,HMODULE game);
void after_commit(patch_transaction::outcome);
bool startup_allowed();
const std::string& startup_error();
bool installed();
std::string status(); // empty while OFF; otherwise the exact W2 telemetry line
}
