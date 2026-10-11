#pragma once
#include <Windows.h>
#include "xml1_hook_transaction.hpp"
namespace xml1_turrets {
void install(HMODULE game);
bool prepare_beam(xml1_hook_transaction::patch& patch);
}
